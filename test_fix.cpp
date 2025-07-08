#include <stdio.h>

int main()
{
    // GPS epoch: Jan 6, 1980
    // Old GLONASS epoch: Jan 1, 1992 (12 years later = 3 * 4-year cycles)
    // New GLONASS epoch: Jan 1, 1996 (16 years later = 4 * 4-year cycles)
    
    printf("Epoch analysis:\n");
    printf("GPS epoch: January 6, 1980\n");
    printf("Old GLONASS epoch: January 1, 1992 (difference: 12 years = 3 * 4-year cycles)\n");
    printf("New GLONASS epoch: January 1, 1996 (difference: 16 years = 4 * 4-year cycles)\n");
    printf("\nThe formula (GlonassTime.LeapYear + 3) assumes 3 cycles difference\n");
    printf("But with 1996 epoch, it should be (GlonassTime.LeapYear + 4)\n");
    
    // Let's verify the day offset too
    // From Jan 1 to Jan 6 = 5 days
    // Plus 1 day because GLONASS counts from day 1, not day 0
    // Total offset = 6 days
    
    printf("\nDay offset:\n");
    printf("From Jan 1 to Jan 6 = 5 days\n");
    printf("Plus 1 for day numbering = 6 days\n");
    printf("The formula correctly uses '- 6' for day offset\n");
    
    return 0;
}
