// Правильная реализация модуляции ГЛОНАСС с XOR
// Согласно ICD ГЛОНАСС, сигнал формируется сложением по модулю 2 трёх компонентов:
// 1. ПС дальномерный код (511 кбит/с) 
// 2. Навигационное сообщение (50 бит/с)
// 3. Вспомогательный меандр (100 Гц)

// === ИЗМЕНЕНИЕ 1: В SatelliteSignal.cpp ===
// Убрать умножение на меандр, передавать чистый бит данных

// Для ГЛОНАСС FDMA (строки ~340-352)
case SIGNAL_INDEX_G1 :
case SIGNAL_INDEX_G2 :
    // НЕ применяем меандр здесь! Просто передаём бит данных
    DataSignal = complex_number((double)DataBit, 0);
    PilotSignal = complex_number(0, 0);
    // Меандр будет применён в SatIfSignal через XOR
    break;

// === ИЗМЕНЕНИЕ 2: В SatIfSignal.cpp ===
// Реализовать правильное сложение по модулю 2

if (System == GlonassSystem && (SignalIndex == SIGNAL_INDEX_G1 || SignalIndex == SIGNAL_INDEX_G2)) 
{
    // 1. Получаем чип PRN кода (511 кГц)
    DataChip = ChipCount % DataLength;
    int prnBit = (DataPrn && DataChip >= 0 && DataChip < DataLength && DataPrn[DataChip]) ? 1 : 0;
    
    // 2. Получаем бит навигационных данных (50 Гц)
    // DataSignal.real > 0 означает бит 0, < 0 означает бит 1
    int navBit = (DataSignal.real > 0) ? 0 : 1;
    
    // 3. Вычисляем меандр для текущего момента времени (100 Гц)
    // Меандр имеет период 10 мс: 0-5мс = 0, 5-10мс = 1, 10-15мс = 0, и т.д.
    int currentMs = SignalTime.MilliSeconds + (int)(CurChip / PrnSequence->Attribute->ChipRate);
    int meander = ((currentMs / 10) % 2) ? 1 : 0;
    
    // 4. Сложение по модулю 2 всех трёх компонентов
    int modulatedBit = prnBit ^ navBit ^ meander;
    
    // 5. BPSK модуляция: 0 → +1, 1 → -1
    PrnValue = complex_number(modulatedBit ? -1.0 : 1.0, 0.0);
    
    CurChip += CodeStep;
    return PrnValue;
}

// === ПРОВЕРКА ПОЛЯРНОСТИ ===
// Если приёмники всё ещё не видят сигнал, попробовать инвертировать полярность:

// Вариант 1 - инвертировать меандр:
int meander = ((currentMs / 10) % 2) ? 0 : 1;  // поменять 1 и 0

// Вариант 2 - инвертировать финальную модуляцию:
PrnValue = complex_number(modulatedBit ? 1.0 : -1.0, 0.0);  // поменять знаки

// Вариант 3 - инвертировать интерпретацию навигационного бита:
int navBit = (DataSignal.real > 0) ? 1 : 0;  // поменять 0 и 1

// === ОТЛАДКА ===
// Для отладки можно добавить вывод:
if (ChipCount % 511000 == 0) {  // Каждую секунду
    printf("[GLONASS] PRN=%d, NAV=%d, MEANDER=%d, XOR=%d\n", 
           prnBit, navBit, meander, modulatedBit);
}

// === ВАЖНЫЕ ЗАМЕЧАНИЯ ===
// 1. Частота PRN для ГЛОНАСС = 511 кГц (0.511 Мчип/с)
// 2. Период меандра = 10 мс (100 Гц)
// 3. Навигационные данные = 50 бит/с (20 мс на бит)
// 4. Все три компонента складываются по модулю 2 (XOR)
// 5. НЕ использовать умножение для меандра!

// === ПРОВЕРКА ВРЕМЕННОЙ МЕТКИ ===
// Для временной метки также должно применяться XOR с меандром:

if (IsInTimeMarker) {
    // Временная метка (первые 300 мс каждой строки)
    int TimeMarkerBitIndex = StringPosition / 10;
    int timeMarkerBit = (TimeMarkerBits[TimeMarkerBitIndex] ? 1 : 0);
    
    // Меандр для временной метки
    int meander = ((TransmitTime.MilliSeconds / 10) % 2) ? 1 : 0;
    
    // XOR временной метки с меандром (без PRN для временной метки)
    int modulatedBit = timeMarkerBit ^ meander;
    
    // BPSK модуляция
    DataSignal = complex_number(modulatedBit ? -1.0 : 1.0, 0.0);
}