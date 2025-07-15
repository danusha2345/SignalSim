// Пример кода для добавления меандра в ГЛОНАСС
// Этот код показывает, что нужно добавить в SatIfSignal.cpp

// В GetPrnValue добавить для ГЛОНАСС:
complex_number CSatIfSignal::GetPrnValue(double& CurChip, double CodeStep)
{
    // ... существующий код ...
    
    // Для ГЛОНАСС нужно специальная обработка
    if (System == GlonassSystem && (SignalIndex == SIGNAL_INDEX_G1 || SignalIndex == SIGNAL_INDEX_G2))
    {
        // Получаем текущее время в миллисекундах
        int currentTimeMs = (int)(CurChip / PrnSequence->Attribute->ChipRate);
        
        // 1. ПС дальномерный код (511 кбит/с)
        DataChip = ChipCount % DataLength;
        int prnBit = DataPrn[DataChip] ? 1 : 0;
        
        // 2. Навигационные данные (уже есть в DataSignal)
        // DataSignal содержит текущий бит данных (+1 или -1)
        int dataBit = (DataSignal.real() > 0) ? 0 : 1;
        
        // 3. Меандр 100 Гц (период 10 мс)
        int meander = ((currentTimeMs / 10) % 2) ? 1 : 0;
        
        // Сложение по модулю 2 всех трёх компонентов
        int modulatedBit = prnBit ^ dataBit ^ meander;
        
        // BPSK модуляция
        PrnValue = complex_number(modulatedBit ? -1.0 : 1.0, 0.0);
        
        CurChip += CodeStep;
        return PrnValue;
    }
    
    // ... остальной код для других систем ...
}

// Альтернативный вариант - добавить меандр в SatelliteSignal.cpp
// при формировании DataSignal для ГЛОНАСС:

BOOL CSatelliteSignal::GetSatelliteSignal(GNSS_TIME TransmitTime, complex_number &DataSignal, complex_number &PilotSignal)
{
    // ... существующий код ...
    
    // Для ГЛОНАСС модифицировать DataSignal с учётом меандра
    if (SatSystem == GlonassSystem && (SatSignal == SIGNAL_INDEX_G1 || SatSignal == SIGNAL_INDEX_G2))
    {
        // Меандр 100 Гц
        int meander = ((TransmitTime.MilliSeconds / 10) % 2) ? 1 : -1;
        
        // Умножаем DataSignal на меандр
        DataSignal *= meander;
    }
    
    return TRUE;
}