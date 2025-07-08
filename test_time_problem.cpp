#include <stdio.h>
#include <math.h>

// Simplified test to understand the issue
int main()
{
    // Expected values for 2025-06-05 10:05:30 UTC
    // GPS epoch: January 6, 1980 00:00:00 UTC
    // Days from GPS epoch to 2025-06-05 = 16557 days
    // GPS Week should be: 16557 / 7 = 2365 weeks + 2 days
    // Time in week: Thursday (day 4) 10:05:30 = 4*86400 + 36330 = 381930 seconds
    
    printf("Manual calculation:\n");
    printf("Days from 1980-01-06 to 2025-06-05: 16557\n");
    printf("GPS Week: 16557 / 7 = 2365\n");
    printf("Day of week: 16557 %% 7 = %d (Thursday = 4)\n", 16557 % 7);
    printf("Time in day: 10:05:30 = 36330 seconds\n");
    printf("Total seconds in week: 4 * 86400 + 36330 = 381930\n");
    
    // The function calculated:
    // GPS Week: 3622
    // GPS Seconds: 36348
    
    printf("\nFunction calculated:\n");
    printf("GPS Week: 3622 (difference: %d weeks)\n", 3622 - 2365);
    printf("GPS Seconds: 36348\n");
    
    // Check if the base year issue causes this
    // GLONASS epoch changed from 1992 to 1996 = 4 years difference
    // But the calculation goes through GLONASS time which uses 1996 as base
    
    printf("\nAnalysis:\n");
    printf("Week difference: 3622 - 2365 = 1257 weeks = %.1f years\n", 1257 * 7 / 365.25);
    printf("This suggests the calculation has an offset of about 24 years\n");
    
    // Let's check the GLONASS intermediate calculation
    // From debug output: TotalDays from GPS epoch = 25354
    // Expected: 16557 days
    // Difference: 25354 - 16557 = 8797 days = 1256.7 weeks
    
    printf("\nGLONASS intermediate:\n");
    printf("TotalDays calculated: 25354\n");
    printf("TotalDays expected: 16557\n");
    printf("Difference: 25354 - 16557 = 8797 days = %.1f years\n", 8797 / 365.25);
    
    return 0;
}
