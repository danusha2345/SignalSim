//----------------------------------------------------------------------
// LNavBit.h:
//   Implementation of navigation bit synthesis class for LNAV
//
//          Copyright (C) 2020-2029 by Jun Mo, All rights reserved.
//
//----------------------------------------------------------------------

#include <math.h>
#include <memory.h>
#include "ConstVal.h"
#include "GNavBit.h"

const unsigned int GNavBit::CheckSumTable[8][3] = {
{ 0x000aaaab, 0x55555556, 0xaaad5b01, },
{ 0x000ccccd, 0x9999999b, 0x33366d02, },
{ 0x0000f0f1, 0xe1e1e1e3, 0xc3c78e04, },
{ 0x0000ff01, 0xfe01fe03, 0xfc07f008, },
{ 0x000f0001, 0xfffe0003, 0xfff80010, },
{ 0x00000001, 0xfffffffc, 0x00000020, },
{ 0x000ffffe, 0x00000000, 0x00000040, },
{ 0x000fffff, 0xffffffff, 0xffffffff, },
};

GNavBit::GNavBit()
{
	memset(StringEph, 0x0, sizeof(StringEph));
	memset(StringAlm, 0x0, sizeof(StringAlm));
}

GNavBit::~GNavBit()
{
}

int GNavBit::GetFrameData(GNSS_TIME StartTime, int svid, int Param, int *NavBits)
{
	int i, string, frame, bit;
	unsigned int* data, Stream[3];

	if (svid < 1 || svid > 24)
		return 1;

	string = StartTime.MilliSeconds / 2000;
	frame = (string / 15) % 2880;	// frame within one day
	string %= 15;

	if (string < 4)	// Ephemeris string
		data = StringEph[svid - 1][string];
	else
		data = StringAlm[frame % 5][string - 4];

	// left shift 8bit for check sum space and add word m as string number
	Stream[0] = (data[0] & 0xfffff);
	Stream[1] = data[1];
	Stream[2] = data[2] & 0xffffff00;
	if (string == 0)	// first string, tk already in data[0]
	{
		// tk is already composed in ComposeStringEph, no need to overwrite
	}
	else if (string >= 4)
		Stream[0] |= (string + 1) << 16;

	// add check sum and put 85 bits to NavBits position 85~169 (leave first 85 places for meander code expansion)
	Stream[2] |= CheckSum(Stream);
	AssignBits(Stream[0], 21, NavBits + 85);
	AssignBits(Stream[1], 32, NavBits + 106);
	AssignBits(Stream[2], 32, NavBits + 138);
	// do relative coding
	bit = 0;	// first bit of the string is always 0
	for (i = 86; i < 170; i++)
		bit = NavBits[i] = (bit ^ NavBits[i]);
	// expand to meander code
	for (i = 0; i < 85; i++)
	{
		NavBits[i * 2] = NavBits[i + 85];
		NavBits[i * 2 + 1] = 1 - NavBits[i + 85];
	}
	// append time mark
	AssignBits(0x3e375096, 30, NavBits + 170);

	return 0;
}

int GNavBit::SetEphemeris(int svid, PGPS_EPHEMERIS Eph)
{
	PGLONASS_EPHEMERIS GloEph = (PGLONASS_EPHEMERIS)Eph;
	if (svid < 1 || svid > 24 || !GloEph || !GloEph->flag)
		return 0;
	ComposeStringEph(GloEph, StringEph[svid-1]);
	return svid;
}

int GNavBit::SetAlmanac(GPS_ALMANAC Alm[])
{
	PGLONASS_ALMANAC GloAlm = (PGLONASS_ALMANAC)Alm;
	int i, frame, string;

	for (i = 0; i < 24; i ++)
		if (GloAlm->flag == 1)
			break;
	if (i == 24)
		return 0;

	// compose string 5 for each frame (t_c and t_GPS set to 0)
	StringAlm[0][0][0] = StringAlm[1][0][0] = StringAlm[2][0][0] = StringAlm[3][0][0] = StringAlm[4][0][0] = (5 << 16) | COMPOSE_BITS(GloAlm[i].day, 5, 11);
	StringAlm[0][0][1] = StringAlm[1][0][1] = StringAlm[2][0][1] = StringAlm[3][0][1] = StringAlm[4][0][1] = COMPOSE_BITS(GloAlm[i].leap_year >> 1, 0, 4);
	StringAlm[0][0][2] = StringAlm[1][0][2] = StringAlm[2][0][2] = StringAlm[3][0][2] = StringAlm[4][0][2] = ((GloAlm[i].leap_year & 1) << 31) | 0x100;
	for (i = 0; i < 24; i ++)
	{
		frame = i / 5;
		string = (i % 5) * 2 + 6;
		ComposeStringAlm((PGLONASS_ALMANAC)Alm + i, i + 1, StringAlm[frame][string-5], StringAlm[frame][string-4]);
		StringAlm[frame][string - 5][0] |= ((string << 16) | ((i + 1) << 8));
		StringAlm[frame][string - 4][0] |= ((string + 1) << 16);
	}

	return 0;
}

int GNavBit::SetIonoUtc(PIONO_PARAM IonoParam, PUTC_PARAM UtcParam)
{
	return 0;
}

int GNavBit::ComposeStringEph(PGLONASS_EPHEMERIS Ephemeris, unsigned int String[][3])
{
	unsigned int UintValue;

	// string 1
	String[0][0] = (1 << 16);  // m = 1 (string number)
	// Bits 5-6: empty
	String[0][0] |= COMPOSE_BITS(0, 14, 2);  // bits 5-6 empty
	// P1 in bits 7-8 (P1 is in bits 0-1 of P field)
	String[0][0] |= COMPOSE_BITS(Ephemeris->P & 0x3, 12, 2);  // P1 (bits 7-8)
	// tk in bits 9-20 (12 bits: 5 hours, 6 minutes, 1 thirty-second interval)
	String[0][0] |= COMPOSE_BITS(Ephemeris->tk, 0, 12);  // tk (bits 9-20)
	// Coordinate X (bits 21-44, 24 bits total with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->x) / 1000, -11); 
	UintValue |= (Ephemeris->x < 0) ? (1 << 23) : 0;  // sign bit for 24-bit value
	// X entirely in word[1]: bits 21-44 map to positions 31-8 of word[1]
	String[0][1] = COMPOSE_BITS(UintValue, 8, 24);  // bits 21-44 (all 24 bits)
	
	// Velocity X (bits 45-49, 5 bits with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->vx) / 1000, -20); 
	UintValue |= (Ephemeris->vx < 0) ? (1 << 4) : 0;  // sign bit for 5-bit value
	// vx: bits 45-49 map to positions 7-3 of word[1]
	String[0][1] |= COMPOSE_BITS(UintValue, 3, 5);  // bits 45-49
	
	// Acceleration X (bits 50-76, 27 bits with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->ax) / 1000, -30); 
	UintValue |= (Ephemeris->ax < 0) ? (1 << 26) : 0;  // sign bit for 27-bit value
	// ax split: bits 50-52 in word[1] positions 2-0, bits 53-76 in word[2] positions 31-8
	String[0][1] |= COMPOSE_BITS(UintValue >> 24, 0, 3);  // bits 50-52 (3 MSB)
	String[0][2] = COMPOSE_BITS(UintValue, 8, 24);  // bits 53-76 (24 LSB)
	
	// Note: Bits 77-84 are for Hamming code (КХ) - handled by CheckSum function
	// Bit 85 is time mark (МВ) - handled separately
	// string 2
	String[1][0] = (2 << 16);  // m = 2 (string number)
	// Bn (bits 5-7, 3 bits) - unreliability flag
	String[1][0] |= COMPOSE_BITS(Ephemeris->Bn, 13, 3);  // Bn (bits 5-7)
	// P2 (bit 8) - satellite mode flag (P2 is in bit 2 of P field)
	String[1][0] |= COMPOSE_BITS((Ephemeris->P >> 2) & 1, 12, 1);  // P2 (bit 8)
	// tb (bits 9-15, 7 bits) - time index within day in 15-minute intervals
	UintValue = Ephemeris->tb / 900;  // tb in 15-minute intervals
	String[1][0] |= COMPOSE_BITS(UintValue, 5, 7);  // tb (bits 9-15)
	// Bits 16-20: empty
	String[1][0] |= COMPOSE_BITS(0, 0, 5);  // bits 16-20 empty
	
	// Coordinate Y (bits 21-44, 24 bits total with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->y) / 1000, -11); 
	UintValue |= (Ephemeris->y < 0) ? (1 << 23) : 0;  // sign bit for 24-bit value
	// Y entirely in word[1]: bits 21-44 map to positions 31-8 of word[1]
	String[1][1] = COMPOSE_BITS(UintValue, 8, 24);  // bits 21-44 (all 24 bits)
	
	// Velocity Y (bits 45-49, 5 bits with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->vy) / 1000, -20); 
	UintValue |= (Ephemeris->vy < 0) ? (1 << 4) : 0;  // sign bit for 5-bit value
	// vy: bits 45-49 map to positions 7-3 of word[1]
	String[1][1] |= COMPOSE_BITS(UintValue, 3, 5);  // bits 45-49
	
	// Acceleration Y (bits 50-76, 27 bits with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->ay) / 1000, -30); 
	UintValue |= (Ephemeris->ay < 0) ? (1 << 26) : 0;  // sign bit for 27-bit value
	// ay split: bits 50-52 in word[1] positions 2-0, bits 53-76 in word[2] positions 31-8
	String[1][1] |= COMPOSE_BITS(UintValue >> 24, 0, 3);  // bits 50-52 (3 MSB)
	String[1][2] = COMPOSE_BITS(UintValue, 8, 24);  // bits 53-76 (24 LSB)
	// string 3
	String[2][0] = (3 << 16);  // m = 3 (string number)
	// P3 (bit 5) - satellite time flag
	String[2][0] |= COMPOSE_BITS((Ephemeris->P >> 3) & 1, 15, 1);  // P3 (bit 5)
	// gamma (bits 6-16, 11 bits with sign) - relative frequency deviation
	UintValue = UnscaleUint(fabs(Ephemeris->gamma), -40); 
	UintValue |= (Ephemeris->gamma < 0) ? (1 << 10) : 0;  // sign bit for 11-bit value
	String[2][0] |= COMPOSE_BITS(UintValue, 4, 11);  // gamma (bits 6-16)
	// Reserved (bit 17)
	String[2][0] |= COMPOSE_BITS(0, 3, 1);  // bit 17 reserved
	// p (bits 18-19, 2 bits) - GLONASS-M flag
	String[2][0] |= COMPOSE_BITS(Ephemeris->M & 0x3, 1, 2);  // p (bits 18-19)
	// ln (bit 20) - health flag (extracted from bit 5 of P field)
	String[2][0] |= COMPOSE_BITS((Ephemeris->P >> 5) & 1, 0, 1);  // ln (bit 20)
	
	// Coordinate Z (bits 21-44, 24 bits total with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->z) / 1000, -11); 
	UintValue |= (Ephemeris->z < 0) ? (1 << 23) : 0;  // sign bit for 24-bit value
	// Z entirely in word[1]: bits 21-44 map to positions 31-8 of word[1]
	String[2][1] = COMPOSE_BITS(UintValue, 8, 24);  // bits 21-44 (all 24 bits)
	
	// Velocity Z (bits 45-49, 5 bits with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->vz) / 1000, -20); 
	UintValue |= (Ephemeris->vz < 0) ? (1 << 4) : 0;  // sign bit for 5-bit value
	// vz: bits 45-49 map to positions 7-3 of word[1]
	String[2][1] |= COMPOSE_BITS(UintValue, 3, 5);  // bits 45-49
	
	// Acceleration Z (bits 50-76, 27 bits with sign)
	UintValue = UnscaleUint(fabs(Ephemeris->az) / 1000, -30); 
	UintValue |= (Ephemeris->az < 0) ? (1 << 26) : 0;  // sign bit for 27-bit value
	// az split: bits 50-52 in word[1] positions 2-0, bits 53-76 in word[2] positions 31-8
	String[2][1] |= COMPOSE_BITS(UintValue >> 24, 0, 3);  // bits 50-52 (3 MSB)
	String[2][2] = COMPOSE_BITS(UintValue, 8, 24);  // bits 53-76 (24 LSB)
	// string 4
	String[3][0] = (4 << 16);  // m = 4 (string number)
	// tau_n (bits 5-26, 22 bits with sign) - satellite time scale correction
	UintValue = UnscaleUint(fabs(Ephemeris->tn), -30); 
	UintValue |= (Ephemeris->tn < 0) ? (1 << 21) : 0;  // sign bit for 22-bit value
	// tau_n split: 16 bits in word[0], 6 bits in word[1]
	String[3][0] |= COMPOSE_BITS(UintValue >> 6, 0, 16);  // bits 5-20 (16 MSB)
	String[3][1] = COMPOSE_BITS(UintValue, 26, 6);  // bits 21-26 (6 LSB)
	
	// delta_tau_n (bits 27-31, 5 bits with sign) - tn and tc scale difference
	UintValue = UnscaleUint(fabs(Ephemeris->dtn), -30); 
	UintValue |= (Ephemeris->dtn < 0) ? (1 << 4) : 0;  // sign bit for 5-bit value
	String[3][1] |= COMPOSE_BITS(UintValue, 21, 5);  // bits 27-31
	
	// En (bits 32-36, 5 bits) - age of operational information
	String[3][1] |= COMPOSE_BITS(Ephemeris->En, 16, 5);  // bits 32-36
	
	// Reserved (bits 37-50, 14 bits)
	String[3][1] |= COMPOSE_BITS(0, 2, 14);  // bits 37-50 reserved
	
	// P4 (bit 51) - ephemeris update flag
	String[3][1] |= COMPOSE_BITS((Ephemeris->P >> 4) & 1, 1, 1);  // P4 (bit 51)
	
	// FT (bits 52-55, 4 bits) - predicted frequency correction
	String[3][1] |= COMPOSE_BITS(Ephemeris->Ft & 0xF, 0, 4);  // bits 52-55 (positions 3-0)
	String[3][2] = 0;  // Start with clean word[2]
	
	// Bits 56-58: empty (bits 56-58 map to word[2] positions 28-26)
	String[3][2] |= COMPOSE_BITS(0, 26, 3);  // bits 56-58 empty
	
	// NT (bits 59-69, 11 bits) - day number within four-year period
	// bits 59-69 map to word[2] positions 25-15
	String[3][2] |= COMPOSE_BITS(Ephemeris->day, 15, 11);  // bits 59-69
	
	// n (bits 70-74, 5 bits) - satellite number in system  
	// bits 70-74 map to word[2] positions 14-10
	String[3][2] |= COMPOSE_BITS(Ephemeris->n, 10, 5);  // bits 70-74
	
	// M (bits 75-76, 2 bits) - satellite type (01-M, 10-K1, 11-K2)
	// bits 75-76 map to word[2] positions 9-8
	String[3][2] |= COMPOSE_BITS(Ephemeris->M, 8, 2);  // bits 75-76
	
	// Note: Bits 77-84 are for Hamming code (КХ) - handled by CheckSum function
	// Bit 85 is time mark (МВ) - handled separately

	return 0;
}

int GNavBit::ComposeStringAlm(PGLONASS_ALMANAC Almanac, int slot, unsigned int StreamEven[3], unsigned int StreamOdd[3])
{
	unsigned int UintValue;

	if (!Almanac->flag)
	{
		StreamEven[2] = StreamEven[1] = StreamEven[0] = StreamOdd[2] = StreamOdd[1] = StreamOdd[0] = 0;
		return 0;
	}
	// first string
	StreamEven[0] = COMPOSE_BITS(Almanac->flag, 15, 1);
	StreamEven[0] |= COMPOSE_BITS(1, 13, 2);		// Mn=01 for GLONASS-M
	StreamEven[0] |= COMPOSE_BITS(slot, 8, 5);
	UintValue = UnscaleInt(fabs(Almanac->clock_error), -18); UintValue |= (Almanac->clock_error < 0) ? (1 << 9) : 0;
	StreamEven[0] |= COMPOSE_BITS((UintValue >> 2), 0, 8);
	StreamEven[1] = COMPOSE_BITS(UintValue, 30, 2);
	UintValue = UnscaleUint(fabs(Almanac->lambda), -20); UintValue |= (Almanac->lambda < 0) ? (1 << 20) : 0;
	StreamEven[1] |= COMPOSE_BITS(UintValue, 9, 21);
	UintValue = UnscaleUint(fabs(Almanac->di), -20); UintValue |= (Almanac->di < 0) ? (1 << 17) : 0;
	StreamEven[1] |= COMPOSE_BITS(UintValue >> 9, 0, 9);
	StreamEven[2] = COMPOSE_BITS(UintValue, 23, 9);
	UintValue = UnscaleUint(Almanac->ecc, -20);
	StreamEven[2] |= COMPOSE_BITS(UintValue, 8, 15);
	// second string
	UintValue = UnscaleUint(fabs(Almanac->w), -15); UintValue |= (Almanac->w < 0) ? (1 << 15) : 0;
	StreamOdd[0] = COMPOSE_BITS(UintValue, 0, 16);
	UintValue = UnscaleUint(fabs(Almanac->t), -5);
	StreamOdd[1] = COMPOSE_BITS(UintValue, 11, 21);
	UintValue = UnscaleUint(fabs(Almanac->dt), -9); UintValue |= (Almanac->dt < 0) ? (1 << 21) : 0;
	StreamOdd[1] |= COMPOSE_BITS(UintValue >> 11, 0, 11);
	StreamOdd[2] = COMPOSE_BITS(UintValue, 21, 11);
	UintValue = UnscaleUint(fabs(Almanac->dt_dot), -14); UintValue |= (Almanac->dt < 0) ? (1 << 6) : 0;
	StreamOdd[2] |= COMPOSE_BITS(UintValue, 14, 7);
	StreamOdd[2] |= COMPOSE_BITS(Almanac->freq, 9, 5);
	return 0;
}

// calculate check sum on data
// the data bit number from 84 to 1 placed are bit19~0 in data[0]
// then bit31~0 in data[1] and bit31~0 in data[2]
// put the hamming code bits in 8 LSB in data[2] will return 0 if check success
// or put 0 in 8 LSB of data[0] will return humming code bits
unsigned int GNavBit::CheckSum(unsigned int Data[3])
{
	unsigned int CheckSumValue = 0;
	unsigned int xor_value;
	int i;

	Data[2] &= ~0xff;	// clear checksum bits (8 bits for GLONASS)
	// calculate parity value
	for (i = 0; i < 8; i ++)
	{
		xor_value = (Data[0] & CheckSumTable[i][0]) ^ (Data[1] & CheckSumTable[i][1]) ^ ((Data[2] | CheckSumValue) & CheckSumTable[i][2]);
		// calculate bit 1 number
		xor_value = (xor_value & 0x55555555) + ((xor_value & 0xaaaaaaaa) >> 1);
		xor_value = (xor_value & 0x33333333) + ((xor_value & 0xcccccccc) >> 2);
		xor_value = (xor_value & 0x0f0f0f0f) + ((xor_value & 0xf0f0f0f0) >> 4);
		xor_value = (xor_value & 0x000f000f) + ((xor_value & 0x0f000f00) >> 8);
		xor_value = (xor_value & 0x0000000f) + ((xor_value & 0x000f0000) >> 16);
		CheckSumValue |= ((xor_value & 1) << i);
	}

	return CheckSumValue;
}
