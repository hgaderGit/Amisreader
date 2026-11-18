/*
 * parser.h
 *
 *  Created on: 20.04.2026
 *      Author: HgaderGit
 */

#ifndef PARSER_H_
#define PARSER_H_

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "mbedtls/esp_config.h"
#include "mbedtls/aes.h"
#include "parserTypes.h"

#define AES_KEYLEN	16
#define MAX_TARIFS_REGISTERS	5   /* SUM, T1 -T4 */

/* Ergebnis der Telegrammpruefung bzw. des Parsens */
typedef enum
{
	AMIS_OK = 0,
	AMIS_ERR_FRAME,      /* Rahmenformat: Start-/Stoppbyte, Laengenwiederholung, Gesamtlaenge */
	AMIS_ERR_CHECKSUM,   /* M-Bus Pruefsumme stimmt nicht */
	AMIS_ERR_LENGTH,     /* Nutzdatenlaenge passt nicht zum Header */
	AMIS_ERR_CONFIG,     /* unbekannte Verschluesselungskonfiguration */
	AMIS_ERR_DECRYPT     /* 0x2F2F-Pruefung nach Entschluesselung fehlgeschlagen (falscher Key?) */
}AmisResult;

typedef struct
{
	mbedtls_aes_context aesContext;
	uint8_t        		decodedData[256];
	char				output[1536];
	uint32_t       		dataSize;
	uint8_t				aesKey[AES_KEYLEN];
	uint8_t				iv[AES_KEYLEN];
	uint8_t				configuration;
	uint8_t				dataSizeAes;
	time_t				timeStamp;

	/* Zaehlerzeit als Rastergrundlage fuer die Viertelstunden-Berechnung */
	int64_t				localSeconds;          /* Sekunden seit 1970-01-01 00:00 nach Zaehleruhr (ohne Zeitzone/Sommerzeit) */
	int32_t				monthId;               /* Jahr * 12 + (Monat - 1) der Zaehlerzeit */
	char				dateTime[20];          /* "JJJJ-MM-TT hh:mm:ss" */
	uint8_t				meterTimeValid;        /* 1 = Zeitfelder im aktuellen Telegramm plausibel */
	uint8_t				meterTimeFlagInvalid;  /* 1 = Zaehler markiert die Zeit selbst als ungueltig */

	char           		manufacturerId[4];
	char           		identNr[9];
	uint8_t   			medium;
}Device;

typedef struct
{
	uint8_t dif;
	uint8_t vifPrimary;
	uint8_t vifExtension;
	uint8_t vifCombinable;
	uint8_t vifCombinableExtension;
	int tarif;
	int subunit;
	int storage;
}MbusDifVif;

typedef struct
{
	char       value[32];
	uint8_t    valid;      /* 1 = Wert stammt aus dem zuletzt erfolgreich geparsten Telegramm */
	MbusDifVif mbus;
}DeviceDataPoint;

typedef struct
{
	Device  	 		   device;
	DeviceDataPoint    time;
	DeviceDataPoint    date;
	DeviceDataPoint    m18x[MAX_TARIFS_REGISTERS];
	DeviceDataPoint    m28x[MAX_TARIFS_REGISTERS];
	DeviceDataPoint    m38x[MAX_TARIFS_REGISTERS];
	DeviceDataPoint    m48x[MAX_TARIFS_REGISTERS];
	DeviceDataPoint    m170;
	DeviceDataPoint    m270;
	DeviceDataPoint    m370;
	DeviceDataPoint    m470;
	DeviceDataPoint    m01280;
	DeviceDataPoint    m1570;

	uint8_t			   countHandled;

	DeviceDataPoint    unknown;
	uint8_t			   unknownDifVif;
}AmisDevice;

AmisDevice* AmisInitDevice(const uint8_t* pAesKey);

/* Prueft einen kompletten M-Bus Langframe "68 L L 68 ... CS 16" (size = L + 6). */
AmisResult AmisCheckFrame(const uint8_t *pFrame, size_t size);

/*
 * pBuffer zeigt auf das erste Byte nach "68 L L 68" (C-Feld), len ist das
 * Laengenbyte L des Frames. Bei Rueckgabe != AMIS_OK sind die Datenpunkte
 * NICHT aktualisiert und duerfen nicht veroeffentlicht werden.
 */
AmisResult AmisParseTelegram(const uint8_t *pBuffer, int len);

const char* AmisResultText(AmisResult result);

#endif /* PARSER_H_ */
