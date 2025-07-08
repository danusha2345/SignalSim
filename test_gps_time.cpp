#include <stdio.h>
#include <math.h>

struct UTC_TIME {
    int Year;
    int Month;
    int Day;
    int Hour;
    int Minute;
    double Second;
};

struct GNSS_TIME {
    int Week;
    int MilliSeconds;
    double SubMilliSeconds;
};

struct GLONASS_TIME {
    int LeapYear;
    int Day;
    int MilliSeconds;
    double SubMilliSeconds;
};

static int DaysAcc[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

unsigned int InsertTime[] = { 46828800, 78364801, 109900802, 173059203, 252028804, 315187205,
                     346723206, 393984007, 425520008, 457056009, 504489610, 551750411,
                     599184012, 820108813, 914803214, 1025136015, 1119744016, 1167264017 };

int GetLeapSecond(unsigned int Seconds, int &LeapSecond)
{
    int len = sizeof(InsertTime) / sizeof(unsigned int), i;

    for (i = 0; i < len; i ++)
    {
        if (Seconds <= InsertTime[i])
        {
            LeapSecond = i;
            return (Seconds == InsertTime[i]);
        }
    }
    LeapSecond = len;
    return 0;
}

GLONASS_TIME UtcToGlonassTime(UTC_TIME UtcTime)
{
    int Years, Days;
    GLONASS_TIME time;
    double MilliSeconds = (UtcTime.Second * 1000);

    time.MilliSeconds = (((UtcTime.Hour * 60) + UtcTime.Minute) * 60000 + (int)MilliSeconds) + 10800000;
    time.SubMilliSeconds = MilliSeconds - (int)MilliSeconds;
    Years = UtcTime.Year - 1996;
    Days = DaysAcc[UtcTime.Month - 1] + UtcTime.Day - 1;
    
    // Handle leap year correctly
    int YearsInCycle = Years % 4;
    if (YearsInCycle == 0)
    {
        // First year of cycle is leap year
        if (Days >= 59)
            Days++;
    }
    else
    {
        // Not first year of cycle - add 1 for non-leap adjustment
        Days++;
        // Add days from previous years in cycle
        if (YearsInCycle >= 1) Days += 366;  // First year was leap
        if (YearsInCycle >= 2) Days += 365;  // Second year
        if (YearsInCycle >= 3) Days += 365;  // Third year
    }
    
    // Calculate total day number from epoch
    time.Day = Days + 1 + (Years / 4) * 1461;
    time.LeapYear = Years / 4;

    return time;
}

GNSS_TIME UtcToGpsTime(UTC_TIME UtcTime, int UseLeapSecond = 1)
{
    GLONASS_TIME GlonassTime;
    GNSS_TIME time;
    int TotalDays, LeapSecond = 0;
    int NextDay = (UtcTime.Hour == 0 && UtcTime.Minute == 0 && ((int)UtcTime.Second) == 0);
    int AtLeapSecond;
    unsigned int TotalSeconds, TempSeconds;

    GlonassTime = UtcToGlonassTime(UtcTime);
    TotalDays = (GlonassTime.LeapYear + 3) * (366 + 365 * 3) + GlonassTime.Day - 6;
    TotalSeconds = TempSeconds = TotalDays * 86400 + GlonassTime.MilliSeconds / 1000 - 10800;

    printf("Debug UtcToGpsTime:\n");
    printf("  Input UTC: %04d-%02d-%02d %02d:%02d:%06.3f\n", 
           UtcTime.Year, UtcTime.Month, UtcTime.Day, 
           UtcTime.Hour, UtcTime.Minute, UtcTime.Second);
    printf("  GlonassTime: LeapYear=%d, Day=%d, MilliSeconds=%d\n", 
           GlonassTime.LeapYear, GlonassTime.Day, GlonassTime.MilliSeconds);
    printf("  TotalDays from GPS epoch = %d\n", TotalDays);
    printf("  TotalSeconds before leap = %u\n", TotalSeconds);

    if (UseLeapSecond)
    {
        AtLeapSecond = GetLeapSecond(TempSeconds, LeapSecond);
        TempSeconds += LeapSecond;
        AtLeapSecond = GetLeapSecond(TempSeconds, LeapSecond);
        TotalSeconds += (AtLeapSecond && NextDay) ? (LeapSecond + 1) : LeapSecond;
        printf("  LeapSecond adjustment = %d\n", LeapSecond);
        printf("  TotalSeconds after leap = %u\n", TotalSeconds);
    }
    time.Week = TotalSeconds / 604800;
    time.MilliSeconds = TotalSeconds - time.Week * 604800;
    time.MilliSeconds = time.MilliSeconds * 1000 + GlonassTime.MilliSeconds % 1000;
    time.SubMilliSeconds = GlonassTime.SubMilliSeconds;

    printf("  GPS Week = %d\n", time.Week);
    printf("  GPS MilliSeconds in week = %d\n", time.MilliSeconds);
    printf("  GPS Seconds in week = %.3f\n", time.MilliSeconds / 1000.0);

    return time;
}

int main()
{
    UTC_TIME utc;
    GNSS_TIME gps;
    
    // Test date: 2025-06-05 10:05:30
    utc.Year = 2025;
    utc.Month = 6;
    utc.Day = 5;
    utc.Hour = 10;
    utc.Minute = 5;
    utc.Second = 30.0;
    
    printf("Testing UTC to GPS time conversion\n");
    printf("================================\n");
    
    gps = UtcToGpsTime(utc, 1);
    
    printf("\nFinal result:\n");
    printf("  GPS Week: %d\n", gps.Week);
    printf("  GPS Time of Week: %.3f seconds\n", gps.MilliSeconds / 1000.0);
    
    // Verify: June 5, 2025 is Thursday (day 4 of week, 0=Sunday)
    int dayOfWeek = (gps.MilliSeconds / 1000) / 86400;
    printf("  Day of week: %d (0=Sunday, 4=Thursday)\n", dayOfWeek);
    
    return 0;
}
