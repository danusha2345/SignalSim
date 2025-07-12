//----------------------------------------------------------------------
// L5CNavBit.cpp:
//   Implementation of navigation bit synthesis class for L5 CNAV
//
//          Copyright (C) 2020-2029 by Jun Mo, All rights reserved.
//
//----------------------------------------------------------------------

#include <math.h>
#include <memory.h>
#include "ConstVal.h"
#include "L5CNavBit.h"

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


#define INVALID_TOA 255	// valid range of toa is 0~147
#define A_REF 26559710.0
#define OMEGA_DOT_REF (-2.6e-9)
#define NORMINAL_I0 0.94247779607693797153879301498385

static const unsigned int InterleaveMatrix[300] = {
	  0,  50, 100, 150, 200, 250,   1,  51, 101, 151, 201, 251,   2,  52, 102, 152, 202, 252,   3,  53, 103, 153, 203, 253,   4,  54, 104, 154, 204, 254,
	  5,  55, 105, 155, 205, 255,   6,  56, 106, 156, 206, 256,   7,  57, 107, 157, 207, 257,   8,  58, 108, 158, 208, 258,   9,  59, 109, 159, 209, 259,
	 10,  60, 110, 160, 210, 260,  11,  61, 111, 161, 211, 261,  12,  62, 112, 162, 212, 262,  13,  63, 113, 163, 213, 263,  14,  64, 114, 164, 214, 264,
	 15,  65, 115, 165, 215, 265,  16,  66, 116, 166, 216, 266,  17,  67, 117, 167, 217, 267,  18,  68, 118, 168, 218, 268,  19,  69, 119, 169, 219, 269,
	 20,  70, 120, 170, 220, 270,  21,  71, 121, 171, 221, 271,  22,  72, 122, 172, 222, 272,  23,  73, 123, 173, 223, 273,  24,  74, 124, 174, 224, 274,
	 25,  75, 125, 175, 225, 275,  26,  76, 126, 176, 226, 276,  27,  77, 127, 177, 227, 277,  28,  78, 128, 178, 228, 278,  29,  79, 129, 179, 229, 279,
	 30,  80, 130, 180, 230, 280,  31,  81, 131, 181, 231, 281,  32,  82, 132, 182, 232, 282,  33,  83, 133, 183, 233, 283,  34,  84, 134, 184, 234, 284,
	 35,  85, 135, 185, 235, 285,  36,  86, 136, 186, 236, 286,  37,  87, 137, 187, 237, 287,  38,  88, 138, 188, 238, 288,  39,  89, 139, 189, 239, 289,
	 40,  90, 140, 190, 240, 290,  41,  91, 141, 191, 241, 291,  42,  92, 142, 192, 242, 292,  43,  93, 143, 193, 243, 293,  44,  94, 144, 194, 244, 294,
	 45,  95, 145, 195, 245, 295,  46,  96, 146, 196, 246, 296,  47,  97, 147, 197, 247, 297,  48,  98, 148, 198, 248, 298,  49,  99, 149, 199, 249, 299,
};

L5CNavBit::L5CNavBit()
{
	memset(EphMessage, 0, sizeof(EphMessage));
	memset(MidiAlm, 0, sizeof(MidiAlm));
	memset(ReducedAlm, 0, sizeof(ReducedAlm));
	memset(ClockMessage, 0, sizeof(ClockMessage));
	memset(DelayMessage, 0, sizeof(DelayMessage));
	memset(UTCMessage, 0, sizeof(UTCMessage));
	memset(ConvEncodeBits, 0, sizeof(ConvEncodeBits));
	TOA = INVALID_TOA;
}

L5CNavBit::~L5CNavBit()
{
}

// each message is 6s (300bit), according maximum interval, the message is arranged to 48s frame and 1200s super frame
// each frame contains 8 messages and each super frame contains 25 frames, the order of message with a super frame is:
// frame index   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
// msg index 0  10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10 10
// msg index 1  11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11 11
// msg index 2  30 33 31 37 31 37 30 33 31 37 31 37 30 33 31 37 31 37 30 33 31 37 31 37 30
// msg index 3  37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 37 33
// each super frame has 8 message 31 each has 4 reduced amlamanc
// each super frame has 32 message 37, message index 3 contains SV01 to SV24, message index 2 contains SV25 to SV32
// Param is used to distinguish from Dc in L2C and D5 in L5 (0 for L2C, 1 for L5)
int L5CNavBit::GetFrameData(GNSS_TIME StartTime, int svid, int Param, int *NavBits)
{
	int i, TOW, message_type;
	unsigned int MessageData[9], CrcResult;	// 276bit to be encoded by CRC
	int UncodedBits[300], InterleavedBits[300];

	// validate svid to prevent out-of-bounds array access
	if (svid < 1 || svid > 32)
	{
		// fill NavBits with zeros for invalid svid
		memset(NavBits, 0, sizeof(int) * 600); // L5 has 600 symbols after FEC
		return -1;
	}

	// first determine the current TOW and message type
	StartTime.Week += StartTime.MilliSeconds / 604800000;
	StartTime.MilliSeconds %= 604800000;
	TOW = StartTime.MilliSeconds / 6000; // L5 message is 6s long
	
	// Simplified message schedule for demonstration: 10, 11, 30, 37...
	int message_index = TOW % 4;
	switch(message_index)
	{
		case 0: message_type = 10; break;
		case 1: message_type = 11; break;
		case 2: message_type = 30; break;
		case 3: message_type = 37; break; // Almanac
		default: message_type = 10;
	}

	TOW = (TOW + 1) % 100800; // TOW is the time of NEXT message

	GetMessageData(svid, message_type, TOW, MessageData);
	
	// Add header to the message data
	MessageData[0] |= (0x8b << 24) | (svid << 18) | (message_type << 12);
	MessageData[0] |= (TOW & 0x1FFFF) >> 5; // Upper 12 bits of TOW
	MessageData[1] |= (TOW & 0x1F) << 27;   // Lower 5 bits of TOW
	MessageData[1] |= (0 << 26); // Alert flag

	CrcResult = Crc24qEncode(MessageData, 276);

	// Place message bits and CRC into a single array
	for (i = 0; i < 276; i++)
		UncodedBits[i] = (MessageData[i / 32] >> (31 - (i % 32))) & 1;
	for (i = 0; i < 24; i++)
		UncodedBits[276 + i] = (CrcResult >> (23 - i)) & 1;

	// Interleave the 300 bits
	for (i = 0; i < 300; i++)
		InterleavedBits[i] = UncodedBits[InterleaveMatrix[i]];

	// FEC (Forward Error Correction) Rate 1/2 Convolutional Encoder
	// Per IS-GPS-705J, G1=1+x^2+x^3+x^5+x^6 (0x5B) and G2=1+x+x^2+x^3+x^6 (0x79)
	unsigned char &ConvState = ConvEncodeBits[svid - 1];
	for (i = 0; i < 300; i++)
	{
		ConvState = (ConvState << 1) | InterleavedBits[i];
		// Generator G1: 1011011 -> 0x5B
		NavBits[i * 2] = Count1(ConvState & 0x5B);
		// Generator G2: 1111001 -> 0x79
		NavBits[i * 2 + 1] = Count1(ConvState & 0x79);
	}

	return 0;
}

int L5CNavBit::SetEphemeris(int svid, PGPS_EPHEMERIS Eph)
{
	if (svid < 1 || svid > 32 || !Eph || !Eph->valid)
		return 0;
	ComposeEphWords(Eph, EphMessage[svid-1], ClockMessage[svid-1], DelayMessage[svid-1]);
	return svid;
}

int L5CNavBit::SetAlmanac(GPS_ALMANAC Alm[])
{
	int i;

	for (i = 0; i < 32; i ++)
	{
		ComposeAlmWords(&Alm[i], ReducedAlm[i], MidiAlm[i]);
		if ((Alm[i].valid & 1) && TOA == INVALID_TOA)
			TOA = (Alm[i].week << 8) + (Alm[i].toa >> 12);
	}
	return 0;
}

int L5CNavBit::SetIonoUtc(PIONO_PARAM IonoParam, PUTC_PARAM UtcParam)
{
	signed int IntValue;

	if (IonoParam->flag == 0 || (UtcParam->flag & 3) != 3)
		return 0;

	IntValue = UnscaleInt(IonoParam->a0, -30);
	IonoMessage[0] = COMPOSE_BITS(IntValue, 12, 8);
	IntValue = UnscaleInt(IonoParam->a1, -27);
	IonoMessage[0] |= COMPOSE_BITS(IntValue, 4, 8);
	IntValue = UnscaleInt(IonoParam->a2, -24);
	IonoMessage[0] |= COMPOSE_BITS(IntValue >> 4, 0, 4);
	IonoMessage[1] = COMPOSE_BITS(IntValue, 28, 4);
	IntValue = UnscaleInt(IonoParam->a3, -24);
	IonoMessage[1] |= COMPOSE_BITS(IntValue, 20, 8);
	IntValue = UnscaleInt(IonoParam->b0, 11);
	IonoMessage[1] |= COMPOSE_BITS(IntValue, 12, 8);
	IntValue = UnscaleInt(IonoParam->b1, 14);
	IonoMessage[1] |= COMPOSE_BITS(IntValue, 4, 8);
	IntValue = UnscaleInt(IonoParam->b2, 16);
	IonoMessage[1] |= COMPOSE_BITS(IntValue >> 4, 0, 4);
	IonoMessage[2] = COMPOSE_BITS(IntValue, 28, 4);
	IntValue = UnscaleInt(IonoParam->b3, 16);
	IonoMessage[2] |= COMPOSE_BITS(IntValue, 20, 8);
	IonoMessage[2] |= COMPOSE_BITS(UtcParam->WN, 12, 8);	// use WN to take the place of WNop

	IntValue = UnscaleInt(UtcParam->A0, -35);
	UTCMessage[0] = COMPOSE_BITS(IntValue, 5, 16);
	IntValue = UnscaleInt(UtcParam->A1, -51);
	UTCMessage[0] |= COMPOSE_BITS(IntValue >> 8, 0, 5);
	UTCMessage[1] = COMPOSE_BITS(IntValue, 24, 8);
	IntValue = UnscaleInt(UtcParam->A2, -68);
	UTCMessage[1] |= COMPOSE_BITS(IntValue, 17, 7);
	UTCMessage[1] |= COMPOSE_BITS(UtcParam->TLS, 9, 8);
	UTCMessage[1] |= COMPOSE_BITS(UtcParam->tot, 1, 8);	// UtcParam->tot has scale factor of 2^12, so leaving 8LSB as 0 for scale factor 2^4 
	UTCMessage[2] = COMPOSE_BITS(UtcParam->WN, 12, 13);
	UTCMessage[2] |= COMPOSE_BITS(UtcParam->WNLSF >> 1, 0, 12);
	UTCMessage[3] = COMPOSE_BITS(UtcParam->WNLSF, 31, 1);
	UTCMessage[3] |= COMPOSE_BITS(UtcParam->DN, 27, 4);
	UTCMessage[3] |= COMPOSE_BITS(UtcParam->TLSF, 19, 8);

	return 0;
}

int L5CNavBit::ComposeEphWords(PGPS_EPHEMERIS Ephemeris, unsigned int EphData[2][9], unsigned int ClockData[4], unsigned int DelayData[3])
{
    // All scaling factors and bit fields are from IS-GPS-705J, Table 3.5-1
    // The data is packed into a 9-DWORD (288 bit) buffer.
    // We compose the data payload (bits 39-276). Header (bits 1-38) is added later.

    // Clear arrays first
    memset(EphData[0], 0, sizeof(unsigned int) * 9);
    memset(EphData[1], 0, sizeof(unsigned int) * 9);
    memset(ClockData, 0, sizeof(unsigned int) * 4);
    memset(DelayData, 0, sizeof(unsigned int) * 3);

    long long temp_val_ll;
    unsigned long long temp_uval_ll;

    // === Message Type 10: Ephemeris 1 (bits 39-276) ===
    // WN (13 bits, 39-51)
    temp_uval_ll = Ephemeris->week;
    EphData[0][1] |= (unsigned int)((temp_uval_ll & 0x1FFF) << 8);
    // toe (11 bits, 52-62)
    temp_uval_ll = Ephemeris->toe / 300;
    EphData[0][1] |= (unsigned int)((temp_uval_ll & 0x7FF) >> 3);
    EphData[0][2] |= (unsigned int)((temp_uval_ll & 0x7) << 29);
    // M0 (32 bits, 63-94)
    temp_val_ll = (long long)round(Ephemeris->M0 / PI * pow(2, 31));
    EphData[0][2] |= (unsigned int)(((unsigned long long)temp_val_ll >> 3) & 0x1FFFFFFF);
    EphData[0][3] |= (unsigned int)((unsigned long long)temp_val_ll << 29);
    // e (32 bits, 95-126)
    temp_uval_ll = (unsigned long long)round(Ephemeris->ecc * pow(2, 33));
    EphData[0][3] |= (unsigned int)((temp_uval_ll >> 3) & 0x1FFFFFFF);
    EphData[0][4] |= (unsigned int)(temp_uval_ll << 29);
    // sqrt(A) (32 bits, 127-158)
    temp_uval_ll = (unsigned long long)round(Ephemeris->sqrtA * pow(2, 19));
    EphData[0][4] |= (unsigned int)((temp_uval_ll >> 3) & 0x1FFFFFFF);
    EphData[0][5] |= (unsigned int)(temp_uval_ll << 29);

    // === Message Type 11: Ephemeris 2 (bits 39-276) ===
    // toe (11 bits, 39-49)
    temp_uval_ll = Ephemeris->toe / 300;
    EphData[1][1] |= (unsigned int)((temp_uval_ll & 0x7FF) << 10);
    // Omega0 (32 bits, 50-81)
    temp_val_ll = (long long)round(Ephemeris->omega0 / PI * pow(2, 31));
    EphData[1][1] |= (unsigned int)(((unsigned long long)temp_val_ll >> 22) & 0x3FF);
    EphData[1][2] = (unsigned int)((unsigned long long)temp_val_ll << 10);
    // i0 (32 bits, 82-113)
    temp_val_ll = (long long)round(Ephemeris->i0 / PI * pow(2, 31));
    EphData[1][2] |= (unsigned int)(((unsigned long long)temp_val_ll >> 22) & 0x3FF);
    EphData[1][3] = (unsigned int)((unsigned long long)temp_val_ll << 10);
    // w (32 bits, 114-145)
    temp_val_ll = (long long)round(Ephemeris->w / PI * pow(2, 31));
    EphData[1][3] |= (unsigned int)(((unsigned long long)temp_val_ll >> 22) & 0x3FF);
    EphData[1][4] = (unsigned int)((unsigned long long)temp_val_ll << 10);
    // OmegaDot (24 bits, 146-169)
    temp_val_ll = (long long)round(Ephemeris->omega_dot / PI * pow(2, 43));
    EphData[1][4] |= (unsigned int)(((unsigned long long)temp_val_ll >> 14) & 0x3FF);
    EphData[1][5] = (unsigned int)((unsigned long long)temp_val_ll << 18);
    // iDot (14 bits, 170-183)
    temp_val_ll = (long long)round(Ephemeris->idot / PI * pow(2, 43));
    EphData[1][5] |= (unsigned int)(((unsigned long long)temp_val_ll & 0x3FFF) << 4);
    // delta_n (16 bits, 184-199)
    temp_val_ll = (long long)round(Ephemeris->delta_n / PI * pow(2, 43));
    EphData[1][5] |= (unsigned int)(((unsigned long long)temp_val_ll >> 12) & 0xF);
    EphData[1][6] = (unsigned int)((unsigned long long)temp_val_ll << 20);

    // === Message Type 30: Clock and Delay data ===
    // This is composed into ClockData and DelayData arrays.
    // We compose the payload for MT30 (bits 39-135)
    unsigned int mt30_buf[4] = {0}; // 97 bits of data payload
    // toc (11 bits, 39-49)
    temp_uval_ll = Ephemeris->toc / 300;
    mt30_buf[1] |= (unsigned int)((temp_uval_ll & 0x7FF) << 10);
    // af0 (26 bits, 50-75)
    temp_val_ll = (long long)round(Ephemeris->af0 * pow(2, 34));
    mt30_buf[1] |= (unsigned int)(((unsigned long long)temp_val_ll >> 16) & 0x3FF);
    mt30_buf[2] = (unsigned int)((unsigned long long)temp_val_ll << 16);
    // af1 (20 bits, 76-95)
    temp_val_ll = (long long)round(Ephemeris->af1 * pow(2, 46));
    mt30_buf[2] |= (unsigned int)(((unsigned long long)temp_val_ll >> 4) & 0xFFFF);
    mt30_buf[3] = (unsigned int)((unsigned long long)temp_val_ll << 28);
    // af2 (10 bits, 96-105)
    temp_val_ll = (long long)round(Ephemeris->af2 * pow(2, 59));
    mt30_buf[3] |= (unsigned int)(((unsigned long long)temp_val_ll & 0x3FF) << 18);
    
    // The original code splits this into ClockData and DelayData.
    // Based on GetMessageData, ClockData holds the first 4 DWORDs of the message.
    ClockData[0] = mt30_buf[0];
    ClockData[1] = mt30_buf[1];
    ClockData[2] = mt30_buf[2];
    ClockData[3] = mt30_buf[3];

    // DelayData holds the TGD and ISC parts
    memset(DelayData, 0, sizeof(unsigned int) * 3);
    // TGD (10 bits, 106-115)
    temp_val_ll = (long long)round(Ephemeris->tgd * pow(2, 32));
    DelayData[0] |= (unsigned int)(((unsigned long long)temp_val_ll & 0x3FF) << 18);
    // ISC_L5I5 (10 bits, 116-125)
    // Using tgd_ext[2] for ISC_L5I5 as a placeholder from original logic
    temp_val_ll = (long long)round(Ephemeris->tgd_ext[2] * pow(2, 32));
    DelayData[0] |= (unsigned int)(((unsigned long long)temp_val_ll & 0x3FF) << 8);
    // ISC_L5Q5 (10 bits, 126-135)
    // Using tgd_ext[3] for ISC_L5Q5 as a placeholder from original logic
    temp_val_ll = (long long)round(Ephemeris->tgd_ext[3] * pow(2, 32));
    DelayData[0] |= (unsigned int)(((unsigned long long)temp_val_ll >> 2) & 0xFF);
    DelayData[1] |= (unsigned int)((unsigned long long)temp_val_ll << 30);

    return 0;
}

int L5CNavBit::ComposeAlmWords(GPS_ALMANAC Almanac[], unsigned int &ReducedAlmData, unsigned int MidiAlmData[6])
{
	signed int IntValue;
	unsigned int UintValue;
	double DoubleValue;

	MidiAlmData[0] = COMPOSE_BITS(Almanac->valid ? Almanac->svid : 0, 26, 6);
	MidiAlmData[0] |= COMPOSE_BITS(Almanac->valid ? 0 : 7, 23, 3);
	UintValue = UnscaleUint(Almanac->ecc, -16);
	MidiAlmData[0] |= COMPOSE_BITS(UintValue, 12, 11);
	IntValue = UnscaleInt((Almanac->i0 - NORMINAL_I0) / PI, -14);
	MidiAlmData[0] |= COMPOSE_BITS(IntValue, 1, 11);
	IntValue = UnscaleInt(Almanac->omega_dot / PI, -33);
	MidiAlmData[0] |= COMPOSE_BITS(IntValue >> 10, 0, 1);
	MidiAlmData[1] = COMPOSE_BITS(IntValue, 22, 10);
	UintValue = UnscaleUint(Almanac->sqrtA, -4);
	MidiAlmData[1] |= COMPOSE_BITS(IntValue, 5, 17);
	IntValue = UnscaleInt(Almanac->omega0 / PI, -15);
	MidiAlmData[1] |= COMPOSE_BITS(IntValue >> 11, 0, 5);
	MidiAlmData[2] = COMPOSE_BITS(IntValue, 21, 11);
	IntValue = UnscaleInt(Almanac->w / PI, -15);
	MidiAlmData[2] |= COMPOSE_BITS(IntValue, 5, 16);
	IntValue = UnscaleInt(Almanac->M0 / PI, -15);
	MidiAlmData[2] |= COMPOSE_BITS(IntValue >> 11, 0, 5);
	MidiAlmData[3] = COMPOSE_BITS(IntValue, 21, 11);
	IntValue = UnscaleInt(Almanac->af0, -20);
	MidiAlmData[3] |= COMPOSE_BITS(IntValue, 10, 11);
	IntValue = UnscaleInt(Almanac->af0, -37);
	MidiAlmData[3] |= COMPOSE_BITS(IntValue, 0, 10);

	ReducedAlmData = (Almanac->valid ? Almanac->svid : 0) << 25;
	DoubleValue = Almanac->sqrtA * Almanac->sqrtA - A_REF;
	IntValue = (int)(DoubleValue / 512 + 0.5);
	ReducedAlmData |= COMPOSE_BITS(IntValue, 17, 8);
	IntValue = UnscaleInt(Almanac->omega0 / PI, -6);
	ReducedAlmData |= COMPOSE_BITS(IntValue, 10, 7);
	IntValue = UnscaleInt((Almanac->M0 + Almanac->w) / PI, -6);
	ReducedAlmData |= COMPOSE_BITS(IntValue, 3, 7);
	ReducedAlmData |= Almanac->valid ? 0 : 7;
	return 0;
}

void L5CNavBit::GetMessageData(int svid, int message_type, int TOW, unsigned int Data[9])
{
	// validate svid to prevent out-of-bounds array access
	if (svid < 1 || svid > 32)
	{
		// initialize Data with zeros for invalid svid
		memset(Data, 0, sizeof(unsigned int) * 9);
		return;
	}

	memset(Data, 0, sizeof(unsigned int) * 9);

	switch (message_type)
	{
	case 10: // Ephemeris 1
		memcpy(Data, EphMessage[svid-1][0], sizeof(unsigned int) * 9);
		break;
	case 11: // Ephemeris 2
		memcpy(Data, EphMessage[svid-1][1], sizeof(unsigned int) * 9);
		break;
	case 30: // Clock, TGD, Iono
		// Assemble from parts. Note: IonoMessage is composed in SetIonoUtc
		memcpy(Data, ClockMessage[svid-1], sizeof(unsigned int) * 4);
		Data[3] |= DelayMessage[svid-1][0]; // TGD/ISC starts at bit 106
		Data[4] |= DelayMessage[svid-1][1];
		Data[4] |= IonoMessage[0]; // Iono starts at bit 136
		Data[5] |= IonoMessage[1];
		Data[6] |= IonoMessage[2];
		break;
	case 37: // Almanac
		// Assemble from parts. Note: Almanac messages are composed in SetAlmanac
		Data[1] |= (TOA >> 13) & 0xFF; // WNa
		Data[2] |= (TOA & 0x1F) << 27; // toa
		Data[2] |= MidiAlm[svid-1][0];
		Data[3] = MidiAlm[svid-1][1];
		Data[4] = MidiAlm[svid-1][2];
		Data[5] = MidiAlm[svid-1][3];
		break;
	default:
		// Return empty message for unsupported types
		break;
	}
}
