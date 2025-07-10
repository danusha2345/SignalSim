//----------------------------------------------------------------------
// FNavBit.h:
//   Implementation of navigation bit synthesis class for F/NAV
//
//          Copyright (C) 2020-2029 by Jun Mo, All rights reserved.
//
//----------------------------------------------------------------------

#include <memory.h>
#include "ConstVal.h"
#include "FNavBit.h"

// Count number of set bits in a byte
static int Count1(unsigned char n)
{
	int count = 0;
	while (n > 0)
	{
		n &= (n - 1);
		count++;
	}
	return (count & 1);
}


#define SQRT_A0 5440.588203494177338011974948823
#define NORMINAL_I0 0.97738438111682456307726683035362

const int FNavBit::SyncPattern[12] = {
	1, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0
};

FNavBit::FNavBit()
{
	memset(GalEphData, 0, sizeof(GalEphData));
	memset(GalAlmData, 0, sizeof(GalAlmData));
	memset(GalUtcData, 0, sizeof(GalUtcData));
	memset(GalIonoData, 0, sizeof(GalIonoData));
}

FNavBit::~FNavBit()
{
}

int FNavBit::GetFrameData(GNSS_TIME StartTime, int svid, int Param, int *NavBits)
{
	int i, j, TOW, subframe, page;
	unsigned int EncodeData[7], GST, CrcResult;
	int UncodedBits[268];
	int EncodedSymbols[536];
	unsigned char ConvState;

	Param;	// not used
	// first determine the current TOW and subframe number
	StartTime.Week += StartTime.MilliSeconds / 604800000;
	StartTime.MilliSeconds %= 604800000;
	TOW = StartTime.MilliSeconds / 1000;
	GST = (((StartTime.Week - 1024) & 0xfff) << 20) + TOW;
	subframe = (TOW % 1200) / 50;	// two round of 600s frame (24 subframes) to hold 36 almanacs
	page = (TOW % 50) / 10;
	GetPageData(svid, page, subframe, GST, EncodeData);
	CrcResult = Crc24qEncode(EncodeData, 244);

	// Place message bits and CRC into a single array (244 + 24 = 268 bits)
	for (i = 0; i < 244; i++)
		UncodedBits[i] = (EncodeData[i / 32] >> (31 - (i % 32))) & 1;
	for (i = 0; i < 24; i++)
		UncodedBits[244 + i] = (CrcResult >> (23 - i)) & 1;

	// FEC (Forward Error Correction) Rate 1/2 Convolutional Encoder for F/NAV
	// G1 = 1110101b (0x75), G2 = 1011011b (0x5B)
	ConvState = 0;
	for (i = 0; i < 268; i++)
	{
		ConvState = (ConvState << 1) | UncodedBits[i];
		EncodedSymbols[i * 2] = Count1(ConvState & 0x75);
		EncodedSymbols[i * 2 + 1] = Count1(ConvState & 0x5B);
	}

	// Add 12-bit sync pattern
	for (i = 0; i < 12; i++)
		NavBits[i] = SyncPattern[i];

	// Block interleaving (8 rows, 67 columns)
	for (i = 0; i < 536; i++)
		NavBits[i + 12] = EncodedSymbols[(i % 8) * 67 + (i / 8)];

	return 0;
}

int FNavBit::SetEphemeris(int svid, PGPS_EPHEMERIS Eph)
{
	if (svid < 1 || svid > 36 || !Eph || !Eph->valid)
		return 0;
	ComposeEphWords(Eph, GalEphData[svid-1]);
	return svid;
}

int FNavBit::SetAlmanac(GPS_ALMANAC Alm[])
{
	int i, week = 0;

	for (i = 0; i < 36; i ++)
		if (Alm[i].valid & 1)
		{
			week = Alm[i].week;
			break;
		}
	for (i = 0; i < 12; i ++)
		ComposeAlmWords(Alm + i * 3, GalAlmData[i], week);
	return 0;
}

int FNavBit::SetIonoUtc(PIONO_PARAM IonoParam, PUTC_PARAM UtcParam)
{
	PIONO_NEQUICK IonoGal = (PIONO_NEQUICK)IonoParam;
	signed int IntValue;
	unsigned int UintValue;

	// put ionosphere parameters
	UintValue = UnscaleUint(IonoGal->ai0, -2);
	GalIonoData[0] = COMPOSE_BITS(UintValue, 5, 11);
	IntValue = UnscaleInt(IonoGal->ai1, -8);
	GalIonoData[0] |= COMPOSE_BITS(IntValue >> 6, 0, 5);
	GalIonoData[1] = COMPOSE_BITS(IntValue, 26, 6);
	IntValue = UnscaleInt(IonoGal->ai2, -15);
	GalIonoData[1] |= COMPOSE_BITS(IntValue, 12, 14);
	GalIonoData[1] |= COMPOSE_BITS(IonoGal->flag, 7, 5);

	// put UTC parameters
	IntValue = UnscaleInt(UtcParam->A0, -30);
	GalUtcData[0] = COMPOSE_BITS(IntValue >> 26, 0, 6);
	GalUtcData[1] = COMPOSE_BITS(IntValue, 6, 26);
	IntValue = UnscaleInt(UtcParam->A1, -50);
	GalUtcData[1] |= COMPOSE_BITS(IntValue >> 18, 0, 6);
	GalUtcData[2] = COMPOSE_BITS(IntValue, 14, 18);
	GalUtcData[2] |= COMPOSE_BITS(UtcParam->TLS, 6, 8);
	GalUtcData[2] |= COMPOSE_BITS(UtcParam->tot >> 2, 0, 6);
	GalUtcData[3] = COMPOSE_BITS(UtcParam->tot, 30, 2);
	GalUtcData[3] |= COMPOSE_BITS(UtcParam->WN, 22, 8);
	GalUtcData[3] |= COMPOSE_BITS(UtcParam->WNLSF, 14, 8);
	GalUtcData[3] |= COMPOSE_BITS(UtcParam->DN, 11, 3);
	GalUtcData[3] |= COMPOSE_BITS(UtcParam->TLSF, 3, 8);

	return 0;
}

int FNavBit::ComposeEphWords(PGPS_EPHEMERIS Ephemeris, unsigned int EphData[4][7])
{
	signed int IntValue;
	unsigned int UintValue;
	
	// Initialize all data to zero
	memset(EphData, 0, sizeof(unsigned int) * 4 * 7);

	// PageType 1
	// Note: TOW will be added in GetPageData
	// Bits layout (after 10-bit shift in GetFrameData):
	// [0] bits 21-16: Page type (6 bits)
	// [0] bits 15-0: Reserved for TOW (will be filled in GetPageData)
	EphData[0][0] = COMPOSE_BITS(1, 16, 6);  // Page type = 1
	// [1] bits 31-28: Reserved for TOW (4 bits)
	// [1] bits 27-26: Spare (2 bits) 
	// [1] bits 25-16: IODnav (10 bits)
	// [1] bits 15-2: t0c (14 bits)
	UintValue = Ephemeris->toc / 60;
	EphData[0][1] = COMPOSE_BITS(Ephemeris->iodc, 16, 10) | COMPOSE_BITS(UintValue, 2, 14);
	// [1] bits 1-0: af2 MSB (2 bits)
	// [2] bits 31-28: af2 remaining (4 bits)
	IntValue = UnscaleInt(Ephemeris->af2, -59);
	EphData[0][1] |= COMPOSE_BITS(IntValue >> 4, 0, 2);
	EphData[0][2] = COMPOSE_BITS(IntValue, 28, 4);
	// [2] bits 27-7: af1 (21 bits)
	IntValue = UnscaleInt(Ephemeris->af1, -46);
	EphData[0][2] |= COMPOSE_BITS(IntValue, 7, 21);
	// [2] bits 6-0: Spare (7 bits) + [3] bit 31: Spare (1 bit) = 8 bits spare total
	// [3] bits 30-29: E5a HS (2 bits)
	// [3] bits 28-0: af0 MSB (29 bits)
	EphData[0][3] = COMPOSE_BITS(Ephemeris->health & 0x3, 29, 2);  // E5a HS
	IntValue = UnscaleInt(Ephemeris->af0, -34);
	EphData[0][3] |= COMPOSE_BITS(IntValue >> 2, 0, 29);
	// [4] bits 31-30: af0 LSB (2 bits)
	// [4] bits 29-19: ai0 (11 bits) - iono correction
	// [4] bits 18-8: ai1 (11 bits) - iono correction  
	// [4] bits 7-0: ai2 MSB (8 bits) - iono correction
	EphData[0][4] = COMPOSE_BITS(IntValue, 30, 2);
	// Iono parameters will be added in GetPageData
	
	// [5] bits 31-26: ai2 LSB (6 bits) - iono correction
	// [5] bits 25-21: Region flags (5 bits) - iono correction
	// [5] bits 20-11: BGD E5a/E1 (10 bits)
	// [5] bit 10: E5a DVS (1 bit)
	// [5] bits 9-5: Spare (5 bits)
	// [5] bits 4-0: GST-WN MSB (5 bits) - will be added in GetPageData
	IntValue = UnscaleInt(Ephemeris->tgd, -32);
	EphData[0][5] = COMPOSE_BITS(IntValue, 11, 10) | COMPOSE_BITS((Ephemeris->health >> 2) & 0x1, 10, 1);
	// [6] bits 31-5: GST-WN remaining + GST-TOW (27 bits) - will be added in GetPageData
	// [6] bits 4-0: Spare (5 bits)
	EphData[0][6] = 0;  // GST will be added in GetPageData

	// PageType 2
	EphData[1][0] = (2 << 16) | COMPOSE_BITS(Ephemeris->iodc, 6, 10);
	IntValue = UnscaleInt(Ephemeris->M0 / PI, -31);
	EphData[1][0] |= COMPOSE_BITS(IntValue >> 26, 0, 6);
	EphData[1][1] = COMPOSE_BITS(IntValue, 6, 26);
	IntValue = UnscaleInt(Ephemeris->omega_dot / PI, -43);
	EphData[1][1] |= COMPOSE_BITS(IntValue >> 18, 0, 6);
	EphData[1][2] = COMPOSE_BITS(IntValue, 14, 18);
	UintValue = UnscaleUint(Ephemeris->ecc, -33);
	EphData[1][2] |= COMPOSE_BITS(UintValue >> 18, 0, 14);
	EphData[1][3] = COMPOSE_BITS(UintValue, 14, 18);
	UintValue = UnscaleUint(Ephemeris->sqrtA, -19);
	EphData[1][3] |= COMPOSE_BITS(UintValue >> 18, 0, 14);
	EphData[1][4] = COMPOSE_BITS(UintValue, 14, 18);
	IntValue = UnscaleInt(Ephemeris->omega0 / PI, -31);
	EphData[1][4] |= COMPOSE_BITS(IntValue >> 18, 0, 14);
	EphData[1][5] = COMPOSE_BITS(IntValue, 14, 18);
	IntValue = UnscaleInt(Ephemeris->idot / PI, -43);
	EphData[1][5] |= COMPOSE_BITS(IntValue, 0, 14);
	EphData[1][6] = 0;	// for GST

	// PageType 3
	EphData[2][0] = (3 << 16) | COMPOSE_BITS(Ephemeris->iodc, 6, 10);
	IntValue = UnscaleInt(Ephemeris->i0 / PI, -31);
	EphData[2][0] |= COMPOSE_BITS(IntValue >> 26, 0, 6);
	EphData[2][1] = COMPOSE_BITS(IntValue, 6, 26);
	IntValue = UnscaleInt(Ephemeris->w / PI, -31);
	EphData[2][1] |= COMPOSE_BITS(IntValue >> 26, 0, 6);
	EphData[2][2] = COMPOSE_BITS(IntValue, 6, 26);
	IntValue = UnscaleInt(Ephemeris->delta_n / PI, -43);
	EphData[2][2] |= COMPOSE_BITS(IntValue >> 10, 0, 6);
	EphData[2][3] = COMPOSE_BITS(IntValue, 22, 10);
	IntValue = UnscaleInt(Ephemeris->cuc, -29);
	EphData[2][3] |= COMPOSE_BITS(IntValue, 6, 16);
	IntValue = UnscaleInt(Ephemeris->cus, -29);
	EphData[2][3] |= COMPOSE_BITS(IntValue >> 10, 0, 6);
	EphData[2][4] = COMPOSE_BITS(IntValue, 22, 10);
	IntValue = UnscaleInt(Ephemeris->crc, -5);
	EphData[2][4] |= COMPOSE_BITS(IntValue, 6, 16);
	IntValue = UnscaleInt(Ephemeris->crs, -5);
	EphData[2][4] |= COMPOSE_BITS(IntValue >> 10, 0, 6);
	EphData[2][5] = COMPOSE_BITS(IntValue, 22, 10);
	EphData[2][5] |= COMPOSE_BITS((Ephemeris->toe / 60), 8, 14);
	EphData[2][6] = 0;	// for GST and 8 spare bits

	// PageType 4
	EphData[3][0] = (4 << 16) | COMPOSE_BITS(Ephemeris->iodc, 6, 10);
	IntValue = UnscaleInt(Ephemeris->cic, -29);
	EphData[3][0] |= COMPOSE_BITS(IntValue >> 10, 0, 6);
	EphData[3][1] = COMPOSE_BITS(IntValue, 22, 10);
	IntValue = UnscaleInt(Ephemeris->cis, -29);
	EphData[3][1] |= COMPOSE_BITS(IntValue, 6, 16);
	EphData[3][2] = EphData[3][3] = EphData[3][4] = EphData[3][5] = EphData[3][6] = 0;	// for GST-UTC, GST-GPS, TOW and 5 spare bits

	return 0;
}

int FNavBit::ComposeAlmWords(GPS_ALMANAC Almanac[], unsigned int AlmData[2][7], int week)
{
	signed int IntValue;
	unsigned int UintValue;
	int toa = (Almanac[0].valid & 1) ? Almanac[0].toa : (Almanac[1].valid & 1) ? Almanac[1].toa : (Almanac[2].valid & 1) ? Almanac[2].toa : 0;
	
	// Initialize all data to zero
	memset(AlmData, 0, sizeof(unsigned int) * 2 * 7);

	// PageType 5
	// Note: this page type doesn't need TOW as it's not in the first 4 page types
	AlmData[0][0] = COMPOSE_BITS(5, 16, 6) | COMPOSE_BITS(4, 12, 4) | COMPOSE_BITS(week, 10, 2) | COMPOSE_BITS((toa / 600), 0, 10);	// Type=5, IODa=4
	AlmData[0][1] = COMPOSE_BITS(Almanac[0].svid, 26, 6);	// SVID1 starts here
	IntValue = UnscaleInt(Almanac[0].sqrtA - SQRT_A0, -11);
	AlmData[0][1] |= COMPOSE_BITS(IntValue, 13, 13);
	UintValue = UnscaleUint(Almanac[0].ecc, -16);
	AlmData[0][1] |= COMPOSE_BITS(UintValue, 2, 11);
	IntValue = UnscaleInt(Almanac[0].w / PI, -15);
	AlmData[0][1] |= COMPOSE_BITS(IntValue >> 14, 0, 2);
	AlmData[0][2] = COMPOSE_BITS(IntValue, 18, 14);
	IntValue = UnscaleInt((Almanac[0].i0 - NORMINAL_I0) / PI, -14);
	AlmData[0][2] |= COMPOSE_BITS(IntValue, 7, 11);
	IntValue = UnscaleInt(Almanac[0].omega0 / PI, -15);
	AlmData[0][2] |= COMPOSE_BITS(IntValue >> 9, 0, 7);
	AlmData[0][3] = COMPOSE_BITS(IntValue, 23, 9);
	IntValue = UnscaleInt(Almanac[0].omega_dot / PI, -33);
	AlmData[0][3] |= COMPOSE_BITS(IntValue, 12, 11);
	IntValue = UnscaleInt(Almanac[0].M0 / PI, -15);
	AlmData[0][3] |= COMPOSE_BITS(IntValue >> 4, 0, 12);
	AlmData[0][4] = COMPOSE_BITS(IntValue, 28, 4);
	IntValue = UnscaleInt(Almanac[0].af0, -19);
	AlmData[0][4] |= COMPOSE_BITS(IntValue, 12, 16);
	IntValue = UnscaleInt(Almanac[0].af1, -38);
	AlmData[0][4] |= COMPOSE_BITS(IntValue >> 1, 0, 12);
	AlmData[0][5] = COMPOSE_BITS(IntValue, 31, 1);
	AlmData[0][5] |= COMPOSE_BITS((Almanac[1].valid & 1) ? 0 : 1, 29, 2);
	AlmData[0][5] |= COMPOSE_BITS(Almanac[1].svid, 23, 6);	// SVID2 starts here
	IntValue = UnscaleInt(Almanac[1].sqrtA - SQRT_A0, -11);
	AlmData[0][5] |= COMPOSE_BITS(IntValue, 10, 13);
	UintValue = UnscaleUint(Almanac[1].ecc, -16);
	AlmData[0][5] |= COMPOSE_BITS(UintValue >> 1, 0, 10);
	AlmData[0][6] = COMPOSE_BITS(IntValue, 31, 1);
	IntValue = UnscaleInt(Almanac[1].w / PI, -15);
	AlmData[0][6] |= COMPOSE_BITS(IntValue, 15, 16);
	IntValue = UnscaleInt((Almanac[1].i0 - NORMINAL_I0) / PI, -14);
	AlmData[0][6] |= COMPOSE_BITS(IntValue, 4, 11);
	IntValue = UnscaleInt(Almanac[1].omega0 / PI, -15);
	AlmData[0][6] |= COMPOSE_BITS(IntValue >> 12, 0, 4);

	// PageType 6
	AlmData[1][0] = COMPOSE_BITS(6, 16, 6) | COMPOSE_BITS(4, 12, 4);	// Type=6, IODa=4
	AlmData[1][0] |= COMPOSE_BITS(IntValue, 0, 12);
	IntValue = UnscaleInt(Almanac[1].omega_dot / PI, -33);
	AlmData[1][1] = COMPOSE_BITS(IntValue, 21, 11);
	IntValue = UnscaleInt(Almanac[1].M0 / PI, -15);
	AlmData[1][1] |= COMPOSE_BITS(IntValue, 5, 16);
	IntValue = UnscaleInt(Almanac[1].af0, -19);
	AlmData[1][1] |= COMPOSE_BITS(IntValue >> 11, 0, 5);
	AlmData[1][2] = COMPOSE_BITS(IntValue, 21, 11);
	IntValue = UnscaleInt(Almanac[1].af1, -38);
	AlmData[1][2] |= COMPOSE_BITS(IntValue, 8, 13);
	AlmData[1][2] |= COMPOSE_BITS((Almanac[1].valid & 1) ? 0 : 1, 6, 2);
	AlmData[1][2] |= COMPOSE_BITS(Almanac[2].svid, 0, 6);	// SVID3 starts here
	IntValue = UnscaleInt(Almanac[2].sqrtA - SQRT_A0, -11);
	AlmData[1][3] = COMPOSE_BITS(IntValue, 19, 13);
	UintValue = UnscaleUint(Almanac[2].ecc, -16);
	AlmData[1][3] |= COMPOSE_BITS(UintValue, 8, 11);
	IntValue = UnscaleInt(Almanac[2].w / PI, -15);
	AlmData[1][3] |= COMPOSE_BITS(IntValue >> 8, 0, 8);
	AlmData[1][4] = COMPOSE_BITS(IntValue, 24, 8);
	IntValue = UnscaleInt((Almanac[2].i0 - NORMINAL_I0) / PI, -14);
	AlmData[1][4] |= COMPOSE_BITS(IntValue, 13, 11);
	IntValue = UnscaleInt(Almanac[2].omega0 / PI, -15);
	AlmData[1][4] |= COMPOSE_BITS(IntValue >> 3, 0, 13);
	AlmData[1][5] = COMPOSE_BITS(IntValue, 29, 3);
	IntValue = UnscaleInt(Almanac[2].omega_dot / PI, -33);
	AlmData[1][5] |= COMPOSE_BITS(IntValue, 18, 11);
	IntValue = UnscaleInt(Almanac[2].M0 / PI, -15);
	AlmData[1][5] |= COMPOSE_BITS(IntValue, 2, 16);
	IntValue = UnscaleInt(Almanac[2].af0, -19);
	AlmData[1][5] |= COMPOSE_BITS(IntValue >> 14, 0, 2);
	AlmData[1][6] = COMPOSE_BITS(IntValue, 18, 14);
	IntValue = UnscaleInt(Almanac[2].af1, -38);
	AlmData[1][6] |= COMPOSE_BITS(IntValue, 5, 13);
	AlmData[1][6] |= COMPOSE_BITS((Almanac[2].valid & 1) ? 0 : 1, 3, 2);

	return 0;
}

void FNavBit::GetPageData(int svid, int page, int subframe, unsigned int GST, unsigned int Data[7])
{
	switch (page)
	{
	case 0:	// page 1
		{
			unsigned int TOW = GST & 0xFFFFF;  // Extract 20-bit TOW
			memcpy(Data, GalEphData[svid-1][0], sizeof(unsigned int) * 7);
			// Add TOW (20 bits)
			Data[0] |= COMPOSE_BITS(TOW >> 4, 0, 16);  // TOW upper 16 bits
			Data[1] |= COMPOSE_BITS(TOW, 28, 4);       // TOW lower 4 bits
			// add iono correction
			Data[3] |= GalIonoData[0];
			Data[4] |= GalIonoData[1];
			// add GST (32 bits)
			Data[5] |= COMPOSE_BITS(GST >> 27, 0, 5);   // GST upper 5 bits
			Data[6] |= COMPOSE_BITS(GST, 5, 27);        // GST lower 27 bits
		}
		break;
	case 1:	// page 2
		memcpy(Data, GalEphData[svid-1][1], sizeof(unsigned int) * 7);
		Data[6] = GST;
		break;
	case 2:	// page 3
		memcpy(Data, GalEphData[svid-1][2], sizeof(unsigned int) * 7);
		// add GST
		Data[5] |= COMPOSE_BITS(GST >> 24, 0, 8);
		Data[6] |= COMPOSE_BITS(GST, 8, 24);
		break;
	case 3:	// page 4
		memcpy(Data, GalEphData[svid-1][3], sizeof(unsigned int) * 7);
		// add GST-UTC
		Data[1] |= GalUtcData[0];
		Data[2] = GalUtcData[1];
		Data[3] = GalUtcData[2];
		Data[4] = GalUtcData[3];
		// add TOW
		Data[6] |= COMPOSE_BITS(GST, 5, 20);
		break;
	case 4:	// page 5/6
		memcpy(Data, GalAlmData[subframe/2][subframe&1], sizeof(unsigned int) * 7);
		break;
	}
}


