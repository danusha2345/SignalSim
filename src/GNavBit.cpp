#include <math.h>
#include <memory.h>
#include "ConstVal.h"
#include "GNavBit.h"

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
	int i, string_idx, frame_idx;
	unsigned int data[3];
	unsigned int checksum;
	int data_bits[85];

	if (svid < 1 || svid > 24)
		return 1;

	string_idx = (StartTime.MilliSeconds / 2000) % 15;
	frame_idx = (StartTime.MilliSeconds / 30000) % 5;

	if (string_idx < 4)
	{
		data[0] = StringEph[svid - 1][string_idx][0];
		data[1] = StringEph[svid - 1][string_idx][1];
		data[2] = StringEph[svid - 1][string_idx][2];
	}
	else
	{
		data[0] = StringAlm[frame_idx][string_idx - 4][0];
		data[1] = StringAlm[frame_idx][string_idx - 4][1];
		data[2] = StringAlm[frame_idx][string_idx - 4][2];
	}

	AssignBits(data[0], 21, data_bits);
	AssignBits(data[1], 32, data_bits + 21);
	AssignBits(data[2], 32, data_bits + 53);

	checksum = CheckSum(data);

	for (i = 0; i < 8; i++)
		NavBits[i] = (checksum >> (7 - i)) & 1;
	
	for (i = 0; i < 7; i++)
		NavBits[8 + i] = 0;

	for (i = 0; i < 85; i++)
		NavBits[15 + i] = data_bits[i];

	return 0;
}

// Генерация временной метки ГЛОНАСС (укороченная ПСП)
// Согласно ICD ГЛОНАСС, временная метка - это 30-битная последовательность
void GNavBit::GetTimeMarker(int *TimeMarkerBits)
{
	// Временная метка ГЛОНАСС: 000110111000010110010011101000b = 0x1B8593A0
	// Это укороченная псевдослучайная последовательность для синхронизации приёмника
	const unsigned int TIME_MARK = 0x1B8593A0;
	const int MARK_LENGTH = 30;
	
	for (int i = 0; i < MARK_LENGTH; i++)
	{
		TimeMarkerBits[i] = (TIME_MARK >> (29 - i)) & 1;
	}
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

	unsigned int NA = GloAlm[i].day;
	
	for (int frame = 0; frame < 5; frame++)
	{
		if ((StringAlm[frame][0][0] & 0xF0000) != (5 << 16))
		{
			StringAlm[frame][0][0] = (5 << 16) | COMPOSE_BITS(NA, 5, 11);
			StringAlm[frame][0][1] = 0;
			StringAlm[frame][0][2] = 0;
		}
	}
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
	unsigned int NA = 0;
	signed int tau_c = 0;
	unsigned int N4 = 0;
	signed int tau_GPS = 0;
	unsigned int ln5 = 0;
	
	if (UtcParam && (UtcParam->flag & 0x3))
	{
		ln5 = 1;
		if (UtcParam->WN >= 834)
		{
			int weeks_since_glonass_epoch = UtcParam->WN - 834;
			N4 = weeks_since_glonass_epoch / 209;
			int days_in_current_period = (weeks_since_glonass_epoch % 209) * 7;
			NA = days_in_current_period % 1461;
		}
		
		if (UtcParam->flag & 0x2)
		{
			double time_diff = 19.0 - UtcParam->TLS - 10800.0;
			tau_GPS = (signed int)(time_diff * (1L << 30));
		}
	}
	
	for (int frame = 0; frame < 5; frame++)
	{
		StringAlm[frame][0][0] = (5 << 16);
		StringAlm[frame][0][0] |= COMPOSE_BITS(NA, 5, 11);
		StringAlm[frame][0][0] |= COMPOSE_BITS(tau_c >> 28, 0, 4);
		StringAlm[frame][0][1] = COMPOSE_BITS(tau_c >> 4, 8, 24);
		StringAlm[frame][0][1] |= COMPOSE_BITS(tau_c, 4, 4);
		StringAlm[frame][0][1] |= COMPOSE_BITS(0, 3, 1);
		StringAlm[frame][0][1] |= COMPOSE_BITS(N4, 0, 3);
		StringAlm[frame][0][2] = COMPOSE_BITS(N4, 30, 2);
		StringAlm[frame][0][2] |= COMPOSE_BITS(tau_GPS, 8, 22);
		StringAlm[frame][0][2] |= COMPOSE_BITS(ln5, 7, 1);
	}
	
	return 0;
}

int GNavBit::ComposeStringEph(PGLONASS_EPHEMERIS Ephemeris, unsigned int String[][3])
{
	signed int IntValue;
	unsigned int UintValue;

	memset(String, 0, 4 * 3 * sizeof(unsigned int));

	// String 1
	String[0][0] = COMPOSE_BITS(1, 17, 4);
	String[0][0] |= COMPOSE_BITS(Ephemeris->P, 15, 2);
	String[0][0] |= COMPOSE_BITS(Ephemeris->Bn, 14, 1);
	String[0][0] |= COMPOSE_BITS(Ephemeris->tk / 30, 2, 12);
	UintValue = (unsigned int)round(fabs(Ephemeris->x) / pow(2.0, -11));
	String[0][0] |= COMPOSE_BITS(UintValue >> 25, 0, 2);
	String[0][1] = COMPOSE_BITS(UintValue, 7, 25);
	IntValue = (int)round(Ephemeris->vx / pow(2.0, -20));
	String[0][1] |= COMPOSE_BITS(IntValue >> 18, 0, 7);
	String[0][2] = COMPOSE_BITS(IntValue, 14, 18);
	IntValue = (int)round(Ephemeris->ax / pow(2.0, -30));
	String[0][2] |= COMPOSE_BITS(IntValue, 9, 5);
	
	// String 2
	String[1][0] = COMPOSE_BITS(2, 17, 4);
	String[1][0] |= COMPOSE_BITS(Ephemeris->Bn, 16, 1);
	String[1][0] |= COMPOSE_BITS(Ephemeris->P >> 2, 14, 2);
	String[1][0] |= COMPOSE_BITS(Ephemeris->tb / 900, 7, 7);
	UintValue = (unsigned int)round(fabs(Ephemeris->y) / pow(2.0, -11));
	String[1][0] |= COMPOSE_BITS(UintValue >> 21, 0, 7);
	String[1][1] = COMPOSE_BITS(UintValue, 12, 20);
	IntValue = (int)round(Ephemeris->vy / pow(2.0, -20));
	String[1][1] |= COMPOSE_BITS(IntValue, 0, 12);
	String[1][2] = COMPOSE_BITS(IntValue >> 12, 20, 12);
	IntValue = (int)round(Ephemeris->ay / pow(2.0, -30));
	String[1][2] |= COMPOSE_BITS(IntValue, 15, 5);

	// String 3
	String[2][0] = COMPOSE_BITS(3, 17, 4);
	String[2][0] |= COMPOSE_BITS(Ephemeris->P >> 3, 16, 1);
	IntValue = (int)round(Ephemeris->gamma / pow(2.0, -40));
	String[2][0] |= COMPOSE_BITS(IntValue, 5, 11);
	UintValue = (unsigned int)round(fabs(Ephemeris->z) / pow(2.0, -11));
	String[2][0] |= COMPOSE_BITS(UintValue >> 23, 0, 5);
	String[2][1] = COMPOSE_BITS(UintValue, 10, 22);
	IntValue = (int)round(Ephemeris->vz / pow(2.0, -20));
	String[2][1] |= COMPOSE_BITS(IntValue, 0, 10);
	String[2][2] = COMPOSE_BITS(IntValue >> 10, 22, 14);
	IntValue = (int)round(Ephemeris->az / pow(2.0, -30));
	String[2][2] |= COMPOSE_BITS(IntValue, 17, 5);

	// String 4
	String[3][0] = COMPOSE_BITS(4, 17, 4);
	IntValue = (int)round(Ephemeris->tn / pow(2.0, -30));
	String[3][0] |= COMPOSE_BITS(IntValue >> 5, 0, 17);
	String[3][1] = COMPOSE_BITS(IntValue, 27, 5);
	IntValue = (int)round(Ephemeris->dtn / pow(2.0, -30));
	String[3][1] |= COMPOSE_BITS(IntValue, 22, 5);
	String[3][1] |= COMPOSE_BITS(Ephemeris->En, 17, 5);
	String[3][1] |= COMPOSE_BITS(Ephemeris->P >> 4, 16, 1);
	String[3][2] = COMPOSE_BITS(Ephemeris->Ft, 28, 4);
	String[3][2] |= COMPOSE_BITS(Ephemeris->day, 17, 11);
	String[3][2] |= COMPOSE_BITS(Ephemeris->n, 12, 5);
	String[3][2] |= COMPOSE_BITS(Ephemeris->M, 10, 2);

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
	
	StreamEven[0] = COMPOSE_BITS((Almanac->flag ? 0 : 1), 15, 1);
	StreamEven[0] |= COMPOSE_BITS(1, 13, 2);
	StreamEven[0] |= COMPOSE_BITS(Almanac->freq & 0x1F, 8, 5);
	UintValue = UnscaleInt(fabs(Almanac->clock_error), -18);
	UintValue |= (Almanac->clock_error < 0) ? (1 << 9) : 0;
	StreamEven[0] |= COMPOSE_BITS(UintValue >> 2, 0, 8);
	
	StreamEven[1] = COMPOSE_BITS(UintValue, 30, 2);
	UintValue = UnscaleUint(fabs(Almanac->lambda), -20);
	UintValue |= (Almanac->lambda < 0) ? (1 << 20) : 0;
	StreamEven[1] |= COMPOSE_BITS(UintValue, 9, 21);
	UintValue = UnscaleUint(fabs(Almanac->di), -20);
	UintValue |= (Almanac->di < 0) ? (1 << 17) : 0;
	StreamEven[1] |= COMPOSE_BITS(UintValue >> 9, 0, 9);
	
	StreamEven[2] = COMPOSE_BITS(UintValue, 23, 9);
	UintValue = UnscaleUint(Almanac->ecc, -20);
	StreamEven[2] |= COMPOSE_BITS(UintValue, 8, 15);
	
	UintValue = UnscaleUint(fabs(Almanac->w), -15);
	UintValue |= (Almanac->w < 0) ? (1 << 15) : 0;
	StreamOdd[0] = COMPOSE_BITS(UintValue, 0, 16);
	
	UintValue = UnscaleUint(Almanac->t, -5);
	StreamOdd[1] = COMPOSE_BITS(UintValue, 11, 21);
	UintValue = UnscaleUint(fabs(Almanac->dt), -9);
	UintValue |= (Almanac->dt < 0) ? (1 << 21) : 0;
	StreamOdd[1] |= COMPOSE_BITS(UintValue >> 11, 0, 11);
	
	StreamOdd[2] = COMPOSE_BITS(UintValue, 21, 11);
	UintValue = UnscaleUint(fabs(Almanac->dt_dot), -14);
	UintValue |= (Almanac->dt_dot < 0) ? (1 << 6) : 0;
	StreamOdd[2] |= COMPOSE_BITS(UintValue, 14, 7);
	StreamOdd[2] |= COMPOSE_BITS(Almanac->freq & 0x1F, 9, 5);
	StreamOdd[2] |= COMPOSE_BITS((Almanac->flag ? 0 : 1), 8, 1);
	
	return 0;
}

// Calculate Hamming code for GLONASS string (85 data bits)
// The checksum is calculated over 85 data bits. The result is 8 parity bits.
// The bit numbering is from MSB (1) to LSB (85).
unsigned int GNavBit::CheckSum(unsigned int Data[3])
{
    int i;
    int data_bits[85];
    unsigned char parity[8] = {0};
    unsigned int checksum = 0;

    // Extract 85 data bits into a simple array
    AssignBits(Data[0], 21, data_bits);
    AssignBits(Data[1], 32, data_bits + 21);
    AssignBits(Data[2], 32, data_bits + 53);

    // Calculate parity bits p1-p7 based on ICD
    for (i = 0; i < 85; i++)
    {
        if (((i+1) % 2) != 0) parity[0] ^= data_bits[i]; // p1
        if ((((i+1)/2) % 2) != 0) parity[1] ^= data_bits[i]; // p2
        if ((((i+1)/4) % 2) != 0) parity[2] ^= data_bits[i]; // p3
        if ((((i+1)/8) % 2) != 0) parity[3] ^= data_bits[i]; // p4
        if ((((i+1)/16) % 2) != 0) parity[4] ^= data_bits[i]; // p5
        if ((((i+1)/32) % 2) != 0) parity[5] ^= data_bits[i]; // p6
        if ((((i+1)/64) % 2) != 0) parity[6] ^= data_bits[i]; // p7
    }

    // p8 is the overall parity of data bits + p1-p7
    parity[7] = parity[0]^parity[1]^parity[2]^parity[3]^parity[4]^parity[5]^parity[6];
    for (i = 0; i < 85; i++)
        parity[7] ^= data_bits[i];

    // Assemble the 8-bit checksum
    for (i = 0; i < 8; i++)
        checksum |= (parity[i] << (7-i));

    return checksum;
}
