/*
 * parser.c
 *
 *  Created on: 20.04.2026
 *      Author: HgaderGit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "parser.h"

/* Die Strukturen werden per memcpy direkt aus dem Telegramm befuellt. */
_Static_assert(sizeof(MBusDataHeader_Amis) == 15, "MBusDataHeader_Amis muss 15 Byte gross sein");
_Static_assert(sizeof(TTimeTypeI) == 6, "TTimeTypeI muss 6 Byte gross sein");
_Static_assert(sizeof(TTimeTypeF) == 4, "TTimeTypeF muss 4 Byte gross sein");
_Static_assert(sizeof(TTimeTypeG) == 2, "TTimeTypeG muss 2 Byte gross sein");

/* Mehr DIFE-Erweiterungsbytes werden nicht ausgewertet (verhindert Ueberlauf der Shifts). */
#define MAX_DIFE_EXTENSIONS		4

/* Alle Symbole sind static, damit sie nicht mit anderen Komponenten kollidieren. */
static AmisDevice	deviceAmis;
static int			deviceInitialized = 0;
static double		raw170 = 0;
static double		raw270 = 0;

static const int difLengths[16] = { 0, 1, 2, 3, 4, 4, 6, 8, 0, 1, 2, 3, 4, 255, 6, 0 };

static int GetDifLength(unsigned char dif)
{
	return(difLengths[(dif&0x0F)]);
}

const char* AmisResultText(AmisResult result)
{
	switch (result)
	{
		case AMIS_OK:           return "OK";
		case AMIS_ERR_FRAME:    return "Rahmenformat ungültig";
		case AMIS_ERR_CHECKSUM: return "Prüfsumme falsch";
		case AMIS_ERR_LENGTH:   return "Datenlänge passt nicht zum Header";
		case AMIS_ERR_CONFIG:   return "Unbekannte Verschlüsselungskonfiguration";
		case AMIS_ERR_DECRYPT:  return "Entschlüsselung fehlgeschlagen - aes_key prüfen";
	}
	return "Unbekannter Fehler";
}

/* Tage seit 1970-01-01 fuer ein Datum im proleptischen gregorianischen Kalender (m = 1..12) */
static int64_t DaysFromCivil(int y, int m, int d)
{
	int64_t era;
	unsigned yoe, doy, doe;

	y -= (m <= 2);
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (unsigned)(y - era * 400);
	doy = (unsigned)((153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1);
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

static void SetValue(DeviceDataPoint *pDataPoint, double value)
{
	snprintf(pDataPoint->value, sizeof(pDataPoint->value), "%.0f", value);
	pDataPoint->valid = 1;
}

static void ResetValidFlags(void)
{
	AmisDevice* pDevice = &deviceAmis;
	int i;

	pDevice->time.valid = 0;
	pDevice->date.valid = 0;
	pDevice->device.meterTimeValid = 0;
	pDevice->device.meterTimeFlagInvalid = 0;
	for (i = 0; i < MAX_TARIFS_REGISTERS; i++)
	{
		pDevice->m18x[i].valid = 0;
		pDevice->m28x[i].valid = 0;
		pDevice->m38x[i].valid = 0;
		pDevice->m48x[i].valid = 0;
	}
	pDevice->m170.valid = 0;
	pDevice->m270.valid = 0;
	pDevice->m370.valid = 0;
	pDevice->m470.valid = 0;
	pDevice->m01280.valid = 0;
	pDevice->m1570.valid = 0;
}

/*
 * Wertet einen Zeitstempel-Datensatz aus (Typ I, F oder G).
 * Rueckgabe 1 = Datensatz erkannt und uebernommen, 0 = kein Zeitstempel.
 */
static int AmisDecodeDateTime(const unsigned char *pBuffer, unsigned char dif, unsigned char vifPrimary)
{
	AmisDevice* pDevice = &deviceAmis;
	struct tm  dateTime;
	int year = 0;
	int hasTime = 0;
	int fMonth = 0, fDay = 0, fHour = 0, fMinute = 0, fSecond = 0, fInvalid = 0;
	char timeStr[sizeof(pDevice->time.value)];
	char dateStr[sizeof(pDevice->date.value)];

	memset(&dateTime, 0, sizeof(dateTime));
	timeStr[0] = 0x00;
	dateStr[0] = 0x00;

	if (vifPrimary == 0x6D && (dif & 0x0F) == 0x06)
	{
		TTimeTypeI timeTypeI;

		memcpy(&timeTypeI, pBuffer, sizeof(timeTypeI));
		year = 2000 + timeTypeI.year_lsb + (timeTypeI.year_msb << 3);
		snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", timeTypeI.hour, timeTypeI.minute, timeTypeI.second);
		snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%4d", timeTypeI.day, timeTypeI.month, year);
		dateTime.tm_mon = timeTypeI.month - 1;
		dateTime.tm_mday = timeTypeI.day;
		dateTime.tm_hour = timeTypeI.hour;
		dateTime.tm_min = timeTypeI.minute;
		dateTime.tm_sec = timeTypeI.second;
		fMonth = timeTypeI.month; fDay = timeTypeI.day; fHour = timeTypeI.hour;
		fMinute = timeTypeI.minute; fSecond = timeTypeI.second; fInvalid = timeTypeI.invalid;
		hasTime = 1;
	}
	else if (vifPrimary == 0x6D && (dif & 0x0F) == 0x04)
	{
		TTimeTypeF timeTypeF;

		memcpy(&timeTypeF, pBuffer, sizeof(timeTypeF));
		if (timeTypeF.hundredYears == 0) timeTypeF.hundredYears = 1;
		year = 1900 + 100 * timeTypeF.hundredYears + ((timeTypeF.yearMsb << 3) + timeTypeF.yearLsb);
		snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeTypeF.hour, timeTypeF.minute);
		snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%4d", timeTypeF.day, timeTypeF.month, year);
		dateTime.tm_mon = timeTypeF.month - 1;
		dateTime.tm_mday = timeTypeF.day;
		dateTime.tm_hour = timeTypeF.hour;
		dateTime.tm_min = timeTypeF.minute;
		fMonth = timeTypeF.month; fDay = timeTypeF.day; fHour = timeTypeF.hour;
		fMinute = timeTypeF.minute; fSecond = 0; fInvalid = timeTypeF.invalid;
		hasTime = 1;
	}
	else if (vifPrimary == 0x6C && (dif & 0x0F) == 0x02)
	{
		TTimeTypeG timeTypeG;

		memcpy(&timeTypeG, pBuffer, sizeof(timeTypeG));
		year = 2000 + ((timeTypeG.yearMsb << 3) + timeTypeG.yearLsb);
		snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%4d", timeTypeG.day, timeTypeG.month, year);
		dateTime.tm_mon = timeTypeG.month - 1;
		dateTime.tm_mday = timeTypeG.day;
	}
	else
	{
		return 0;
	}

	if (hasTime)
	{
		memcpy(pDevice->time.value, timeStr, sizeof(pDevice->time.value));
		pDevice->time.valid = 1;

		/* Zaehlerzeit unabhaengig von Zeitzone und Sommerzeit (fuer das Viertelstunden-Raster) */
		if (fMonth >= 1 && fMonth <= 12 && fDay >= 1 && fDay <= 31 && fHour <= 23 && fMinute <= 59 && fSecond <= 59 && year >= 2000 && year <= 2200)
		{
			pDevice->device.localSeconds = DaysFromCivil(year, fMonth, fDay) * 86400 + fHour * 3600 + fMinute * 60 + fSecond;
			pDevice->device.monthId = year * 12 + (fMonth - 1);
			snprintf(pDevice->device.dateTime, sizeof(pDevice->device.dateTime), "%04d-%02d-%02d %02d:%02d:%02d", year, fMonth, fDay, fHour, fMinute, fSecond);
			pDevice->device.meterTimeFlagInvalid = (uint8_t)(fInvalid ? 1 : 0);
			pDevice->device.meterTimeValid = 1;
		}
	}
	memcpy(pDevice->date.value, dateStr, sizeof(pDevice->date.value));
	pDevice->date.valid = 1;

	/* Ein reines Datum (Typ G) ueberschreibt keinen Zeitstempel aus Datum+Uhrzeit. */
	if (hasTime || pDevice->time.valid == 0)
	{
		time_t ts;

		dateTime.tm_isdst = -1; // Automatisch auf Sommerzeit pruefen
		dateTime.tm_year = year - 1900;
		ts = mktime(&dateTime);
		if (ts != (time_t)-1) pDevice->device.timeStamp = ts;
	}
	return 1;
}

static void AmisDecodeData(unsigned char dif, int tarif, int subunit, int storage, unsigned char vifExtension, unsigned char vifPrimary, unsigned char vifCombinableExtension, unsigned char vifCombinable, unsigned char* pBuffer, int dataLength)
{
	int32_t	value32 = 0;
	AmisDevice* pDevice = &deviceAmis;
	uint8_t		handled = 0;

	(void)dataLength;

	switch ((dif & 0x0F))
	{
		case 0x06:	/* 48 Bit: Zeitstempel Typ I */
		case 0x02:	/* 16 Bit: Datum Typ G */
		{
			if (AmisDecodeDateTime(pBuffer, dif, vifPrimary))
			{
				pDevice->countHandled++;
				handled = 1;
			}
			break;
		}

		case 0x04:	/* 32 Bit: Zeitstempel Typ F oder Messwerte */
		{
			double scaled;
			int    exp10 = (vifPrimary & 0x07) - 3;
			/* Gueltiger Tarifregister-Index oder -1 (z.B. durch DIFE-Kette ausserhalb des Bereichs) */
			int    t = (tarif >= 0 && tarif < MAX_TARIFS_REGISTERS) ? tarif : -1;

			if (AmisDecodeDateTime(pBuffer, dif, vifPrimary))
			{
				pDevice->countHandled++;
				handled = 1;
				break;
			}

			memcpy(&value32, pBuffer, 4);
			scaled = (double)(uint32_t)value32 * pow(10, exp10);

			/* 1-0:1.7.0 / 1-0:2.7.0  Wirkleistung Bezug / Lieferung */
			if (dif == 0x04 && (vifPrimary >= 0x28 && vifPrimary <= 0x2F) && vifExtension == 0x00 && vifCombinableExtension == 0x00)
			{
				if (vifCombinable == 0x00)
				{
					raw170 = scaled;
					SetValue(&pDevice->m170, scaled);
					pDevice->countHandled++;
					handled = 1;
				}
				else if (vifCombinable == 0x3C)
				{
					raw270 = scaled;
					SetValue(&pDevice->m270, scaled);
					pDevice->countHandled++;
					handled = 1;
				}
			}

			/* 1-0:3.7.0 / 1-0:4.7.0  Blindleistung */
			if (dif == 0x04 && (vifPrimary >= 0x10 && vifPrimary <= 0x1F) && vifExtension == 0xFB && vifCombinableExtension == 0x00)
			{
				if (vifCombinable == 0x00)
				{
					SetValue(&pDevice->m370, scaled);
					pDevice->countHandled++;
					handled = 1;
				}
				else if (vifCombinable == 0x3C)
				{
					SetValue(&pDevice->m470, scaled);
					pDevice->countHandled++;
					handled = 1;
				}
			}

			/* 1-0:1.8.x Wirkenergie Bezug */
			if (dif == 0x04 && (vifPrimary <= 0x07) && vifExtension == 0x00 && vifCombinable == 0x00 && vifCombinableExtension == 0x00 && t >= 0)
			{
				SetValue(&pDevice->m18x[t], floor(scaled));
				pDevice->countHandled++;
				handled = 1;
			}

			/* 1-0:2.8.x Wirkenergie Lieferung */
			if (dif == 0x04 && (vifPrimary <= 0x07) && vifExtension == 0x00 && vifCombinable == 0x3C && vifCombinableExtension == 0x00 && t >= 0)
			{
				SetValue(&pDevice->m28x[t], scaled);
				pDevice->countHandled++;
				handled = 1;
			}

			/* 1-0:3.8.x Blindenergie Bezug */
			if (dif == 0x04 && (vifPrimary <= 0x07) && vifExtension == 0xFB && vifCombinable == 0x73 && vifCombinableExtension == 0x00 && t >= 0)
			{
				SetValue(&pDevice->m38x[t], floor(scaled));
				pDevice->countHandled++;
				handled = 1;
			}

			/* 1-0:4.8.x Blindenergie Lieferung */
			if (dif == 0x04 && (vifPrimary <= 0x07) && vifExtension == 0xFB && vifCombinable == 0x3C && vifCombinableExtension == 0xF3 && t >= 0)
			{
				SetValue(&pDevice->m48x[t], floor(scaled));
				pDevice->countHandled++;
				handled = 1;
			}

			/* 0-0:0.128.0 Inkasso-Energie */
			if (dif == 0x04 && (vifPrimary <= 0x07) && vifExtension == 0x00 && vifCombinable == 0x04 && vifCombinableExtension == 0xFF)
			{
				SetValue(&pDevice->m01280, floor(scaled));
				pDevice->countHandled++;
				handled = 1;
			}
			break;
		}
	}

	if (!handled)
	{
		if (pDevice->unknownDifVif == 0 && dif != 0x00) /* Log first occurence of unknown DIF/VIF */
		{
			pDevice->unknown.mbus.dif = dif;
			pDevice->unknown.mbus.vifPrimary = vifPrimary;
			pDevice->unknown.mbus.vifExtension = vifExtension;
			pDevice->unknown.mbus.vifCombinable = vifCombinable;
			pDevice->unknown.mbus.vifCombinableExtension = vifCombinableExtension;
			pDevice->unknown.mbus.tarif = tarif;
			pDevice->unknown.mbus.subunit = subunit;
			pDevice->unknown.mbus.storage = storage;
			pDevice->unknownDifVif = 1;
		}
	}
}

/* Prueft vor jedem Lesezugriff, ob noch genug Bytes im entschluesselten Block vorhanden sind. */
#define NEED(n)		do { if (index + (n) > limit) return; } while (0)

static void AmisParseRecords(void)
{
	unsigned char dif;
	unsigned char* pDife;
	DIFE          difExtended;
	int           tarif = 0, subunit = 0, storage = 0;
	unsigned char vifExtension, vifPrimary, vifCombinable, vifCombinableExtension;
	int           index = 0, dataLength, difExtendedCount = 0;
	unsigned char data[16];
	AmisDevice* pDevice = &deviceAmis;
	uint8_t* pBuffer = &pDevice->device.decodedData[2];
	/* Die ersten beiden Bytes (0x2F 0x2F) wurden bereits uebersprungen. */
	const int limit = (pDevice->device.dataSize > 2) ? (int)pDevice->device.dataSize - 2 : 0;

	memset(&difExtended, 0x00, sizeof(difExtended));
	pDife = (unsigned char*)&difExtended;

	while (index < limit)
	{
		vifExtension = vifPrimary = vifCombinableExtension = vifCombinable = 0;
		*pDife = 0;

		NEED(1);
		dif = pBuffer[index++];

		storage = (dif & 0x40) >> 6; /* LSB of storage number */
		if (dif & 0x80)
		{
			dif = dif & 0x7F;

			NEED(1);
			*pDife = pBuffer[index++];

			tarif = difExtended.tarif;
			subunit = difExtended.subUnit;
			storage += (difExtended.storageNumber << 1);

			while (difExtended.extension)
			{
				if (++difExtendedCount > MAX_DIFE_EXTENSIONS) return;
				NEED(1);
				*pDife = pBuffer[index++];

				tarif += (difExtended.tarif << (2 * difExtendedCount));
				subunit += (difExtended.subUnit << (1 * difExtendedCount));
				storage += (difExtended.storageNumber << (4 * difExtendedCount + 1));
			}
			difExtendedCount = 0;
		}

		if (dif == 0x2F)
		{
			/* Idle filler (not to be interpreted), following byte = DIF of next record */
			index++;
			continue;
		}

		if (((dif & 0x0F) == 0x0D) || ((dif & 0x0F) == 0x08) || ((dif & 0x0F) == 0x0F))
		{
			// !!! Not handled yet !!!
			return;
		}

		NEED(1);
		vifPrimary = pBuffer[index++];
		if (vifPrimary == 0xFB || vifPrimary == 0xFD)
		{
			vifExtension = vifPrimary;
			NEED(1);
			vifPrimary = pBuffer[index++];
		}

		vifCombinable = 0;
		if (vifPrimary & 0x80)
		{
			vifPrimary = vifPrimary & 0x7F;
			NEED(1);
			vifCombinable = pBuffer[index++];
			while (vifCombinable & 0x80)
			{
				vifCombinableExtension = vifCombinable;
				NEED(1);
				vifCombinable = pBuffer[index++];
			}
		}

		dataLength = GetDifLength(dif);
		NEED(dataLength);
		memset(data, 0x00, sizeof(data));
		memcpy(data, &pBuffer[index], dataLength);
		index += dataLength;

		AmisDecodeData(dif, tarif, subunit, storage, vifExtension, vifPrimary, vifCombinableExtension, vifCombinable, data, dataLength);

		tarif = subunit = storage = 0;
	}
}

static void AmisParseMBus(void)
{
	AmisDevice* pDevice = &deviceAmis;

	pDevice->countHandled = 0;
	ResetValidFlags();

	AmisParseRecords();

	/* Wirkleistung gesamt = Bezug - Lieferung (unabhaengig von der Reihenfolge im Telegramm) */
	if (pDevice->m170.valid && pDevice->m270.valid)
	{
		SetValue(&pDevice->m1570, raw170 - raw270);
	}
}


AmisDevice* AmisInitDevice(const uint8_t *pAesKey)
{
	AmisDevice* pDevice = &deviceAmis;

	if (deviceInitialized) mbedtls_aes_free(&pDevice->device.aesContext);

	memset(&deviceAmis, 0x00, sizeof(deviceAmis));
	mbedtls_aes_init(&pDevice->device.aesContext);
	mbedtls_aes_setkey_dec(&pDevice->device.aesContext, pAesKey, 128);
	deviceInitialized = 1;
	return(&deviceAmis);
}

AmisResult AmisCheckFrame(const uint8_t *pFrame, size_t size)
{
	size_t  len, i;
	uint8_t sum = 0;

	if (pFrame == NULL || size < 7) return AMIS_ERR_FRAME;
	if (pFrame[0] != 0x68 || pFrame[3] != 0x68 || pFrame[1] != pFrame[2]) return AMIS_ERR_FRAME;

	len = pFrame[1];
	if (size != len + 6) return AMIS_ERR_FRAME;
	if (pFrame[size - 1] != 0x16) return AMIS_ERR_FRAME;

	/* M-Bus Pruefsumme: Summe aller Bytes vom C-Feld bis zum letzten Nutzdatenbyte (mod 256) */
	for (i = 0; i < len; i++) sum = (uint8_t)(sum + pFrame[4 + i]);
	if (sum != pFrame[4 + len]) return AMIS_ERR_CHECKSUM;

	return AMIS_OK;
}

AmisResult AmisParseTelegram(const uint8_t *pBuffer, int len)
{
	char                 manu[4];
	uint8_t              medium;
	uint16_t	         configuration;
	unsigned char        iv[AES_KEYLEN];
	MBusDataHeader_Amis	 telegram;
	uint32_t             dataSize = 0;
	AmisDevice			 *pDevice = &deviceAmis;
	const uint8_t        *pData;

	if (pBuffer == NULL || len < (int)sizeof(telegram)) return AMIS_ERR_LENGTH;

	memset(iv, 0x00, sizeof(iv));
	memset(&telegram, 0x00, sizeof(telegram));

	memcpy(&telegram, pBuffer, sizeof(telegram));

	configuration = (telegram.configuration & 0x0F00) >> 8;
	dataSize = ((telegram.configuration & 0x00F0) >> 4) * 16;

	/* Die laut Header angegebene Datenmenge muss im Telegramm auch vorhanden sein. */
	if (dataSize == 0 || dataSize > (uint32_t)(len - (int)sizeof(telegram))) return AMIS_ERR_LENGTH;
	if (dataSize > sizeof(pDevice->device.decodedData)) return AMIS_ERR_LENGTH;

	if (configuration != 0x00 && configuration != 0x04 && configuration != 0x05) return AMIS_ERR_CONFIG;

	manu[2] = (telegram.manuId1 & 0x001F) + 64;
	manu[1] = ((telegram.manuId1 & 0x03E0) >> 5) + 64;
	manu[0] = ((telegram.manuId1 & 0xFC00) >> 10) + 64;
	manu[3] = 0x00;

	snprintf(pDevice->device.identNr,sizeof(pDevice->device.identNr),"%02x%02x%02x%02x", telegram.identNr[3], telegram.identNr[2], telegram.identNr[1], telegram.identNr[0]);
	strncpy(pDevice->device.manufacturerId, manu, sizeof(pDevice->device.manufacturerId));

	medium = telegram.deviceType;
	if (medium > 0x40) medium = 0x40;
	pDevice->device.medium = medium;

	pDevice->device.dataSizeAes = dataSize;
	pDevice->device.configuration = configuration;

	if (configuration == 0x05)
	{
		int index = 0;
		/*************/
		/* Create IV */
		/*************/
		memset(iv, 0x00, sizeof(iv));
		memcpy(&iv[index], &telegram.manuId1, sizeof(telegram.manuId1));
		index += sizeof(telegram.manuId1);
		memcpy(&iv[index], telegram.identNr, sizeof(telegram.identNr));
		index += sizeof(telegram.identNr);
		memcpy(&iv[index], &telegram.version, sizeof(telegram.version));
		index += sizeof(telegram.version);
		memcpy(&iv[index], &telegram.deviceType, sizeof(telegram.deviceType));
		index += sizeof(telegram.deviceType);

		/* Fill IV with access number */
		memset(&iv[index], telegram.accessNr, sizeof(iv) - index);

		memcpy(pDevice->device.iv, iv, sizeof(pDevice->device.iv));
	}

	/*  Decrypt (dataSize ist durch die Berechnung immer ein Vielfaches von 16) */
	pData = &pBuffer[sizeof(telegram)];

	memset(pDevice->device.decodedData, 0x00, sizeof(pDevice->device.decodedData));
	memcpy(pDevice->device.decodedData, pData, dataSize);

	if (configuration != 0x00)
	{
		mbedtls_aes_crypt_cbc(&pDevice->device.aesContext, MBEDTLS_AES_DECRYPT, dataSize, iv, pDevice->device.decodedData, pDevice->device.decodedData);
	}

	if (pDevice->device.decodedData[0] != 0x2F || pDevice->device.decodedData[1] != 0x2F)
	{
		return AMIS_ERR_DECRYPT;
	}

	pDevice->device.dataSize = dataSize;
	AmisParseMBus();
	return AMIS_OK;
}
