#include <math.h>
#include <stdio.h>
#include <cstring>

#include "SatIfSignal.h"
#include "../inc/FastMath.h"

CSatIfSignal::CSatIfSignal(int MsSampleNumber, int SatIfFreq, GnssSystem SatSystem, int SatSignalIndex, unsigned char SatId) : SampleNumber(MsSampleNumber), IfFreq(SatIfFreq), System(SatSystem), SignalIndex(SatSignalIndex), Svid((int)SatId)
{
	SampleArray = new complex_number[SampleNumber];
	PrnSequence = new PrnGenerate(System, SignalIndex, Svid);
	
	// Validate PrnSequence creation
	if (!PrnSequence || !PrnSequence->Attribute || !PrnSequence->DataPrn) {
		// Handle invalid PRN generation
		if (PrnSequence) {
			delete PrnSequence;
			PrnSequence = NULL;
		}
		DataLength = 1;  // Set to safe default
		PilotLength = 1;
		SatParam = NULL;
		GlonassHalfCycle = ((IfFreq % 1000) != 0) ? 1 : 0;
		return;
	}
	
	SatParam = NULL;
	DataLength = PrnSequence->Attribute->DataPeriod * PrnSequence->Attribute->ChipRate;
	PilotLength = PrnSequence->Attribute->PilotPeriod * PrnSequence->Attribute->ChipRate;
	
	// Fix array sizes for specific signals
	if (System == GpsSystem && SignalIndex == SIGNAL_INDEX_L2C) {
		// GPS L2C actual array sizes from PrnGenerate.cpp
		DataLength = 10230;      // GetGoldCode(..., 10230, ...)
		PilotLength = 10230 * 75; // GetGoldCode(..., 10230*75, ...)
	}
	if (System == GpsSystem && SignalIndex == SIGNAL_INDEX_L2P) {
		// GPS L2P uses simplified P-code with fixed array size
		DataLength = 10230 * 2;  // GetSimplifiedPCode creates 20460 chips
		PilotLength = 1;         // L2P has no pilot signal
	}
	
	// Ensure positive lengths
	if (DataLength <= 0) DataLength = 1;
	if (PilotLength <= 0) PilotLength = 1;
	
	GlonassHalfCycle = ((IfFreq % 1000) != 0) ? 1 : 0;
}

CSatIfSignal::~CSatIfSignal()
{
	delete[] SampleArray;
	SampleArray = NULL;
	delete PrnSequence;
	PrnSequence = NULL;
}

void CSatIfSignal::InitState(GNSS_TIME CurTime, PSATELLITE_PARAM pSatParam, NavBit* pNavData)
{
	SatParam = pSatParam;
	if (!SatelliteSignal.SetSignalAttribute(System, SignalIndex, pNavData, Svid))
		SatelliteSignal.NavData = (NavBit*)0;	// if system/frequency and navigation data not match, set pointer to NULL
	StartCarrierPhase = GetCarrierPhase(SatParam, SignalIndex);
	SignalTime = StartTransmitTime = GetTransmitTime(CurTime, GetTravelTime(SatParam, SignalIndex));
	SatelliteSignal.GetSatelliteSignal(SignalTime, DataSignal, PilotSignal);
	HalfCycleFlag = 0;
}

void CSatIfSignal::GetIfSample(GNSS_TIME CurTime)
{
	int i, TransmitMsDiff;
	double CurPhase, PhaseStep, CurChip, CodeDiff, CodeStep;
	const PrnAttribute* CodeAttribute = PrnSequence->Attribute;
	complex_number IfSample;
	double Amp = pow(10, (SatParam->CN0 - 3000) / 1000.) / sqrt(SampleNumber);

	if (!SatParam)
		return;
	SignalTime = StartTransmitTime;
	SatelliteSignal.GetSatelliteSignal(SignalTime, DataSignal, PilotSignal);
	EndCarrierPhase = GetCarrierPhase(SatParam, SignalIndex);
	EndTransmitTime = GetTransmitTime(CurTime, GetTravelTime(SatParam, SignalIndex));

	// calculate start/end signal phase and phase step (actual local signal phase is negative ADR)
	PhaseStep = (StartCarrierPhase - EndCarrierPhase) / SampleNumber;
	PhaseStep += IfFreq / 1000. / SampleNumber;
	CurPhase = StartCarrierPhase - (int)StartCarrierPhase;
	CurPhase = 1 - CurPhase;	// carrier is fractional part of negative of travel time, equvalent to 1 minus positive fractional part
	StartCarrierPhase = EndCarrierPhase;
	if (GlonassHalfCycle)	// for GLONASS odd number FreqID, nominal IF result in half cycle toggle every 1ms
	{
		CurPhase += HalfCycleFlag ? 0.5 : 0.0;
		HalfCycleFlag = 1 - HalfCycleFlag;
	}

	// get PRN count for each sample
	TransmitMsDiff = EndTransmitTime.MilliSeconds - StartTransmitTime.MilliSeconds;
	if (TransmitMsDiff < 0)
		TransmitMsDiff += 86400000;
	CodeDiff = (TransmitMsDiff + EndTransmitTime.SubMilliSeconds - StartTransmitTime.SubMilliSeconds) * CodeAttribute->ChipRate;
	CodeStep = CodeDiff / SampleNumber;	// code increase between each sample
	CurChip = (StartTransmitTime.MilliSeconds % CodeAttribute->PilotPeriod + StartTransmitTime.SubMilliSeconds) * CodeAttribute->ChipRate;
	StartTransmitTime = EndTransmitTime;

	// Оптимизированная генерация с сохранением корректности
	for (i = 0; i < SampleNumber; i ++)
	{
		SampleArray[i] = GetPrnValue(CurChip, CodeStep) * GetRotateValue(CurPhase, PhaseStep) * Amp;
	}
}

complex_number CSatIfSignal::GetPrnValue(double& CurChip, double CodeStep)
{
	// Add comprehensive null checks
	if (!PrnSequence || !PrnSequence->Attribute) {
		CurChip += CodeStep;
		return complex_number(0, 0);  // Return zero signal for invalid sequence
	}
	
	const int ChipCount = (int)CurChip;
	int DataChip, PilotChip;
	complex_number PrnValue;
	const int IsBoc = (PrnSequence->Attribute->Attribute) & PRN_ATTRIBUTE_BOC;
	const int IsQmboc = (PrnSequence->Attribute->Attribute) & PRN_ATTRIBUTE_QMBOC;
	const int IsTmboc = (PrnSequence->Attribute->Attribute) & PRN_ATTRIBUTE_TMBOC;
	const int IsCboc = (PrnSequence->Attribute->Attribute) & PRN_ATTRIBUTE_CBOC;
	const int IsTdm = (PrnSequence->Attribute->Attribute) & PRN_ATTRIBUTE_TMD;

	// Validate DataLength to prevent division by zero
	if (DataLength <= 0) {
		CurChip += CodeStep;
		return complex_number(0, 0);
	}
	
	// Cache frequently used values
	const int* DataPrn = PrnSequence->DataPrn;
	const int* PilotPrn = PrnSequence->PilotPrn;
	
	// Special handling for GLONASS FDMA (G1/G2)
	// According to ICD GLONASS, the signal is formed by XOR of three components:
	// 1. PRN code (511 kbit/s)
	// 2. Navigation data (50 bit/s)
	// 3. Meander (100 Hz)
	if (System == GlonassSystem && (SignalIndex == SIGNAL_INDEX_G1 || SignalIndex == SIGNAL_INDEX_G2)) {
		// 1. PRN chip (511 кГц)
		DataChip = ChipCount % DataLength;
		int prnBit = (DataPrn && DataChip >= 0 && DataChip < DataLength && DataPrn[DataChip]) ? 1 : 0;
		
		// 2. Navigation data bit (50 Гц)
		// ВАРИАНТ B: Инвертируем интерпретацию навигационного бита
		// DataSignal.real > 0 означает логическую 1
		// DataSignal.real < 0 означает логический 0
		int navBit = (DataSignal.real > 0) ? 1 : 0;
		
		// 3. Meander (100 Гц, период 10 мс)
		// Вычисляем текущее время в миллисекундах
		int currentMs = SignalTime.MilliSeconds + (int)(CurChip / 511.0);
		// Синхронизируем меандр с границами 2-секундных строк ГЛОНАСС
		int stringStartMs = (currentMs / 2000) * 2000;
		int relativeMs = currentMs - stringStartMs;
		// ВАРИАНТ А: Инвертируем полярность меандра
		int meander = ((relativeMs / 10) % 2) ? 0 : 1;
		
		// 4. XOR всех трёх компонентов
		int modulatedBit = prnBit ^ navBit ^ meander;
		
		
		// 5. BPSK модуляция
		// Возвращаемся к нормальной BPSK модуляции
		// 0 → +1 (положительная фаза)
		// 1 → -1 (отрицательная фаза)
		PrnValue = complex_number(modulatedBit ? -1.0 : 1.0, 0.0);
		
		CurChip += CodeStep;
		return PrnValue;
	}
	
	// Handle Time Division Multiplexing (TDM) for L2C
	if (IsTdm) {
		// For L2C: transmit L2CM on even milliseconds, L2CL on odd milliseconds
		int currentMs = (int)(CurChip / PrnSequence->Attribute->ChipRate) % 2;
		
		if (currentMs == 0) {
			// Even millisecond - transmit L2CM (data channel)
			DataChip = ChipCount % DataLength;
			if (!DataPrn || DataChip < 0 || DataChip >= DataLength) {
				PrnValue = complex_number(0, 0);
			} else {
				PrnValue = DataSignal * (DataPrn[DataChip] ? -1.0 : 1.0);
			}
		} else {
			// Odd millisecond - transmit L2CL (pilot channel)
			PilotChip = ChipCount % PilotLength;
			if (!PilotPrn || PilotChip < 0 || PilotChip >= PilotLength) {
				PrnValue = complex_number(0, 0);
			} else {
				PrnValue = PilotSignal * (PilotPrn[PilotChip] ? -1.0 : 1.0);
			}
		}
		CurChip += CodeStep;
		return PrnValue;
	}
	
	// Normal processing for non-TDM signals
	DataChip = ChipCount % DataLength;
	if (IsBoc)
		DataChip >>= 1;  // Faster division by 2
		
	// Add bounds checking for DataPrn
	if (!DataPrn || DataChip < 0 || DataChip >= DataLength) {
		PrnValue = complex_number(0, 0);
	} else {
		PrnValue = DataSignal * (DataPrn[DataChip] ? -1.0 : 1.0);
		// Apply BOC to data channel (always BOC(1,1) for data)
		// For L1C/B1C, data channel uses only BOC(1,1), not TMBOC
		if (IsBoc && (ChipCount & 1))
			PrnValue *= -1.0;
	}
	
	// Enhanced pilot signal processing with bounds checking
	if (PilotPrn && PilotLength > 0) {
		PilotChip = ChipCount % PilotLength;
		if (IsBoc)
			PilotChip >>= 1;  // Faster division by 2
			
		// Add explicit bounds check for PilotChip after BOC adjustment
		if (PilotChip >= 0 && PilotChip < PilotLength) {
			complex_number pilotVal = PilotSignal * (PilotPrn[PilotChip] ? -1.0 : 1.0);
			
			// Apply TMBOC/QMBOC modulation for pilot channel if enabled
			if ((IsTmboc || IsQmboc) && IsBoc) {
				// TMBOC(6,1,4/33) implementation for GPS L1C pilot
				// QMBOC implementation for BeiDou B1C pilot
				// The pattern repeats every 33 spreading symbols (33 ms for L1C)
				// 4 out of 33 symbols use BOC(6,1), others use BOC(1,1)
				
				// Calculate position in the 33-symbol TMBOC pattern
				// Each L1C symbol is 10ms, so we need position within 330ms cycle
				int symbolPos = (SignalTime.MilliSeconds % 330) / 10;  // 0-32
				
				// GPS L1C standard specifies these 4 positions for BOC(6,1)
				// (1-indexed in standard: 2, 6, 8, 31 -> 0-indexed: 1, 5, 7, 30)
				if (symbolPos == 1 || symbolPos == 5 || symbolPos == 7 || symbolPos == 30) {
					// These 4 positions use BOC(6,1)
					// BOC(6,1) has 6 times higher subcarrier frequency than BOC(1,1)
					// One BOC(6,1) cycle = 1/6 chip, so 12 half-cycles per chip
					int subChipPos = ChipCount % 12;
					// BOC(6,1) square wave: high for 0-5, low for 6-11
					if (subChipPos >= 6)
						pilotVal *= -1.0;
				} else {
					// Regular BOC(1,1) for other 29 positions
					if (ChipCount & 1)
						pilotVal *= -1.0;
				}
				
				PrnValue += pilotVal;
			} else if (IsCboc && IsBoc) {
				// CBOC(6,1,1/11) modulation for Galileo E1 pilot channel
				// 10/11 of the time use BOC(1,1), 1/11 use BOC(6,1)
				// The pattern is deterministic based on chip position within the 4092-chip code
				int chipInCode = ChipCount % 4092;  // E1 code period is 4092 chips
				
				// CBOC pattern: every 11th chip uses BOC(6,1), others use BOC(1,1)
				// This gives exactly 1/11 ratio over the full code period
				if ((chipInCode % 11) == 0) {
					// BOC(6,1) modulation for this chip
					// 6 cycles per chip, so 12 half-cycles
					int boc6Phase = ChipCount % 12;
					if (boc6Phase >= 6)
						pilotVal *= -1.0;
				} else {
					// BOC(1,1) modulation for most chips
					if (ChipCount & 1)
						pilotVal *= -1.0;
				}
				PrnValue += pilotVal;
			} else {
				// Standard BOC(1,1) modulation for pilot
				if (IsBoc && (ChipCount & 1))
					pilotVal *= -1.0;
				PrnValue += pilotVal;
			}
		}
	}
	
	CurChip += CodeStep;
	// check whether go beyond next code period (pilot code period multiple of data code period, so only check data period)
	if (DataLength > 0 && (((int)CurChip) % DataLength) < DataChip)
	{
		SignalTime.MilliSeconds += PrnSequence->Attribute->DataPeriod;
		SatelliteSignal.GetSatelliteSignal(SignalTime, DataSignal, PilotSignal);
	}
	return PrnValue;
}

complex_number CSatIfSignal::GetRotateValue(double& CurPhase, double PhaseStep)
{
	complex_number Rotate = FastMath::FastRotate(CurPhase * PI2);
	CurPhase += PhaseStep;
	return Rotate;
}

void CSatIfSignal::GenerateSamplesVectorized(int SampleCount, double& CurChip, double CodeStep, double& CurPhase, double PhaseStep, double Amp)
{
	// МЕГАОПТИМИЗИРОВАННАЯ генерация для максимальной скорости
	if (!PrnSequence || !PrnSequence->DataPrn) {
		// Быстрое заполнение нулями
		memset(SampleArray, 0, SampleCount * sizeof(complex_number));
		CurChip += CodeStep * SampleCount;
		CurPhase += PhaseStep * SampleCount;
		return;
	}
	
	// Предвычисленные константы для ускорения
	const int* __restrict DataPrn = PrnSequence->DataPrn;
	const int* __restrict PilotPrn = PrnSequence->PilotPrn;
	const double DataReal = DataSignal.real;
	const double DataImag = DataSignal.imag;
	const double PilotReal = PilotSignal.real;
	const double PilotImag = PilotSignal.imag;
	const double PI2_local = PI2;
	
	// СУПЕРСКОРОСТНАЯ генерация без лишних проверок
	#pragma omp simd 
	for (int i = 0; i < SampleCount; i++) {
		// PRN индекс с минимальными вычислениями
		const int ChipCount = (int)(CurChip + i * CodeStep);
		const int DataChip = ChipCount % DataLength;
		
		// Быстрое получение PRN значения
		const double dataSign = DataPrn[DataChip] ? -1.0 : 1.0;
		double prnReal = DataReal * dataSign;
		double prnImag = DataImag * dataSign;
		
		// Pilot PRN только если есть
		if (PilotPrn && PilotLength > 0) {
			const int PilotChip = ChipCount % PilotLength;
			const double pilotSign = PilotPrn[PilotChip] ? -1.0 : 1.0;
			prnReal += PilotReal * pilotSign;
			prnImag += PilotImag * pilotSign;
		}
		
		// Быстрое фазовое вращение
		const double phase = (CurPhase + i * PhaseStep) * PI2_local;
		const complex_number rotation = FastMath::FastRotate(phase);
		
		// Финальный сигнал с inline умножением
		SampleArray[i].real = (prnReal * rotation.real - prnImag * rotation.imag) * Amp;
		SampleArray[i].imag = (prnReal * rotation.imag + prnImag * rotation.real) * Amp;
	}
	
	// Обновляем состояние
	CurChip += CodeStep * SampleCount;
	CurPhase += PhaseStep * SampleCount;
}
