# Complete Navigation Messages Structure - Updated

This document provides a comprehensive overview of all navigation message structures implemented in the SignalSim project, including recent fixes and verifications.

## GPS Navigation Messages

### L1CA - LNAV
- **Message Structure**: 
  - Frame: 1500 bits (30 seconds)
  - 5 Subframes × 10 words × 30 bits
  - Parity: 6-bit Hamming code per word
- **Implementation**: `LNavBit.cpp`
- **Status**: ✅ Working correctly

### L1C - CNAV2
- **Message Structure**:
  - Frame: 1800 bits (18 seconds)
  - 3 Subframes with FEC encoding
  - Subframe 1: Time of Interval (TOI)
  - Subframe 2: Clock and ephemeris
  - Subframe 3: Variable content
- **Implementation**: `CNav2Bit.cpp`
- **Recent Fix**: ✅ Fixed uninitialized subframes causing garbage data
- **Status**: ✅ Working after initialization fix

### L2C - CNAV
- **Message Structure**:
  - Message: 300 bits (12 seconds)
  - 10-bit preamble + 290-bit message
  - Convolutional encoding (rate 1/2)
- **Implementation**: `CNavBit.cpp`
- **Status**: ✅ Working correctly

### L5 - CNAV
- **Message Structure**: Same as L2C
- **Implementation**: `CNavBit.cpp`
- **Status**: ✅ Working correctly

## BeiDou Navigation Messages

### B1I/B2I/B3I - D1/D2
- **Message Structure**:
  - D1: 1500 bits/frame (30 seconds)
  - D2: 600 bits/frame (6 seconds)
  - BCH encoding + interleaving
- **Implementation**: `D1D2NavBit.cpp`
- **Status**: ✅ Working correctly

### B1C - B-CNAV1
- **Message Structure**:
  - Frame: 1800 symbols (1.8 seconds)
  - LDPC encoding
  - 3 subframes with different content
- **Implementation**: `BCNav1Bit.cpp`
- **Recent Updates**: 
  - ✅ Added SetIonoUtc() function
  - ✅ Implemented page rotation for subframe 3
- **Status**: ✅ Working with ionosphere/UTC support

### B2a - B-CNAV2
- **Message Structure**:
  - Frame: 600 bits (3 seconds)
  - 24-bit preamble + 576-bit data
  - LDPC encoding (288→576 bits)
  - Message types: 10, 11, 30, 34 (cyclic)
- **Implementation**: `BCNav2Bit.cpp`
- **Verification**: ✅ Correct structure and modulation
- **Status**: ✅ Working correctly

### B2b - B-CNAV3
- **Message Structure**:
  - Frame: 1000 bits (1 second)
  - Different from B2a structure
- **Implementation**: `BCNav3Bit.cpp`
- **Status**: ✅ Working correctly

## Galileo Navigation Messages

### E1 - I/NAV
- **Message Structure**:
  - Page: 250 bits (2 seconds)
  - Even/odd pages with different content
  - Convolutional encoding + interleaving
- **Implementation**: `INavBit.cpp`
- **Recent Fix**: ✅ Fixed ephemeris word allocation for faster acquisition
- **CBOC Modulation**: ✅ Implemented CBOC(6,1,1/11) for pilot channel
- **Status**: ✅ Working with improved acquisition time

### E5a - F/NAV
- **Message Structure**:
  - Page: 250 bits (10 seconds)
  - CRC-24 protection
  - Convolutional encoding
- **Implementation**: `FNavBit.cpp`
- **Status**: ✅ Working correctly

### E5b - I/NAV
- **Message Structure**: Same as E1 I/NAV
- **Implementation**: `INavBit.cpp`
- **Status**: ✅ Working correctly

### E6 - C/NAV
- **Message Structure**: Reserved for future use
- **Status**: 🚧 Placeholder implementation

## GLONASS Navigation Messages

### G1/G2 - GNAV
- **Message Structure**:
  - String: 85 data bits + 115 auxiliary bits (2 seconds)
  - Differential encoding
  - Meander code transformation
  - Hamming code (85,77)
  - Time mark: 30 bits
- **Implementation**: `GNavBit.cpp`
- **Verification**: ✅ Correct structure and encoding
- **Status**: ✅ Working correctly

## Modulation Summary

### BOC Modulations
- **GPS L1C**: TMBOC(6,1,4/33) on pilot - ✅ Implemented
- **BeiDou B1C**: QMBOC (similar to TMBOC) - ✅ Implemented
- **Galileo E1**: CBOC(6,1,1/11) on pilot - ✅ Implemented

### BPSK Modulations
- **GPS L1CA, L5**: Standard BPSK
- **BeiDou B2a, B3I**: Standard BPSK
- **Galileo E5a, E5b**: Standard BPSK
- **GLONASS G1, G2**: Standard BPSK with FDMA

## Recent Improvements

1. **GPS L1C**: Fixed uninitialized navigation data structures
2. **Galileo E1**: 
   - Implemented proper CBOC modulation
   - Fixed ephemeris transmission for 10-second acquisition
3. **BeiDou B1C**: Added ionosphere and UTC parameter support
4. **General**: Added SVID range validation for all systems

## Testing Recommendations

1. **GPS L1C**: Verify with commercial receivers supporting L1C
2. **Galileo E1**: Check acquisition time is ~10 seconds
3. **BeiDou B2a**: Verify message cycle (60 seconds)
4. **GLONASS**: Test with different frequency channels

## Known Limitations

1. **Galileo E6**: Only placeholder implementation
2. **Ionosphere Models**: Limited to Klobuchar model conversion
3. **GLONASS**: No ionosphere parameter transmission

## File References

- Navigation bit generation: `src/*NavBit.cpp`
- Signal modulation: `src/SatelliteSignal.cpp`, `src/SatIfSignal.cpp`
- PRN generation: `src/PrnGenerate.cpp`
- Signal attributes: `inc/PrnGenerate.h`

Last updated: 2025-01-06