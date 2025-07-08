#include <stdio.h>
#include <memory.h>
#include <math.h>
#include <iostream>
#include <cstring>
#include <chrono>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "SignalSim.h"
#include "../inc/FastMath.h"

// Constants for quantization and time conversion
const double QUANT_SCALE_IQ4 = 3.0;
const double QUANT_SCALE_IQ8 = 25.0;
const int WEEK_MS = 604800000;

#define TOTAL_GPS_SAT 32
#define TOTAL_BDS_SAT 63
#define TOTAL_GAL_SAT 36
#define TOTAL_GLO_SAT 24
#define TOTAL_SAT_CHANNEL 128

typedef enum {
    DataBitLNav, DataBitCNav, DataBitCNav2, // for GPS
    DataBitGNav, DataBitGNav2,	// for GLONASS
    DataBitD1D2, DataBitBCNav1, DataBitBCNav2, DataBitBCNav3,	// for BDS
    DataBitINav, DataBitFNav, DataBitECNav,	// for Galileo
    DataBitSbas, // for SBAS
} DataBitType;

void UpdateSatParamList(GNSS_TIME CurTime, KINEMATIC_INFO CurPos, int ListCount, PSIGNAL_POWER PowerList, PIONO_PARAM IonoParam);
int StepToNextMs();
complex_number GenerateNoise(double Sigma);
NavBit* GetNavData(GnssSystem SatSystem, int SatSignalIndex, NavBit* NavBitArray[]);
void QuantSamplesIQ4(complex_number Samples[], int Length, unsigned char QuantSamples[], int& ClippedCount);
void QuantSamplesIQ8(complex_number Samples[], int Length, unsigned char QuantSamples[], int& ClippedCount);

CTrajectory Trajectory;
CPowerControl PowerControl;
CNavData NavData;
OUTPUT_PARAM OutputParam;
GNSS_TIME CurTime;
PGPS_EPHEMERIS GpsEph[TOTAL_GPS_SAT], GpsEphVisible[TOTAL_GPS_SAT];
PGPS_EPHEMERIS BdsEph[TOTAL_BDS_SAT], BdsEphVisible[TOTAL_BDS_SAT];
PGPS_EPHEMERIS GalEph[TOTAL_GAL_SAT], GalEphVisible[TOTAL_GAL_SAT];
PGLONASS_EPHEMERIS GloEph[TOTAL_GLO_SAT], GloEphVisible[TOTAL_GLO_SAT];
SATELLITE_PARAM GpsSatParam[TOTAL_GPS_SAT], BdsSatParam[TOTAL_BDS_SAT], GalSatParam[TOTAL_GAL_SAT], GloSatParam[TOTAL_GLO_SAT];	// satellite parameter array at CurTime
int GpsSatNumber, BdsSatNumber, GalSatNumber, GloSatNumber;	// number of visible satellite
const int SignalCenterFreq[][8] = {
	{ FREQ_GPS_L1, FREQ_GPS_L1, FREQ_GPS_L2, FREQ_GPS_L2, FREQ_GPS_L5 },
	{ FREQ_BDS_B1C, FREQ_BDS_B1I, FREQ_BDS_B2I, FREQ_BDS_B3I, FREQ_BDS_B2a, FREQ_BDS_B2b, FREQ_BDS_B2ab },
	{ FREQ_GAL_E1, FREQ_GAL_E5a, FREQ_GAL_E5b, FREQ_GAL_E5, FREQ_GAL_E6 },
	{ FREQ_GLO_G1, FREQ_GLO_G2 },
};
const char *SignalName[][8] = {
	{ "L1CA", "L1C", "L2C", "L2P", "L5", },
	{ "B1C", "B1I", "B2I", "B3I", "B2a", "B2b", "B2ab", },
	{ "E1", "E5a", "E5b", "E5", "E6", },
	{ "G1", "G2", },
};

int main(int argc, char* argv[])
{
	int i, j;
	JsonStream JsonTree;
	JsonObject *Object;
	UTC_TIME UtcTime;
	LLA_POSITION StartPos;
	KINEMATIC_INFO CurPos;
	LOCAL_SPEED StartVel;
	NavBit *NavBitArray[12];
	GLONASS_TIME GlonassTime;
	GNSS_TIME BdsTime;
	int ListCount;
	PSIGNAL_POWER PowerList = NULL;
	int FreqLow, FreqHigh;
	CSatIfSignal* SatIfSignal[TOTAL_SAT_CHANNEL];
	int TotalChannelNumber, SignalIndex;
	int IfFreq, FdmaOffset;
	complex_number *NoiseArray;
	unsigned char *QuantArray;
	FILE* IfFile;

	printf("\n================================================================================\n");
	printf("                          IF SIGNAL GENERATION \n");
	printf("================================================================================\n");

	// read JSON file and assign parameters
    const char* jsonFile = "IfGenTest.json"; // Default JSON file

    // Check if a command-line argument is provided
    if (argc > 1)
    {
        if (std::strcmp(argv[1], "--help") == 0)
        {
            std::cout << "Usage: " << argv[0] << " [optional JSON file path]\n";
            return 0;
        }
        jsonFile = argv[1]; // Use the provided JSON file
    }

    // Read the JSON file
	printf("[INFO]\tLoading JSON file: %s\n", jsonFile);
    if (JsonTree.ReadFile(jsonFile) != 0)
    {
        std::cerr << "[ERROR]\tUnable to read JSON file: " << jsonFile << "\n";
        return 1;
    }
	else
	{
		printf("[INFO]\tJSON file read successfully: %s\n", jsonFile);
	}
    Object = JsonTree.GetRootObject();

	AssignParameters(Object, &UtcTime, &StartPos, &StartVel, &Trajectory, &NavData, &OutputParam, &PowerControl, NULL);

	// initial variables
 	Trajectory.ResetTrajectoryTime();
	CurTime = UtcToGpsTime(UtcTime);
	GlonassTime = UtcToGlonassTime_Corrected(UtcTime);
	BdsTime = UtcToBdsTime(UtcTime);
	CurPos = LlaToEcef(StartPos);
	SpeedLocalToEcef(StartPos, StartVel, CurPos);
	
	// Debug time conversion
	printf("[DEBUG] UTC Time: %04d-%02d-%02d %02d:%02d:%02d\n", 
		UtcTime.Year, UtcTime.Month, UtcTime.Day, UtcTime.Hour, UtcTime.Minute, UtcTime.Second);
	printf("[DEBUG] GPS Time: Week=%d, MS=%d (%.1f seconds in week)\n", 
		CurTime.Week, CurTime.MilliSeconds, CurTime.MilliSeconds / 1000.0);
	
	printf("[INFO]\tOpening output file: %s\n", OutputParam.filename);
	if ((IfFile = fopen(OutputParam.filename, "wb")) == NULL)
	{
		printf("[ERROR]\tFailed to open output file: %s\n", OutputParam.filename);
		return 0;
	}
	printf("[INFO]\tOutput file opened successfully.\n");

	for (i = 0; i < TOTAL_GPS_SAT; i ++)
		GpsSatParam[i].CN0 = (int)(PowerControl.InitCN0 * 100 + 0.5);
	for (i = 0; i < TOTAL_BDS_SAT; i ++)
		BdsSatParam[i].CN0 = (int)(PowerControl.InitCN0 * 100 + 0.5);
	for (i = 0; i < TOTAL_GAL_SAT; i ++)
		GalSatParam[i].CN0 = (int)(PowerControl.InitCN0 * 100 + 0.5);
	for (i = 0; i < TOTAL_GLO_SAT; i++)
		GloSatParam[i].CN0 = (int)(PowerControl.InitCN0 * 100 + 0.5);
	// create naviagtion bit instances
	for (i = 0; i < sizeof(NavBitArray) / sizeof(NavBit*); i++)
	{
		switch (i)
		{
		case DataBitLNav:   NavBitArray[i] = new LNavBit; break;
		case DataBitCNav:   NavBitArray[i] = new CNavBit; break;
		case DataBitCNav2:  NavBitArray[i] = new CNav2Bit; break;
		case DataBitGNav:   NavBitArray[i] = new GNavBit; break;
		case DataBitGNav2:  NavBitArray[i] = (NavBit*)0; break;
		case DataBitD1D2:   NavBitArray[i] = new D1D2NavBit; break;
		case DataBitBCNav1: NavBitArray[i] = new BCNav1Bit; break;
		case DataBitBCNav2: NavBitArray[i] = new BCNav2Bit; break;
		case DataBitBCNav3: NavBitArray[i] = new BCNav3Bit; break;
		case DataBitINav:   NavBitArray[i] = new INavBit; break;
		case DataBitFNav:   NavBitArray[i] = new FNavBit; break;
		case DataBitECNav:  NavBitArray[i] = (NavBit*)0; break;
		case DataBitSbas:   NavBitArray[i] = (NavBit*)0; break;
		default:            NavBitArray[i] = (NavBit*)0; break;
		}
	}

	// determine whether signal within IF band (expanded bandwidth for multi-system support)
	
	double BandwidthExpansionFactor = 1.0;  // Use normal bandwidth (sample rate covers GPS+BeiDou)
	FreqLow = (OutputParam.CenterFreq - OutputParam.SampleFreq * BandwidthExpansionFactor / 2) * 1000;
	FreqHigh = (OutputParam.CenterFreq + OutputParam.SampleFreq * BandwidthExpansionFactor / 2) * 1000;
	if (OutputParam.FreqSelect[GpsSystem])
	{
		if ((OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L1CA)) && (FREQ_GPS_L1 < FreqLow || FREQ_GPS_L1 > FreqHigh))
			OutputParam.FreqSelect[GpsSystem] &= ~(1 << SIGNAL_INDEX_L1CA);
		if ((OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L1C)) && (FREQ_GPS_L1 < FreqLow || FREQ_GPS_L1 > FreqHigh))
			OutputParam.FreqSelect[GpsSystem] &= ~(1 << SIGNAL_INDEX_L1C);
		if ((OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L2C)) && (FREQ_GPS_L2 < FreqLow || FREQ_GPS_L2 > FreqHigh))
			OutputParam.FreqSelect[GpsSystem] &= ~(1 << SIGNAL_INDEX_L2C);
		if ((OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L2P)) && (FREQ_GPS_L2 < FreqLow || FREQ_GPS_L2 > FreqHigh))
			OutputParam.FreqSelect[GpsSystem] &= ~(1 << SIGNAL_INDEX_L2P);
		if ((OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L5)) && (FREQ_GPS_L5 < FreqLow || FREQ_GPS_L5 > FreqHigh))
			OutputParam.FreqSelect[GpsSystem] &= ~(1 << SIGNAL_INDEX_L5);
	}
	if (OutputParam.FreqSelect[BdsSystem])
	{
		if ((OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B1C)) && (FREQ_BDS_B1C < FreqLow || FREQ_BDS_B1C > FreqHigh))
			OutputParam.FreqSelect[BdsSystem] &= ~(1 << SIGNAL_INDEX_B1C);
		if ((OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B1I)) && (FREQ_BDS_B1I < FreqLow || FREQ_BDS_B1I > FreqHigh))
			OutputParam.FreqSelect[BdsSystem] &= ~(1 << SIGNAL_INDEX_B1I);
		if ((OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B2I)) && (FREQ_BDS_B2I < FreqLow || FREQ_BDS_B2I > FreqHigh))
			OutputParam.FreqSelect[BdsSystem] &= ~(1 << SIGNAL_INDEX_B2I);
		if ((OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B3I)) && (FREQ_BDS_B3I < FreqLow || FREQ_BDS_B3I > FreqHigh))
			OutputParam.FreqSelect[BdsSystem] &= ~(1 << SIGNAL_INDEX_B3I);
		if ((OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B2a)) && (FREQ_BDS_B2a < FreqLow || FREQ_BDS_B2a > FreqHigh))
			OutputParam.FreqSelect[BdsSystem] &= ~(1 << SIGNAL_INDEX_B2a);
		if ((OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B2b)) && (FREQ_BDS_B2b < FreqLow || FREQ_BDS_B2b > FreqHigh))
			OutputParam.FreqSelect[BdsSystem] &= ~(1 << SIGNAL_INDEX_B2b);
	}
	if (OutputParam.FreqSelect[GalileoSystem])
	{
		if ((OutputParam.FreqSelect[GalileoSystem] & (1 << SIGNAL_INDEX_E1)) && (FREQ_GAL_E1 < FreqLow || FREQ_GAL_E1 > FreqHigh))
			OutputParam.FreqSelect[GalileoSystem] &= ~(1 << SIGNAL_INDEX_E1);
		if ((OutputParam.FreqSelect[GalileoSystem] & (1 << SIGNAL_INDEX_E5a)) && (FREQ_GAL_E5a < FreqLow || FREQ_GAL_E5a > FreqHigh))
			OutputParam.FreqSelect[GalileoSystem] &= ~(1 << SIGNAL_INDEX_E5a);
		if ((OutputParam.FreqSelect[GalileoSystem] & (1 << SIGNAL_INDEX_E5b)) && (FREQ_GAL_E5b < FreqLow || FREQ_GAL_E5b > FreqHigh))
			OutputParam.FreqSelect[GalileoSystem] &= ~(1 << SIGNAL_INDEX_E5b);
		if ((OutputParam.FreqSelect[GalileoSystem] & (1 << SIGNAL_INDEX_E6)) && (FREQ_GAL_E6 < FreqLow || FREQ_GAL_E6 > FreqHigh))
			OutputParam.FreqSelect[GalileoSystem] &= ~(1 << SIGNAL_INDEX_E6);
	}
	if (OutputParam.FreqSelect[GlonassSystem])
	{
		if ((OutputParam.FreqSelect[GlonassSystem] & (1 << SIGNAL_INDEX_G1)) && (FREQ_GLO_G1 < FreqLow || FREQ_GLO_G1 > FreqHigh))
			OutputParam.FreqSelect[GlonassSystem] &= ~(1 << SIGNAL_INDEX_G1);
		if ((OutputParam.FreqSelect[GlonassSystem] & (1 << SIGNAL_INDEX_G2)) && (FREQ_GLO_G2 < FreqLow || FREQ_GLO_G2 > FreqHigh))
			OutputParam.FreqSelect[GlonassSystem] &= ~(1 << SIGNAL_INDEX_G2);
	}
	

	// set Ionosphere and UTC parameter for different navigation data bit
	NavBitArray[DataBitLNav]->SetIonoUtc(NavData.GetGpsIono(), NavData.GetGpsUtcParam());
	NavBitArray[DataBitCNav]->SetIonoUtc(NavData.GetGpsIono(), NavData.GetGpsUtcParam());
	NavBitArray[DataBitCNav2]->SetIonoUtc(NavData.GetGpsIono(), NavData.GetGpsUtcParam());
	NavBitArray[DataBitD1D2]->SetIonoUtc(NavData.GetBdsIono(), NavData.GetBdsUtcParam());
	NavBitArray[DataBitBCNav1]->SetIonoUtc(NavData.GetBdsIono(), NavData.GetBdsUtcParam());
	NavBitArray[DataBitINav]->SetIonoUtc(NavData.GetGalileoIono(), NavData.GetGalileoUtcParam());
	NavBitArray[DataBitFNav]->SetIonoUtc(NavData.GetGalileoIono(), NavData.GetGalileoUtcParam());
	// Find ephemeris match current time and fill in data to generate bit stream
	for (i = 1; i <= TOTAL_GPS_SAT; i ++)
	{
		// Get the best available ephemeris
		// Use the original 4-parameter calls but in correct order
		GpsEph[i-1] = NavData.FindEphemeris(GpsSystem, CurTime, i, 0, 0);
		
		// For L1CA/L1C/L2C/L5, all can use the same ephemeris data
		// The navigation message format is different, but orbital parameters are the same
		NavBitArray[DataBitLNav]->SetEphemeris(i, GpsEph[i - 1]);
		NavBitArray[DataBitCNav]->SetEphemeris(i, GpsEph[i - 1]);
		NavBitArray[DataBitCNav2]->SetEphemeris(i, GpsEph[i - 1]);
	}
	for (i = 1; i <= TOTAL_BDS_SAT; i ++)
	{
		BdsEph[i-1] = NavData.FindEphemeris(BdsSystem, BdsTime, i, 0, 0);
		NavBitArray[DataBitD1D2]->SetEphemeris(i, BdsEph[i - 1]);
		NavBitArray[DataBitBCNav1]->SetEphemeris(i, BdsEph[i - 1]);
		NavBitArray[DataBitBCNav2]->SetEphemeris(i, BdsEph[i - 1]);
		NavBitArray[DataBitBCNav3]->SetEphemeris(i, BdsEph[i - 1]);
	}
	for (i = 1; i <= TOTAL_GAL_SAT; i++)
	{
		GalEph[i - 1] = NavData.FindEphemeris(GalileoSystem, CurTime, i, 0, 0);
		NavBitArray[DataBitINav]->SetEphemeris(i, GalEph[i - 1]);
		NavBitArray[DataBitFNav]->SetEphemeris(i, GalEph[i - 1]);
	}
	for (i = 1; i <= TOTAL_GLO_SAT; i++)
	{
		GloEph[i - 1] = NavData.FindGloEphemeris(GlonassTime, i);
		NavBitArray[DataBitGNav]->SetEphemeris(i, (PGPS_EPHEMERIS)GloEph[i - 1]);
	}
	NavData.CompleteAlmanac(BdsSystem, UtcTime);
	NavBitArray[DataBitLNav]->SetAlmanac(NavData.GetGpsAlmanac());
	NavBitArray[DataBitCNav]->SetAlmanac(NavData.GetGpsAlmanac());
	NavBitArray[DataBitCNav2]->SetAlmanac(NavData.GetGpsAlmanac());
	NavBitArray[DataBitD1D2]->SetAlmanac(NavData.GetBdsAlmanac());
	NavBitArray[DataBitBCNav1]->SetAlmanac(NavData.GetBdsAlmanac());
	NavBitArray[DataBitBCNav2]->SetAlmanac(NavData.GetBdsAlmanac());
	NavBitArray[DataBitBCNav3]->SetAlmanac(NavData.GetBdsAlmanac());
	NavBitArray[DataBitINav]->SetAlmanac(NavData.GetGalileoAlmanac());
	NavBitArray[DataBitFNav]->SetAlmanac(NavData.GetGalileoAlmanac());
	NavBitArray[DataBitGNav]->SetAlmanac((PGPS_ALMANAC)NavData.GetGlonassAlmanac());

	// Debug: count loaded ephemerides
	int gpsEphCount = 0, bdsEphCount = 0, galEphCount = 0, gloEphCount = 0;
	for (i = 0; i < TOTAL_GPS_SAT; i++) if (GpsEph[i] != NULL) gpsEphCount++;
	for (i = 0; i < TOTAL_BDS_SAT; i++) if (BdsEph[i] != NULL) bdsEphCount++;
	for (i = 0; i < TOTAL_GAL_SAT; i++) if (GalEph[i] != NULL) galEphCount++;
	for (i = 0; i < TOTAL_GLO_SAT; i++) if (GloEph[i] != NULL) gloEphCount++;
	printf("[DEBUG]\tLoaded ephemerides: GPS=%d/%d, BDS=%d/%d, GAL=%d/%d, GLO=%d/%d\n", 
		gpsEphCount, TOTAL_GPS_SAT, bdsEphCount, TOTAL_BDS_SAT, galEphCount, TOTAL_GAL_SAT, gloEphCount, TOTAL_GLO_SAT);

	// calculate visible satellite at start time and calculate satellite parameters
	GpsSatNumber = (OutputParam.FreqSelect[GpsSystem]) ? GetVisibleSatellite(CurPos, CurTime, OutputParam, GpsSystem, GpsEph, TOTAL_GPS_SAT, GpsEphVisible) : 0;
	BdsSatNumber = (OutputParam.FreqSelect[BdsSystem]) ? GetVisibleSatellite(CurPos, CurTime, OutputParam, BdsSystem, BdsEph, TOTAL_BDS_SAT, BdsEphVisible) : 0;
	GalSatNumber = (OutputParam.FreqSelect[GalileoSystem]) ? GetVisibleSatellite(CurPos, CurTime, OutputParam, GalileoSystem, GalEph, TOTAL_GAL_SAT, GalEphVisible) : 0;
	GloSatNumber = (OutputParam.FreqSelect[GlonassSystem]) ? GetGlonassVisibleSatellite(CurPos, GlonassTime, OutputParam, GloEph, TOTAL_GLO_SAT, GloEphVisible) : 0;
	ListCount = PowerControl.GetPowerControlList(0, PowerList);
	UpdateSatParamList(CurTime, CurPos, ListCount, PowerList, NavData.GetGpsIono());
	if(PowerList) delete[] PowerList;

	// create CSatIfSignal class for visible satellite, all other satellites clear pointer to NULL
	memset(SatIfSignal, 0, sizeof(SatIfSignal));
	TotalChannelNumber = 0;
	
#ifdef _OPENMP
	printf("[INFO]\tOpenMP configured for PARALLEL execution (%d threads auto-detected)\n", omp_get_max_threads());
#else
	printf("[INFO]\tOpenMP not available - using sequential processing\n");
#endif
	
	printf("[INFO]\tGenerating IF data with following satellite signals:\n\n");
	
	// Enhanced signal display with cleaner formatting
	printf("[INFO]\tEnabled Signals:\n");
	
	// GPS signals
	if (OutputParam.FreqSelect[GpsSystem]) {
		printf("\tGPS : [ ");
		if (OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L1CA)) printf("L1CA ");
		if (OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L1C)) printf("L1C ");
		if (OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L2C)) printf("L2C ");
		if (OutputParam.FreqSelect[GpsSystem] & (1 << SIGNAL_INDEX_L5)) printf("L5 ");
		printf("]\n");
	}
	
	// BDS signals
	if (OutputParam.FreqSelect[BdsSystem]) {
		printf("\tBDS : [ ");
		if (OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B1C)) printf("B1C ");
		if (OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B1I)) printf("B1I ");
		if (OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B2I)) printf("B2I ");
		if (OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B2a)) printf("B2a ");
		if (OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B2b)) printf("B2b ");
		if (OutputParam.FreqSelect[BdsSystem] & (1 << SIGNAL_INDEX_B3I)) printf("B3I ");
		printf("]\n");
	}
	
	// Galileo signals
	if (OutputParam.FreqSelect[GalileoSystem]) {
		printf("\tGAL : [ ");
		if (OutputParam.FreqSelect[GalileoSystem] & (1 << SIGNAL_INDEX_E1)) printf("E1 ");
		if (OutputParam.FreqSelect[GalileoSystem] & (1 << SIGNAL_INDEX_E5a)) printf("E5a ");
		if (OutputParam.FreqSelect[GalileoSystem] & (1 << SIGNAL_INDEX_E5b)) printf("E5b ");
		if (OutputParam.FreqSelect[GalileoSystem] & (1 << SIGNAL_INDEX_E6)) printf("E6 ");
		printf("]\n");
	}
	
	// GLONASS signals
	if (OutputParam.FreqSelect[GlonassSystem]) {
		printf("\tGLO : [ ");
		if (OutputParam.FreqSelect[GlonassSystem] & (1 << SIGNAL_INDEX_G1)) printf("G1 ");
		if (OutputParam.FreqSelect[GlonassSystem] & (1 << SIGNAL_INDEX_G2)) printf("G2 ");
		printf("]\n");
	}
	printf("\n");
	// Count total signals per system
	int GpsSignalCount = 0, BdsSignalCount = 0, GalSignalCount = 0, GloSignalCount = 0;
	for (SignalIndex = SIGNAL_INDEX_L1CA; SignalIndex <= SIGNAL_INDEX_L5; SignalIndex++)
		if (OutputParam.FreqSelect[GpsSystem] & (1 << SignalIndex)) GpsSignalCount++;
	for (SignalIndex = SIGNAL_INDEX_B1C; SignalIndex <= SIGNAL_INDEX_B2b; SignalIndex++)
		if (OutputParam.FreqSelect[BdsSystem] & (1 << SignalIndex)) BdsSignalCount++;
	for (SignalIndex = SIGNAL_INDEX_E1; SignalIndex <= SIGNAL_INDEX_E6; SignalIndex++)
		if (OutputParam.FreqSelect[GalileoSystem] & (1 << SignalIndex)) GalSignalCount++;
	for (SignalIndex = SIGNAL_INDEX_G1; SignalIndex <= SIGNAL_INDEX_G2; SignalIndex++)
		if (OutputParam.FreqSelect[GlonassSystem] & (1 << SignalIndex)) GloSignalCount++;
	
	printf("Signals Summary Table:\n");
	
	// Enhanced constellation summary table
	printf("+---------------+-------------+--------------+------------------------------+\n");
	printf("| Constellation | Visible SVs | Signals / SV | Total Signals / Visible SVs |\n");
	printf("+---------------+-------------+--------------+------------------------------+\n");
	printf("| GPS           | %-11d | %-12d | %-28d |\n", GpsSatNumber, GpsSignalCount, GpsSatNumber * GpsSignalCount);
	printf("| BeiDou        | %-11d | %-12d | %-28d |\n", BdsSatNumber, BdsSignalCount, BdsSatNumber * BdsSignalCount);
	printf("| Galileo       | %-11d | %-12d | %-28d |\n", GalSatNumber, GalSignalCount, GalSatNumber * GalSignalCount);
	printf("| GLONASS       | %-11d | %-12d | %-28d |\n", GloSatNumber, GloSignalCount, GloSatNumber * GloSignalCount);
	printf("+---------------+-------------+--------------+------------------------------+\n");
	
	int TotalVisibleSVs = GpsSatNumber + BdsSatNumber + GalSatNumber + GloSatNumber;
	int TotalChannels = GpsSatNumber * GpsSignalCount + BdsSatNumber * BdsSignalCount + GalSatNumber * GalSignalCount + GloSatNumber * GloSignalCount;
	printf("Total Visible SVs = %d, Total channels = %d\n\n", TotalVisibleSVs, TotalChannels);
	for (SignalIndex = SIGNAL_INDEX_L1CA; SignalIndex <= SIGNAL_INDEX_L5; SignalIndex++)
	{
		if (!(OutputParam.FreqSelect[GpsSystem] & (1 << SignalIndex)))
			continue;
		IfFreq = SignalCenterFreq[0][SignalIndex] - OutputParam.CenterFreq * 1000;
		printf("GPS %s with IF %+dkHz:\n", SignalName[0][SignalIndex], IfFreq / 1000);
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n");
		printf("| SV | Doppler (Hz) | SV | Doppler (Hz) | SV | Doppler (Hz) | SV | Doppler (Hz) |\n");
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n");
		int svCount = 0;
		for (i = 0; i < GpsSatNumber; i++)
		{
			if (TotalChannelNumber >= TOTAL_SAT_CHANNEL)
				break;
			SatIfSignal[TotalChannelNumber] = new CSatIfSignal(OutputParam.SampleFreq, IfFreq, GpsSystem, SignalIndex, GpsEphVisible[i]->svid);
			SatIfSignal[TotalChannelNumber]->InitState(CurTime, &GpsSatParam[GpsEphVisible[i]->svid-1], GetNavData(GpsSystem, SignalIndex, NavBitArray));
			TotalChannelNumber++;
			
			if (svCount % 4 == 0) printf("|");
			printf(" %02d | %+12d |", GpsEphVisible[i]->svid, (int)GetDoppler(&GpsSatParam[GpsEphVisible[i]->svid-1], SignalIndex));
			svCount++;
			if (svCount % 4 == 0) printf("\n");
		}
		// Fill remaining columns if needed
		while (svCount % 4 != 0) {
			printf("    |              |");
			svCount++;
		}
		if (svCount > 0 && (svCount-1) % 4 == 3) printf("\n");
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n\n");
	}
	for (SignalIndex = SIGNAL_INDEX_B1C; SignalIndex <= SIGNAL_INDEX_B2b; SignalIndex++)
	{
		if (!(OutputParam.FreqSelect[BdsSystem] & (1 << SignalIndex)))
			continue;
		IfFreq = SignalCenterFreq[1][SignalIndex] - OutputParam.CenterFreq * 1000;
		printf("BeiDou %s with IF %+dkHz:\n", SignalName[1][SignalIndex], IfFreq / 1000);
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n");
		printf("| SV | Doppler (Hz) | SV | Doppler (Hz) | SV | Doppler (Hz) | SV | Doppler (Hz) |\n");
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n");

		int svCount = 0;
		for (i = 0; i < BdsSatNumber; i++)
		{
			if (TotalChannelNumber >= TOTAL_SAT_CHANNEL)
				break;
			SatIfSignal[TotalChannelNumber] = new CSatIfSignal(OutputParam.SampleFreq, IfFreq, BdsSystem, SignalIndex, BdsEphVisible[i]->svid);
			SatIfSignal[TotalChannelNumber]->InitState(CurTime, &BdsSatParam[BdsEphVisible[i]->svid - 1], GetNavData(BdsSystem, SignalIndex, NavBitArray));
			TotalChannelNumber++;
			
			if (svCount % 4 == 0) printf("|");
			printf(" %02d | %+12d |", BdsEphVisible[i]->svid, (int)GetDoppler(&BdsSatParam[BdsEphVisible[i]->svid-1], SignalIndex));
			svCount++;
			if (svCount % 4 == 0) printf("\n");
		}
		while (svCount % 4 != 0) {
			printf("    |              |");
			svCount++;
		}
		if (svCount > 0 && (svCount-1) % 4 == 3) printf("\n");
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n\n");
	}
	for (SignalIndex = SIGNAL_INDEX_E1; SignalIndex <= SIGNAL_INDEX_E6; SignalIndex++)
	{
		if (!(OutputParam.FreqSelect[GalileoSystem] & (1 << SignalIndex)))
			continue;
		IfFreq = SignalCenterFreq[2][SignalIndex] - OutputParam.CenterFreq * 1000;
		printf("Galileo %s with IF %+dkHz:\n", SignalName[2][SignalIndex], IfFreq / 1000);
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n");
		printf("| SV | Doppler (Hz) | SV | Doppler (Hz) | SV | Doppler (Hz) | SV | Doppler (Hz) |\n");
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n");
		
		int svCount = 0;
		for (i = 0; i < GalSatNumber; i++)
		{
			if (TotalChannelNumber >= TOTAL_SAT_CHANNEL)
				break;
			SatIfSignal[TotalChannelNumber] = new CSatIfSignal(OutputParam.SampleFreq, IfFreq, GalileoSystem, SignalIndex, GalEphVisible[i]->svid);
			SatIfSignal[TotalChannelNumber]->InitState(CurTime, &GalSatParam[GalEphVisible[i]->svid - 1], GetNavData(GalileoSystem, SignalIndex, NavBitArray));
			TotalChannelNumber++;
			
			if (svCount % 4 == 0) printf("|");
			printf(" %02d | %+12d |", GalEphVisible[i]->svid, (int)GetDoppler(&GalSatParam[GalEphVisible[i]->svid-1], SignalIndex));
			svCount++;
			if (svCount % 4 == 0) printf("\n");
		}
		while (svCount % 4 != 0) {
			printf("    |              |");
			svCount++;
		}
		if (svCount > 0 && (svCount-1) % 4 == 3) printf("\n");
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n\n");
	}
	for (SignalIndex = SIGNAL_INDEX_G1; SignalIndex <= SIGNAL_INDEX_G2; SignalIndex++)
	{
		if (!(OutputParam.FreqSelect[GlonassSystem] & (1 << SignalIndex)))
			continue;
		IfFreq = SignalCenterFreq[3][SignalIndex] - OutputParam.CenterFreq * 1000;
		printf("GLONASS %s with IF %+dkHz:\n", SignalName[3][SignalIndex], IfFreq / 1000);
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n");
		printf("| SV | Doppler (Hz) | SV | Doppler (Hz) | SV | Doppler (Hz) | SV | Doppler (Hz) |\n");
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n");

		int svCount = 0;
		for (i = 0; i < GloSatNumber; i++)
		{
			if (TotalChannelNumber >= TOTAL_SAT_CHANNEL)
				break;
			FdmaOffset = (SignalIndex == SIGNAL_INDEX_G1) ? GloEphVisible[i]->freq * 562500 : (SignalIndex == SIGNAL_INDEX_G2) ? GloEphVisible[i]->freq * 437500 : 0;
			SatIfSignal[TotalChannelNumber] = new CSatIfSignal(OutputParam.SampleFreq, IfFreq + FdmaOffset, GlonassSystem, SignalIndex, GloEphVisible[i]->n);
			SatIfSignal[TotalChannelNumber]->InitState(CurTime, &GloSatParam[GloEphVisible[i]->n - 1], GetNavData(GlonassSystem, SignalIndex, NavBitArray));
			TotalChannelNumber++;
			
			if (svCount % 4 == 0) printf("|");
			printf(" %02d | %+12d |", GloEphVisible[i]->n, (int)GetDoppler(&GloSatParam[GloEphVisible[i]->n-1], SignalIndex));
			svCount++;
			if (svCount % 4 == 0) printf("\n");
		}
		while (svCount % 4 != 0) {
			printf("    |              |");
			svCount++;
		}
		if (svCount > 0 && (svCount-1) % 4 == 3) printf("\n");
		printf("+----+--------------+----+--------------+----+--------------+----+--------------+\n\n\n");
	}

	NoiseArray = new complex_number[OutputParam.SampleFreq];
	QuantArray = new unsigned char[OutputParam.SampleFreq * 2];

	// Calculate total data size and setup progress tracking
	int totalDurationMs = (int)(Trajectory.GetTimeLength() * 1000);
	double bytesPerMs = OutputParam.SampleFreq * ((OutputParam.Format == OutputFormatIQ4) ? 1.0 : 2.0);
	double totalMB = (totalDurationMs * bytesPerMs) / (1024.0 * 1024.0);
	int length = 0;
	long long TotalClippedSamples = 0;
	long long TotalSamples = 0;
	double AGCGain = 1.0; // Автоматическая регулировка усиления
	
	printf("[INFO]\tStarting signal generation loop...\n");
	printf("[INFO]\tSignal Duration: %0.2f s\n", totalDurationMs/1000.0);
	printf("[INFO]\tSignal Size: %.2f MB\n", totalMB);
	printf("[INFO]\tSignal Data format: %s\n", (OutputParam.Format == OutputFormatIQ4) ? "IQ4" : "IQ8");
	printf("[INFO]\tSignal Center freq: %0.4f MHz\n", OutputParam.CenterFreq/1000.0);
	printf("[INFO]\tSignal Sample rate: %0.2f MHz\n\n", OutputParam.SampleFreq/1000.0);
	fflush(stdout);
	
	auto start_time = std::chrono::high_resolution_clock::now();
	
	while (!StepToNextMs())
	{
		// generate white noise using fast batch generation
		FastMath::GenerateNoiseBlock(NoiseArray, OutputParam.SampleFreq, 1.0);
		
		// Безопасная оптимизированная параллелизация
		#pragma omp parallel for schedule(dynamic)
		for (i = 0; i < TotalChannelNumber; i++)
		{
			if (SatIfSignal[i]) {
				SatIfSignal[i]->GetIfSample(CurTime);
			}
		}
		
		// Безопасная оптимизированная аккумуляция с AGC
		#pragma omp parallel for schedule(static) if(OutputParam.SampleFreq > 1000)
		for (j = 0; j < OutputParam.SampleFreq; j++)
		{
			complex_number sum = NoiseArray[j];
			for (int ch = 0; ch < TotalChannelNumber; ch++)
			{
				if (SatIfSignal[ch] && SatIfSignal[ch]->SampleArray) {
					sum += SatIfSignal[ch]->SampleArray[j];
				}
			}
			// Применяем AGC
			sum.real *= AGCGain;
			sum.imag *= AGCGain;
			NoiseArray[j] = sum;
		}
		int ClippedInBlock = 0;
		if (OutputParam.Format == OutputFormatIQ4)
		{
			QuantSamplesIQ4(NoiseArray, OutputParam.SampleFreq, QuantArray, ClippedInBlock);
			fwrite(QuantArray, sizeof(unsigned char), OutputParam.SampleFreq, IfFile);
		}
		else
		{
			QuantSamplesIQ8(NoiseArray, OutputParam.SampleFreq, QuantArray, ClippedInBlock);
			fwrite(QuantArray, sizeof(unsigned char) * 2, OutputParam.SampleFreq, IfFile);
		}
		
		// Обновляем статистику
		TotalClippedSamples += ClippedInBlock;
		TotalSamples += OutputParam.SampleFreq * 2; // I и Q компоненты
		
		// Адаптивная подстройка AGC каждые 100 мс
		if ((length % 100) == 0 && length > 0)
		{
			double ClippingRate = (double)TotalClippedSamples / TotalSamples;
			if (ClippingRate > 0.01) // Если больше 1% клиппинга
			{
				AGCGain *= 0.95; // Уменьшаем усиление на 5%
				printf("[WARNING]\tAGC: Clipping %.2f%%, reducing gain to %.3f\n", ClippingRate * 100, AGCGain);
				TotalClippedSamples = TotalSamples = 0;	// reset statistic
			}
			else if (ClippingRate < 0.001 && AGCGain < 1.0) // Если меньше 0.1% клиппинга
			{
				AGCGain *= 1.02; // Увеличиваем усиление на 2%
				if (AGCGain > 1.0) AGCGain = 1.0;
				printf("[WARNING]\tAGC: Clipping %.2f%%, increasing gain to %.3f\n", ClippingRate * 100, AGCGain);
				TotalClippedSamples = TotalSamples = 0;	// reset statistic
			}
		}

//		for (j = 0; j < OutputParam.SampleFreq; j ++)
//			printf("%f %f\n", NoiseArray[j].real, NoiseArray[j].imag);
		if (((++length) % 10) == 0)
		{
			auto current_time = std::chrono::high_resolution_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count();
			
			double percentage = (double)length / totalDurationMs * 100.0;
			double currentMB = (length * bytesPerMs) / (1024.0 * 1024.0);
			double mbPerSec = (elapsed > 0) ? (currentMB * 1000.0) / elapsed : 0.0;
			
			// Calculate estimated time remaining
			long etaMs = 0;
			if (percentage > 0 && elapsed > 0) {
				etaMs = (long)((elapsed * (100.0 - percentage)) / percentage);
			}
			
			// Progress bar with percentage in center
			int barWidth = 50;
			int progress = (int)(percentage * barWidth / 100.0);
			char progressStr[8];
			sprintf(progressStr, "%.1f%%", percentage);
			int progressStrLen = strlen(progressStr);
			int centerPos = (barWidth - progressStrLen) / 2;
			
			printf("\r[");
			for (int k = 0; k < barWidth; k++) {
				if (k >= centerPos && k < centerPos + progressStrLen) {
					printf("%c", progressStr[k - centerPos]);
				} else if (k < progress) {
					printf("=");
				} else if (k == progress && percentage < 100.0) {
					printf(">");
				} else if (percentage >= 100.0 && k < barWidth) {
					printf("=");
				} else {
					printf(" ");
				}
			}
			
			// Format ETA
			char etaStr[32];
			if (etaMs > 0) {
				int etaSeconds = (int)(etaMs / 1000);
				int etaMinutes = etaSeconds / 60;
				etaSeconds %= 60;
				if (etaMinutes > 0) {
					sprintf(etaStr, "ETA: %dm%02ds   ", etaMinutes, etaSeconds);
				} else {
					sprintf(etaStr, "ETA: %02ds   ", etaSeconds);
				}
			} else {
				strcpy(etaStr, "ETA: --   ");
			}
			
			printf("] %d/%d ms | %.2f/%.2f MB | %.2f MB/s | %s",
				   length, totalDurationMs, currentMB, totalMB, mbPerSec, etaStr);
			fflush(stdout);
		}
//		if (length == 2) break;
	}

	// Final progress bar update to ensure 100% is shown
	printf("\r[");
	for (int k = 0; k < 50; k++) {
		if (k >= 22 && k < 28) {
			printf("%c", "100.0%"[k - 22]);
		} else {
			printf("=");
		}
	}
	printf("] %d/%d ms | %.2f/%.2f MB | COMPLETED: --\n",
		   totalDurationMs, totalDurationMs, totalMB, totalMB); 
	
	auto end_time = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
	double finalMB = (length * bytesPerMs) / (1024.0 * 1024.0);
	double avgMbPerSec = (duration.count() > 0) ? (finalMB * 1000.0) / duration.count() : 0.0;

	printf("\n[INFO]\tIF Signal generation completed!\n");
	printf("------------------------------------------------------------------\n");
	printf("[INFO]\tTotal samples: %lld\n", TotalSamples);
	printf("[INFO]\tClipped samples: %lld (%.4f%%)\n", TotalClippedSamples, (double)TotalClippedSamples / TotalSamples * 100);
	printf("[INFO]\tFinal AGC gain: %.3f\n", AGCGain);
	if ((double)TotalClippedSamples / TotalSamples > 0.05)
	{
		printf("[WARNING]\tHigh clipping rate! Consider reducing initPower in JSON config.\n");
	}
	printf("[INFO]\tTotal time taken: %0.2f s\n", duration.count()/1000.0);
	printf("[INFO]\tData generated: %.2f MB\n", finalMB);
	printf("[INFO]\tAverage rate: %.2f MB/s\n", avgMbPerSec);
	printf("------------------------------------------------------------------\n");

	for (i = 0; i < TOTAL_SAT_CHANNEL; i ++)
		if (SatIfSignal[i]) delete SatIfSignal[i];
	for (i = 0; i < sizeof(NavBitArray) / sizeof(NavBit*); i++)
		if (NavBitArray[i]) delete NavBitArray[i];
	delete[] NoiseArray;
	delete[] QuantArray;
	fclose(IfFile);

	return 0;
}

void UpdateSatParamList(GNSS_TIME CurTime, KINEMATIC_INFO CurPos, int ListCount, PSIGNAL_POWER PowerList, PIONO_PARAM IonoParam)
{
	int i, index;
	LLA_POSITION PosLLA = EcefToLla(CurPos);
	int TotalSatNumber = 0;

	for (i = 0; i < GpsSatNumber; i ++)
	{
		index = GpsEphVisible[i]->svid - 1;
		GetSatelliteParam(CurPos, PosLLA, CurTime, GpsSystem, GpsEphVisible[i], IonoParam, &GpsSatParam[index]);
		GetSatelliteCN0(ListCount, PowerList, PowerControl.InitCN0, PowerControl.Adjust, &GpsSatParam[index]);
	}
	for (i = 0; i < BdsSatNumber; i ++)
	{
		index = BdsEphVisible[i]->svid - 1;
		GetSatelliteParam(CurPos, PosLLA, CurTime, BdsSystem, BdsEphVisible[i], IonoParam, &BdsSatParam[index]);
		GetSatelliteCN0(ListCount, PowerList, PowerControl.InitCN0, PowerControl.Adjust, &BdsSatParam[index]);
	}
	for (i = 0; i < GalSatNumber; i ++)
	{
		index = GalEphVisible[i]->svid - 1;
		GetSatelliteParam(CurPos, PosLLA, CurTime, GalileoSystem, GalEphVisible[i], IonoParam, &GalSatParam[index]);
		GetSatelliteCN0(ListCount, PowerList, PowerControl.InitCN0, PowerControl.Adjust, &GalSatParam[index]);
	}
	for (i = 0; i < GloSatNumber; i++)
	{
		index = GloEphVisible[i]->n - 1;
		GetSatelliteParam(CurPos, PosLLA, CurTime, GlonassSystem, (PGPS_EPHEMERIS)GloEphVisible[i], IonoParam, &GloSatParam[index]);
		GetSatelliteCN0(ListCount, PowerList, PowerControl.InitCN0, PowerControl.Adjust, &GloSatParam[index]);
	}
}

int StepToNextMs()
{
	KINEMATIC_INFO CurPos;
	int ListCount = 0;
	PSIGNAL_POWER PowerList = NULL;
//	UTC_TIME UtcTime;
//	GLONASS_TIME GlonassTime;

	if (!Trajectory.GetNextPosVelECEF(0.001, CurPos))
		return -1;

	// calculate new satellite parameter
	ListCount = PowerControl.GetPowerControlList(1, PowerList);
	if (++CurTime.MilliSeconds >= WEEK_MS)
	{
		CurTime.Week ++;
		CurTime.MilliSeconds -= WEEK_MS;
	}
	// Recalculate visible satellites at minute boundary for long simulations
	if ((CurTime.MilliSeconds % 60000) == 0)
	{
		UTC_TIME UtcTime = GpsTimeToUtc(CurTime);
		GLONASS_TIME GlonassTime = UtcToGlonassTime_Corrected(UtcTime);
		GpsSatNumber = (OutputParam.FreqSelect[GpsSystem]) ? GetVisibleSatellite(CurPos, CurTime, OutputParam, GpsSystem, GpsEph, TOTAL_GPS_SAT, GpsEphVisible) : 0;
		BdsSatNumber = (OutputParam.FreqSelect[BdsSystem]) ? GetVisibleSatellite(CurPos, CurTime, OutputParam, BdsSystem, BdsEph, TOTAL_BDS_SAT, BdsEphVisible) : 0;
		GalSatNumber = (OutputParam.FreqSelect[GalileoSystem]) ? GetVisibleSatellite(CurPos, CurTime, OutputParam, GalileoSystem, GalEph, TOTAL_GAL_SAT, GalEphVisible) : 0;
		GloSatNumber = (OutputParam.FreqSelect[GlonassSystem]) ? GetGlonassVisibleSatellite(CurPos, GlonassTime, OutputParam, GloEph, TOTAL_GLO_SAT, GloEphVisible) : 0;
	}
	UpdateSatParamList(CurTime, CurPos, ListCount, PowerList, NavData.GetGpsIono());
	return 0;
}

complex_number GenerateNoise(double Sigma)
{
	return FastMath::FastGaussianNoise(Sigma);
}

NavBit* GetNavData(GnssSystem SatSystem, int SatSignalIndex, NavBit* NavBitArray[])
{
	switch (SatSystem)
	{
	case GpsSystem:
		switch (SatSignalIndex)
		{
		case SIGNAL_INDEX_L1CA: return NavBitArray[DataBitLNav];
		case SIGNAL_INDEX_L1C:  return NavBitArray[DataBitCNav2];
		case SIGNAL_INDEX_L2C:  return NavBitArray[DataBitCNav];
		case SIGNAL_INDEX_L2P:  return NavBitArray[DataBitLNav];
		case SIGNAL_INDEX_L5:   return NavBitArray[DataBitCNav];
		default: return NavBitArray[DataBitLNav];
		}
		break;
	case BdsSystem:
		switch (SatSignalIndex)
		{
		case SIGNAL_INDEX_B1C: return NavBitArray[DataBitBCNav1];
		case SIGNAL_INDEX_B1I: return NavBitArray[DataBitD1D2];
		case SIGNAL_INDEX_B2I: return NavBitArray[DataBitD1D2];
		case SIGNAL_INDEX_B3I: return NavBitArray[DataBitD1D2];
		case SIGNAL_INDEX_B2a: return NavBitArray[DataBitBCNav2];
		case SIGNAL_INDEX_B2b: return NavBitArray[DataBitBCNav3];
		default: return NavBitArray[DataBitD1D2];
		}
		break;
	case GalileoSystem:
		switch (SatSignalIndex)
		{
		case SIGNAL_INDEX_E1:  return NavBitArray[DataBitINav];
		case SIGNAL_INDEX_E5a: return NavBitArray[DataBitFNav];
		case SIGNAL_INDEX_E5b: return NavBitArray[DataBitINav];
		case SIGNAL_INDEX_E5:  return NavBitArray[DataBitFNav];
		case SIGNAL_INDEX_E6:  return NavBitArray[DataBitINav]; // E6 uses I/NAV for now
		default: return NavBitArray[DataBitINav];
		}
		break;
	case GlonassSystem:
		switch (SatSignalIndex)
		{
		case SIGNAL_INDEX_G1: return NavBitArray[DataBitGNav];
		case SIGNAL_INDEX_G2: return NavBitArray[DataBitGNav];
		default: return NavBitArray[DataBitGNav];
		}
		break;
	default: return NavBitArray[DataBitLNav];
	}
}

void QuantSamplesIQ4(complex_number Samples[], int Length, unsigned char QuantSamples[], int& ClippedCount)
{
	int i;
	double Value;
	unsigned char QuantValue, QuantSample;
	ClippedCount = 0;

	for (i = 0; i < Length; i++)
	{
		Value = fabs(Samples[i].real);
		QuantValue = (int)(Value * QUANT_SCALE_IQ4);	// optimal quantization for sigma=1 noise
		if (QuantValue > 7)
		{
			QuantValue = 7;
			ClippedCount++;
		}
		QuantValue += ((Samples[i].real >= 0) ? 0 : (1 << 3));	// add sign bit as MSB
		QuantSample = QuantValue << 4;
		Value = fabs(Samples[i].imag);
		QuantValue = (int)(Value * QUANT_SCALE_IQ4);	// optimal quantization for sigma=1 noise
		if (QuantValue > 7)
		{
			QuantValue = 7;
			ClippedCount++;
		}
		QuantValue += ((Samples[i].imag >= 0) ? 0 : (1 << 3));	// add sign bit as MSB
		QuantSample |= QuantValue;
		QuantSamples[i] = QuantSample;
	}
}

void QuantSamplesIQ8(complex_number Samples[], int Length, unsigned char QuantSamples[], int& ClippedCount)
{
	int i;
	int QuantValue;
	ClippedCount = 0;

	for (i = 0; i < Length; i++)
	{
		double val_real = Samples[i].real * QUANT_SCALE_IQ8;
		if (val_real > 127.0) {
			QuantValue = 127;
			ClippedCount++;
		} else if (val_real < -128.0) {
			QuantValue = -128;
			ClippedCount++;
		} else {
			QuantValue = (int)val_real;
		}
		QuantSamples[i * 2] = (unsigned char)(QuantValue & 0xff);

		double val_imag = Samples[i].imag * QUANT_SCALE_IQ8;
		if (val_imag > 127.0) {
			QuantValue = 127;
			ClippedCount++;
		} else if (val_imag < -128.0) {
			QuantValue = -128;
			ClippedCount++;
		} else {
			QuantValue = (int)val_imag;
		}
		QuantSamples[i * 2 + 1] = (unsigned char)(QuantValue & 0xff);
	}
}
