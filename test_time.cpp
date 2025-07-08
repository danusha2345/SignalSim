#include <stdio.h>
#include <math.h>

// Test program to verify GPS time calculation
// Expected result for 2025-06-05 10:05:30 UTC:
// Day of week: Thursday (4th day from Sunday)
// Seconds in week: 4*86400 + 10*3600 + 5*60 + 30 = 345600 + 36000 + 300 + 30 = 381930

int main() {
    // Calculate days from GPS epoch (Jan 6, 1980) to June 5, 2025
    
    // Method 1: Direct calculation
    // From Jan 6, 1980 to Jan 6, 2025 = 45 years
    // 45 years = 11 four-year cycles + 1 year
    // 11 * 1461 + 365 = 16071 + 365 = 16436 days
    // From Jan 6 to June 5 = 150 days (31+28+31+30+31+5-6)
    int total_days_method1 = 16436 + 150;
    
    // Method 2: Using GLONASS intermediate calculation
    // From Jan 1, 1996 to June 5, 2025:
    // Years = 29, LeapYears = 29/4 = 7
    // Day of year for June 5 = 31+28+31+30+31+5 = 156
    // Days in GLONASS = 7*1461 + 1*365 + 156 = 10227 + 365 + 156 = 10748
    // From Jan 1, 1980 to Jan 1, 1996 = 16 years = 4*1461 = 5844 days
    // From Jan 1 to Jan 6 = 5 days
    int glonass_days = 10748;
    int total_days_method2 = glonass_days + 5844 - 5;
    
    printf("Method 1 (direct): %d days\n", total_days_method1);
    printf("Method 2 (via GLONASS): %d days\n", total_days_method2);
    
    // Convert to GPS week and seconds
    int gps_week1 = total_days_method1 / 7;
    int day_of_week1 = total_days_method1 % 7;
    
    int gps_week2 = total_days_method2 / 7;
    int day_of_week2 = total_days_method2 % 7;
    
    printf("\nMethod 1: GPS week %d, day %d\n", gps_week1, day_of_week1);
    printf("Method 2: GPS week %d, day %d\n", gps_week2, day_of_week2);
    
    // Calculate seconds in week for 10:05:30
    int seconds_in_day = 10*3600 + 5*60 + 30;
    int seconds_in_week1 = day_of_week1 * 86400 + seconds_in_day;
    int seconds_in_week2 = day_of_week2 * 86400 + seconds_in_day;
    
    printf("\nSeconds in week:\n");
    printf("Method 1: %d\n", seconds_in_week1);
    printf("Method 2: %d\n", seconds_in_week2);
    
    // What the program currently calculates
    printf("\nProgram currently calculates: 468348 seconds\n");
    printf("Difference: %d seconds (%.1f days)\n", 468348 - seconds_in_week2, 
           (468348 - seconds_in_week2) / 86400.0);
    
    return 0;
}