//----------------------------------------------------------------------
// BCNavBit.h:
//   Implementation of navigation bit synthesis class for BDS3 navigation bit
//
//          Copyright (C) 2020-2029 by Jun Mo, All rights reserved.
//
//----------------------------------------------------------------------

#include <memory.h>
#include "ConstVal.h"
#include "BCNavBit.h"
#include <cstdio>

const unsigned int BCNavBit::crc24q[256] = {
	0x00000000u, 0x864CFB00u, 0x8AD50D00u, 0x0C99F600u, 0x93E6E100u, 0x15AA1A00u, 0x1933EC00u, 0x9F7F1700u, 
	0xA1813900u, 0x27CDC200u, 0x2B543400u, 0xAD18CF00u, 0x3267D800u, 0xB42B2300u, 0xB8B2D500u, 0x3EFE2E00u, 
	0xC54E8900u, 0x43027200u, 0x4F9B8400u, 0xC9D77F00u, 0x56A86800u, 0xD0E49300u, 0xDC7D6500u, 0x5A319E00u, 
	0x64CFB000u, 0xE2834B00u, 0xEE1ABD00u, 0x68564600u, 0xF7295100u, 0x7165AA00u, 0x7DFC5C00u, 0xFBB0A700u, 
	0x0CD1E900u, 0x8A9D1200u, 0x8604E400u, 0x00481F00u, 0x9F370800u, 0x197BF300u, 0x15E20500u, 0x93AEFE00u, 
	0xAD50D000u, 0x2B1C2B00u, 0x2785DD00u, 0xA1C92600u, 0x3EB63100u, 0xB8FACA00u, 0xB4633C00u, 0x322FC700u, 
	0xC99F6000u, 0x4FD39B00u, 0x434A6D00u, 0xC5069600u, 0x5A798100u, 0xDC357A00u, 0xD0AC8C00u, 0x56E07700u, 
	0x681E5900u, 0xEE52A200u, 0xE2CB5400u, 0x6487AF00u, 0xFBF8B800u, 0x7DB44300u, 0x712DB500u, 0xF7614E00u, 
	0x19A3D200u, 0x9FEF2900u, 0x9376DF00u, 0x153A2400u, 0x8A453300u, 0x0C09C800u, 0x00903E00u, 0x86DCC500u, 
	0xB822EB00u, 0x3E6E1000u, 0x32F7E600u, 0xB4BB1D00u, 0x2BC40A00u, 0xAD88F100u, 0xA1110700u, 0x275DFC00u, 
	0xDCED5B00u, 0x5AA1A000u, 0x56385600u, 0xD074AD00u, 0x4F0BBA00u, 0xC9474100u, 0xC5DEB700u, 0x43924C00u, 
	0x7D6C6200u, 0xFB209900u, 0xF7B96F00u, 0x71F59400u, 0xEE8A8300u, 0x68C67800u, 0x645F8E00u, 0xE2137500u, 
	0x15723B00u, 0x933EC000u, 0x9FA73600u, 0x19EBCD00u, 0x8694DA00u, 0x00D82100u, 0x0C41D700u, 0x8A0D2C00u, 
	0xB4F30200u, 0x32BFF900u, 0x3E260F00u, 0xB86AF400u, 0x2715E300u, 0xA1591800u, 0xADC0EE00u, 0x2B8C1500u, 
	0xD03CB200u, 0x56704900u, 0x5AE9BF00u, 0xDCA54400u, 0x43DA5300u, 0xC596A800u, 0xC90F5E00u, 0x4F43A500u, 
	0x71BD8B00u, 0xF7F17000u, 0xFB688600u, 0x7D247D00u, 0xE25B6A00u, 0x64179100u, 0x688E6700u, 0xEEC29C00u, 
	0x3347A400u, 0xB50B5F00u, 0xB992A900u, 0x3FDE5200u, 0xA0A14500u, 0x26EDBE00u, 0x2A744800u, 0xAC38B300u, 
	0x92C69D00u, 0x148A6600u, 0x18139000u, 0x9E5F6B00u, 0x01207C00u, 0x876C8700u, 0x8BF57100u, 0x0DB98A00u, 
	0xF6092D00u, 0x7045D600u, 0x7CDC2000u, 0xFA90DB00u, 0x65EFCC00u, 0xE3A33700u, 0xEF3AC100u, 0x69763A00u, 
	0x57881400u, 0xD1C4EF00u, 0xDD5D1900u, 0x5B11E200u, 0xC46EF500u, 0x42220E00u, 0x4EBBF800u, 0xC8F70300u, 
	0x3F964D00u, 0xB9DAB600u, 0xB5434000u, 0x330FBB00u, 0xAC70AC00u, 0x2A3C5700u, 0x26A5A100u, 0xA0E95A00u, 
	0x9E177400u, 0x185B8F00u, 0x14C27900u, 0x928E8200u, 0x0DF19500u, 0x8BBD6E00u, 0x87249800u, 0x01686300u, 
	0xFAD8C400u, 0x7C943F00u, 0x700DC900u, 0xF6413200u, 0x693E2500u, 0xEF72DE00u, 0xE3EB2800u, 0x65A7D300u, 
	0x5B59FD00u, 0xDD150600u, 0xD18CF000u, 0x57C00B00u, 0xC8BF1C00u, 0x4EF3E700u, 0x426A1100u, 0xC426EA00u, 
	0x2AE47600u, 0xACA88D00u, 0xA0317B00u, 0x267D8000u, 0xB9029700u, 0x3F4E6C00u, 0x33D79A00u, 0xB59B6100u, 
	0x8B654F00u, 0x0D29B400u, 0x01B04200u, 0x87FCB900u, 0x1883AE00u, 0x9ECF5500u, 0x9256A300u, 0x141A5800u, 
	0xEFAAFF00u, 0x69E60400u, 0x657FF200u, 0xE3330900u, 0x7C4C1E00u, 0xFA00E500u, 0xF6991300u, 0x70D5E800u, 
	0x4E2BC600u, 0xC8673D00u, 0xC4FECB00u, 0x42B23000u, 0xDDCD2700u, 0x5B81DC00u, 0x57182A00u, 0xD154D100u, 
	0x26359F00u, 0xA0796400u, 0xACE09200u, 0x2AAC6900u, 0xB5D37E00u, 0x339F8500u, 0x3F067300u, 0xB94A8800u, 
	0x87B4A600u, 0x01F85D00u, 0x0D61AB00u, 0x8B2D5000u, 0x14524700u, 0x921EBC00u, 0x9E874A00u, 0x18CBB100u, 
	0xE37B1600u, 0x6537ED00u, 0x69AE1B00u, 0xEFE2E000u, 0x709DF700u, 0xF6D10C00u, 0xFA48FA00u, 0x7C040100u, 
	0x42FA2F00u, 0xC4B6D400u, 0xC82F2200u, 0x4E63D900u, 0xD11CCE00u, 0x57503500u, 0x5BC9C300u, 0xDD853800u
};

const unsigned int BCNavBit::e2v_table[128] = {
 1,  2,  4,  8, 16, 32,  3,  6, 12, 24, 48, 35,  5, 10, 20, 40,
19, 38, 15, 30, 60, 59, 53, 41, 17, 34,  7, 14, 28, 56, 51, 37, 
 9, 18, 36, 11, 22, 44, 27, 54, 47, 29, 58, 55, 45, 25, 50, 39, 
13, 26, 52, 43, 21, 42, 23, 46, 31, 62, 63, 61, 57, 49, 33, 
 1,  2,  4,  8, 16, 32,  3,  6, 12, 24, 48, 35,  5, 10, 20, 40,
19, 38, 15, 30, 60, 59, 53, 41, 17, 34,  7, 14, 28, 56, 51, 37, 
 9, 18, 36, 11, 22, 44, 27, 54, 47, 29, 58, 55, 45, 25, 50, 39, 
13, 26, 52, 43, 21, 42, 23, 46, 31, 62, 63, 61, 57, 49, 33, 
};

const unsigned int BCNavBit::v2e_table[64] = {
 0,  0,  1,  6,  2, 12,  7, 26,  3, 32, 13, 35,  8, 48, 27, 18, 
 4, 24, 33, 16, 14, 52, 36, 54,  9, 45, 49, 38, 28, 41, 19, 56, 
 5, 62, 25, 11, 34, 31, 17, 47, 15, 23, 53, 51, 37, 44, 55, 40, 
10, 61, 46, 30, 50, 22, 39, 43, 29, 60, 42, 21, 20, 59, 57, 58, 
};

BCNavBit::BCNavBit()
{
	memset(Ephemeris1, 0, sizeof(Ephemeris1));
	memset(Ephemeris2, 0, sizeof(Ephemeris2));
	memset(ClockParam, 0, sizeof(ClockParam));
	memset(IntegrityFlags, 0, sizeof(IntegrityFlags));
	memset(ReducedAlmanac, 0, sizeof(ReducedAlmanac));
	memset(MidiAlmanac, 0, sizeof(MidiAlmanac));
	memset(BdGimIono, 0, sizeof(BdGimIono));
	memset(BdtUtcParam, 0, sizeof(BdtUtcParam));
	memset(EopParam, 0, sizeof(EopParam));
	memset(BgtoParam, 0, sizeof(BgtoParam));
	AlmanacWeek = 0;
	AlmanacToa = 0;
}

BCNavBit::~BCNavBit()
{
}

int BCNavBit::SetEphemeris(int svid, PGPS_EPHEMERIS Eph)
{
	unsigned int *Data;
	signed int IntValue;
	unsigned int UintValue;
	long long int LongValue;
	unsigned long long int ULongValue;

	if (svid < 1 || svid > 63 || !Eph || !Eph->valid)
		return 0;
	if ((Eph->toe % 300) != 0)	// BCNAV ephemeris requires toe be multiple of 300
		return 0;

	// fill in Ephemeris1
	Data = Ephemeris1[svid-1];
	Data[0] = COMPOSE_BITS(Eph->iode, 16, 8);
	UintValue = Eph->toe / 300;	// toe
	Data[0] |= COMPOSE_BITS(UintValue, 5, 11);
	UintValue = Eph->flag;	// SatType
	Data[0] |= COMPOSE_BITS(UintValue, 3, 2);
	IntValue = UnscaleInt(Eph->axis - ((UintValue == 3) ? 27906100.0 : 42162200.0), -9);	// deltaA
	Data[0] |= COMPOSE_BITS(IntValue >> 23, 0, 3);
	Data[1] = COMPOSE_BITS(IntValue, 1, 23);
	IntValue = UnscaleInt(Eph->axis_dot, -21);	// Adot
	Data[1] |= COMPOSE_BITS(IntValue >> 24, 0, 1);
	Data[2] = COMPOSE_BITS(IntValue, 0, 24);
	IntValue = UnscaleInt(Eph->delta_n / PI, -44);	// delta_n
	Data[3] = COMPOSE_BITS(IntValue, 7, 17);
	IntValue = UnscaleInt(Eph->delta_n_dot, -57);	// delta n dot
	Data[3] |= COMPOSE_BITS(IntValue >> 16, 0, 7);
	Data[4] = COMPOSE_BITS(IntValue, 8, 16);
	LongValue = UnscaleLong(Eph->M0 / PI, -32);
	IntValue = (LongValue & 0x100000000LL) ? 1 : 0;
	UintValue = (unsigned int)LongValue;
	Data[4] |= COMPOSE_BITS(IntValue, 7, 1);
	Data[4] |= COMPOSE_BITS(UintValue >> 25, 0, 7);
	Data[5] = COMPOSE_BITS(UintValue >> 1, 0, 24);
	Data[6] = COMPOSE_BITS(UintValue, 23, 1);
	ULongValue = UnscaleULong(Eph->ecc, -34);
	IntValue = (ULongValue & 0x100000000LL) ? 1 : 0;
	UintValue = (unsigned int)ULongValue;
	Data[6] |= COMPOSE_BITS(IntValue, 22, 1);
	Data[6] |= COMPOSE_BITS(UintValue >> 10, 0, 22);
	Data[7] = COMPOSE_BITS(UintValue, 14, 10);
	LongValue = UnscaleLong(Eph->w / PI, -32);
	IntValue = (LongValue & 0x100000000LL) ? 1 : 0;
	UintValue = (unsigned int)LongValue;
	Data[7] |= COMPOSE_BITS(IntValue, 13, 1);
	Data[7] |= COMPOSE_BITS(UintValue >> 19, 0, 13);
	Data[8] = COMPOSE_BITS(UintValue, 5, 19);

	// fill in Ephemeris2
	Data = Ephemeris2[svid-1];
	LongValue = UnscaleLong(Eph->omega0 / PI, -32);
	IntValue = (LongValue & 0x100000000LL) ? 1 : 0;
	UintValue = (unsigned int)LongValue;
	Data[0] = COMPOSE_BITS(IntValue, 23, 1);
	Data[0] |= COMPOSE_BITS(UintValue >> 9, 0, 23);
	Data[1] = COMPOSE_BITS(UintValue, 15, 9);
	LongValue = UnscaleLong(Eph->i0 / PI, -32);
	IntValue = (LongValue & 0x100000000LL) ? 1 : 0;
	UintValue = (unsigned int)LongValue;
	Data[1] |= COMPOSE_BITS(IntValue, 14, 1);
	Data[1] |= COMPOSE_BITS(UintValue >> 18, 0, 14);
	Data[2] = COMPOSE_BITS(UintValue, 6, 18);
	IntValue = UnscaleInt(Eph->omega_dot / PI, -44);	// omega dot
	Data[2] |= COMPOSE_BITS(IntValue >> 13, 0, 6);
	Data[3] = COMPOSE_BITS(IntValue, 11, 13);
	IntValue = UnscaleInt(Eph->idot / PI, -44);	// i dot
	Data[3] |= COMPOSE_BITS(IntValue >> 4, 0, 11);
	Data[4] = COMPOSE_BITS(IntValue, 20, 4);
	IntValue = UnscaleInt(Eph->cis, -30);	// cis
	Data[4] |= COMPOSE_BITS(IntValue, 4, 16);
	IntValue = UnscaleInt(Eph->cic, -30);	// cic
	Data[4] |= COMPOSE_BITS(IntValue >> 12, 0, 4);
	Data[5] = COMPOSE_BITS(IntValue, 12, 12);
	IntValue = UnscaleInt(Eph->crs, -8);	// crs
	Data[5] |= COMPOSE_BITS(IntValue >> 12, 0, 12);
	Data[6] = COMPOSE_BITS(IntValue, 12, 12);
	IntValue = UnscaleInt(Eph->crc, -8);	// crc
	Data[6] |= COMPOSE_BITS(IntValue >> 12, 0, 12);
	Data[7] = COMPOSE_BITS(IntValue, 12, 12);
	IntValue = UnscaleInt(Eph->cus, -30);	// cus
	Data[7] |= COMPOSE_BITS(IntValue >> 9, 0, 12);
	Data[8] = COMPOSE_BITS(IntValue, 15, 9);
	IntValue = UnscaleInt(Eph->cuc, -30);	// cuc
	Data[8] |= COMPOSE_BITS(IntValue >> 6, 0, 15);
	Data[9] = COMPOSE_BITS(IntValue, 18, 6);

	// fill in ClockParam (with 17 zeros)
	Data = ClockParam[svid-1];
	UintValue = Eph->toc / 300;	// toc
	Data[0] = COMPOSE_BITS(UintValue, 13, 11);
	IntValue = UnscaleInt(Eph->af0, -34);	// af0
	Data[0] |= COMPOSE_BITS(IntValue >> 12, 0, 13);
	Data[1] = COMPOSE_BITS(IntValue, 12, 12);
	IntValue = UnscaleInt(Eph->af1, -50);	// af1
	Data[1] |= COMPOSE_BITS(IntValue >> 10, 0, 12);
	Data[2] = COMPOSE_BITS(IntValue, 14, 10);
	IntValue = UnscaleInt(Eph->af2, -66);	// af2
	Data[2] |= COMPOSE_BITS(IntValue, 3, 11);
	Data[3] |= COMPOSE_BITS(Eph->iodc, 0, 10);

	// fill in IntegrityFlags
	IntegrityFlags[svid-1] = Eph->flag >> 2;

	// fill in TGD and ISC
	Data = TgsIscParam[svid-1];
	IntValue = UnscaleInt(Eph->tgd_ext[0] - Eph->tgd_ext[1], -34);	// ISC_B1C
	Data[0] = COMPOSE_BITS(IntValue, 12, 12);
	IntValue = UnscaleInt(Eph->tgd_ext[1], -34);	// TGD_B1C
	Data[0] |= COMPOSE_BITS(IntValue, 0, 12);
	IntValue = UnscaleInt(Eph->tgd_ext[2] - Eph->tgd_ext[3], -34);	// ISC_B2a
	Data[1] = COMPOSE_BITS(IntValue, 12, 12);
	IntValue = UnscaleInt(Eph->tgd_ext[1], -34);	// TGD_B2a
	Data[1] |= COMPOSE_BITS(IntValue, 0, 12);
	IntValue = UnscaleInt(Eph->tgd_ext[4], -34);	// TGD_B2b
	Data[2] = COMPOSE_BITS(IntValue, 0, 12);

	return svid;
}

int BCNavBit::SetAlmanac(GPS_ALMANAC Alm[])
{
	int i;

	// fill in almanac page
	for (i = 0; i < 63; i ++)
	{
		FillBdsAlmanacPage(&Alm[i], MidiAlmanac[i], ReducedAlmanac[i]);
		if (Alm[i].valid & 1)
		{
			AlmanacToa = Alm[i].toa >> 12;
			AlmanacWeek = Alm[i].week;
		}
	}

	return 0;
}

// put Length bits in 24bit WORD Src into 24bit WORD Dest starting from StartBit with MSB to LSB order
int BCNavBit::AppendWord(unsigned int *Dest, int StartBit, unsigned int *Src, int Length)
{
	int RemainBits = 24;
	int FillBits;

	while (Length > 0)
	{
		FillBits = 24 - StartBit;
		FillBits = (FillBits <= RemainBits) ? FillBits : RemainBits;
		FillBits = (FillBits <= Length) ? FillBits : Length;
		*Dest = ((StartBit == 0) ? 0 : (*Dest)) | COMPOSE_BITS(*Src >> (RemainBits - FillBits), (24 - StartBit - FillBits), FillBits);
		if ((StartBit += FillBits) >= 24)
		{
			StartBit = 0;
			Dest ++;
		}
		if ((RemainBits -= FillBits) <= 0)
		{
			RemainBits = 24;
			Src ++;
		}
		Length -= FillBits;
	}

	return StartBit;
}

// put bit in Data from MSB ot LSB into BitStream, bit order from bit(BitNumber-1) to bit(0) of Data
int BCNavBit::AssignBits(unsigned int Data, int BitNumber, int BitStream[])
{
	int i;

	Data <<= (32 - BitNumber);
	for (i = 0; i < BitNumber; i ++)
	{
		BitStream[i] = (Data & 0x80000000) ? 1 : 0;
		Data <<= 1;
	}

	return BitNumber;
}

// Append CRC to the end of data stream, Length is the size of DataStream (24bit data in each DWORD) including CRC bits
int BCNavBit::AppendCRC(unsigned int DataStream[], int Length)
{
	int i;
	unsigned int Data, crc_result = 0;

	for (i = 0; i < Length - 1; i ++)
	{
		Data = DataStream[i] << 8;	// move data to MSB
		crc_result = (crc_result << 8) ^ crc24q[(Data >> 24) ^ (unsigned char)(crc_result >> 16)];
		Data <<= 8;
		crc_result = (crc_result << 8) ^ crc24q[(Data >> 24) ^ (unsigned char)(crc_result >> 16)];
		Data <<= 8;
		crc_result = (crc_result << 8) ^ crc24q[(Data >> 24) ^ (unsigned char)(crc_result >> 16)];
		Data <<= 8;
	}
	DataStream[i] = (crc_result & 0xffffff);

	return 0;
}

int BCNavBit::LDPCEncode(int SymbolStream[], int SymbolLength, const char *MatrixGen)
{
	int i, j;
	int *Parity;
	const char *p1 = MatrixGen;
	int *p2, sum;

	// Check for NULL pointer
	if (!MatrixGen || !SymbolStream) {
		return -1;
	}

	Parity = SymbolStream + SymbolLength;
	for (i = 0; i < SymbolLength; i ++)
	{
		sum = 0;
		p2 = SymbolStream;
		for (j = 0; j < SymbolLength; j ++)
		{
			// Check if we're still within the matrix bounds
			if (*p1 == '\0') {
				return -1;
			}
			sum ^= GF6IntMul((int)(*p1)-'0', *p2);
			p1 ++; p2 ++;
		}
		*Parity ++ = sum;
	}

	return 0;
}

int BCNavBit::GF6IntMul(int a, int b)
{
	if (a && b)
		return e2v_table[v2e_table[a] + v2e_table[b]];
	else
		return 0;
}

int BCNavBit::FillBdsAlmanacPage(PGPS_ALMANAC Almanac, unsigned int MidiAlm[8], unsigned int ReducedAlm[2])
{
	signed int IntValue;
	unsigned int UintValue;

	if (!(Almanac->valid & 1))	// almanac not valid
	{
		MidiAlm[0] = ReducedAlm[0] = 0;	// set PRN field to 0
		return 0;
	}

	// fill midi almanac
	MidiAlm[0] = COMPOSE_BITS(Almanac->svid, 18, 6);	// PRN
	MidiAlm[0] |= COMPOSE_BITS(Almanac->flag, 16, 2);	// SatType
	MidiAlm[0] |= COMPOSE_BITS(Almanac->week, 3, 13);	// WN
	UintValue = Almanac->toa >> 12;	// toa
	MidiAlm[0] |= COMPOSE_BITS(UintValue >> 5, 0, 3);
	MidiAlm[1] = COMPOSE_BITS(UintValue, 19, 5);
	UintValue = UnscaleUint(Almanac->ecc, -16);	// ecc
	MidiAlm[1] |= COMPOSE_BITS(UintValue, 8, 11);
	IntValue = UnscaleInt(Almanac->i0 / PI - ((Almanac->flag == 1) ? 0 : 0.3), -14);	// delta_i
	MidiAlm[1] |= COMPOSE_BITS(IntValue >> 3, 0, 8);
	MidiAlm[2] = COMPOSE_BITS(IntValue, 21, 3);
	UintValue = UnscaleUint(Almanac->sqrtA, -4);	// sqrtA
	MidiAlm[2] |= COMPOSE_BITS(UintValue, 4, 17);
	IntValue = UnscaleInt(Almanac->omega0 / PI, -15);	// omega0
	MidiAlm[2] |= COMPOSE_BITS(IntValue >> 12, 0, 4);
	MidiAlm[3] = COMPOSE_BITS(IntValue, 12, 12);
	IntValue = UnscaleInt(Almanac->omega_dot / PI, -33);	// omega_dot
	MidiAlm[3] |= COMPOSE_BITS(IntValue, 1, 11);
	IntValue = UnscaleInt(Almanac->w / PI, -15);	// w
	MidiAlm[3] |= COMPOSE_BITS(IntValue >> 15, 0, 1);
	MidiAlm[4] = COMPOSE_BITS(IntValue, 9, 15);
	IntValue = UnscaleInt(Almanac->M0 / PI, -15);	// M0
	MidiAlm[4] |= COMPOSE_BITS(IntValue >> 7, 0, 9);
	MidiAlm[5] = COMPOSE_BITS(IntValue, 17, 7);
	IntValue = UnscaleInt(Almanac->af0, -20);	// af0
	MidiAlm[5] |= COMPOSE_BITS(IntValue, 6, 11);
	IntValue = UnscaleInt(Almanac->af1, -37);	// af1
	MidiAlm[5] |= COMPOSE_BITS(IntValue >> 4, 0, 6);
	MidiAlm[6] = COMPOSE_BITS(IntValue, 20, 4);
	MidiAlm[6] |= COMPOSE_BITS(Almanac->health, 12, 8);	// use B1I health as clock health

	// fill reduced almanac
	ReducedAlm[0] = COMPOSE_BITS(Almanac->svid, 18, 6);	// PRN
	ReducedAlm[0] |= COMPOSE_BITS(Almanac->flag, 16, 2);	// SatType
	IntValue = UnscaleInt(Almanac->sqrtA * Almanac->sqrtA - ((Almanac->flag == 3) ? 27906100.0 : 42162200.0), 9);	// deltaA
	ReducedAlm[0] |= COMPOSE_BITS(IntValue, 8, 8);
	IntValue = UnscaleInt(Almanac->omega0 / PI, -6);	// omega0
	ReducedAlm[0] |= COMPOSE_BITS(IntValue, 1, 7);
	IntValue = UnscaleInt((Almanac->M0 + Almanac->w) / PI, -6);	// Phi0
	ReducedAlm[0] |= COMPOSE_BITS(IntValue >> 6, 0, 1);
	ReducedAlm[1] = COMPOSE_BITS(IntValue, 18, 6);
	ReducedAlm[1] |= COMPOSE_BITS(Almanac->health, 10, 8);	// use B1I health as clock health

	return 0;
}

int BCNavBit::SetIonoUtc(PIONO_PARAM IonoParam, PUTC_PARAM UtcParam)
{
	// Default implementation - does nothing
	// Derived classes (like BCNav1Bit) can override this
	return 0;
}
