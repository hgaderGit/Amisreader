/*
 * parserTypes.h
 *
 *  Created on: 20.04.2026
 *      Author: HgaderGit
 */

#ifndef PARSERTYPES_H_
#define PARSERTYPES_H_

#include <stdint.h>

#define MBUS_MAX_TARIF		3


#pragma pack(push,1)
/*
 * Beginnt beim ersten Byte nach dem Frame-Kopf "68 L L 68", also beim
 * C-Feld. Die ersten drei Bytes sind laut M-Bus: C-Feld, A-Feld (Adresse)
 * und CI-Feld. Die Namen "length"/"cField"/"ciField" sind historisch.
 */
typedef struct
{
	uint8_t	   length;
	uint8_t    cField;
	uint8_t    ciField;
	uint8_t    identNr[4];
	uint16_t   manuId1;
	uint8_t    version;
	uint8_t    deviceType;
	uint8_t    accessNr;
	uint8_t    status;
	uint16_t   configuration;

}MBusDataHeader_Amis;
#pragma pack(pop)




#pragma pack(push,1)
typedef struct DIF_T
{
	unsigned char     dataField     : 4;
	unsigned char     functionField : 2;
	unsigned char     lsbStorage    : 1;
	unsigned char     extension     : 1;
}DIF;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct DIFE_t
{
	unsigned char     storageNumber : 4;
	unsigned char     tarif         : 2;
	unsigned char     subUnit       : 1;
	unsigned char     extension     : 1;
}DIFE;
#pragma pack(pop)



#pragma pack(push,1)
/* Type G = Year down to day: EN 13757-3:2004 */
typedef struct
{
	unsigned char     day : 5; //  < 1 to 31 > /    0: every day
	unsigned char     yearLsb : 3; //
	unsigned char     month : 4; //  <1 to 12 >  /   15: every month
	unsigned char     yearMsb : 4; //  <0 to 99 >  /  127: every year
} TTimeTypeG;
#pragma pack(pop)


#pragma pack(push,1)
/* Type I = Year down to second: EN 13757-3:2004 */
typedef struct
{
	unsigned char     second : 6; //  second counter [0 .. 59]
	unsigned char     daylightSaving : 1; //  1 = yes(summer time) 0 = no
	unsigned char     leapYear : 1; //  1 = leap year  0 = standard year

	unsigned char     minute : 6; // minute counter [0 .. 59]
	unsigned char     daylightSavingDev : 1; // 1 = +    0 = -
	unsigned char     invalid : 1; //  0 = valid; 1 = invalid

	unsigned char     hour : 5; // hour counter   [0 .. 23]
	unsigned char     dayOfWeek : 3; // 1 = Monday    7 = Sunday

	unsigned char     day : 5; // day counter    [0 .. 31]
	unsigned char     year_lsb : 3; // 3 lsb's of year

	unsigned char     month : 4; // month counter  [0 .. 12]
	unsigned char     year_msb : 4; // 4 msb's of year

	unsigned char     week : 6; // [1..53]
	unsigned char     daylightSavingDur : 2; // 0 = no daylight saving
} TTimeTypeI;
#pragma pack(pop)

#pragma pack(push,1)
/* Type F = Year down to minute: EN 13757-3:2004 */
typedef struct
{
	unsigned char	minute : 6;
	unsigned char	res1 : 1;
	unsigned char   invalid : 1; /* 0: Valid          1: invalid    */

	unsigned char   hour : 5;
	unsigned char	hundredYears : 2;
	unsigned char	summerTime : 1; /* 0: standard time  1: summer time */

	unsigned char   day : 5;
	unsigned char	yearLsb : 3;

	unsigned char	month : 4;
	unsigned char	yearMsb : 4; /*  year = 1900 + 100*hundredYears + ((yearMsb<<3)+yearLsb ) */
}TTimeTypeF;
#pragma pack(pop)


#endif /* PARSERTYPES_H_ */
