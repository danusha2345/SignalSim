# Рекомендации по исправлению модуляции ГЛОНАСС
## Дата: 14 января 2025

## Выявленные проблемы

### 1. ❌ КРИТИЧНО: Неправильная реализация модуляции
**Проблема:** Используется умножение для применения меандра вместо XOR
**Локация:** SatelliteSignal.cpp (строки 205, 350) и SatIfSignal.cpp (строка 151)

**Текущая реализация:**
```cpp
// SatelliteSignal.cpp
int meander = ((TransmitTime.MilliSeconds / 10) % 2) ? -1 : 1;
DataSignal *= meander;  // НЕПРАВИЛЬНО! Должен быть XOR

// SatIfSignal.cpp  
int dataBit = (DataSignal.real < 0) ? 1 : 0;  // Данные уже с меандром
int modulatedBit = prnBit ^ dataBit;  // Только 2 компонента вместо 3
```

### 2. ❌ Неполное сложение по модулю 2
**Проблема:** XOR применяется только к 2 компонентам вместо 3
**Причина:** Меандр применяется раньше через умножение

## Правильная реализация согласно ICD ГЛОНАСС

### Шаг 1: Изменить SatelliteSignal.cpp
```cpp
// Строки ~340-352
case SIGNAL_INDEX_G1 :
case SIGNAL_INDEX_G2 :
    // Для ГЛОНАСС не применяем меандр здесь!
    // Передаём чистый бит навигационных данных
    DataBit = (DataBits[BitNumber] ? -1 : 1);
    DataSignal = complex_number((double)DataBit, 0);
    PilotSignal = complex_number(0, 0);
    // Меандр будет применён через XOR в SatIfSignal.cpp
    break;
```

### Шаг 2: Изменить SatIfSignal.cpp
```cpp
// Строки ~144-160
if (System == GlonassSystem && (SignalIndex == SIGNAL_INDEX_G1 || SignalIndex == SIGNAL_INDEX_G2)) 
{
    // 1. PRN код (511 кГц)
    DataChip = ChipCount % DataLength;
    int prnBit = (DataPrn && DataChip >= 0 && DataChip < DataLength && DataPrn[DataChip]) ? 1 : 0;
    
    // 2. Навигационный бит (50 Гц)
    // DataSignal.real > 0 означает логический 0
    // DataSignal.real < 0 означает логическую 1
    int navBit = (DataSignal.real < 0) ? 1 : 0;
    
    // 3. Меандр (100 Гц, период 10 мс)
    // Вычисляем текущее время в миллисекундах
    int currentMs = SignalTime.MilliSeconds + (int)(CurChip / 511.0);
    int meander = ((currentMs / 10) % 2) ? 1 : 0;
    
    // 4. Сложение по модулю 2 (XOR) всех трёх компонентов
    int modulatedBit = prnBit ^ navBit ^ meander;
    
    // 5. BPSK модуляция
    // 0 → +1 (положительная фаза)
    // 1 → -1 (отрицательная фаза)
    PrnValue = complex_number(modulatedBit ? -1.0 : 1.0, 0.0);
    
    CurChip += CodeStep;
    return PrnValue;
}
```

### Шаг 3: Исправить обработку временной метки
В SatelliteSignal.cpp для временной метки также убрать умножение:
```cpp
// Строки ~195-208
if (StringPosition < 300)  // Временная метка
{
    if (StringPosition == 0) {
        GNavBit::GetTimeMarker(TimeMarkerBits);
    }
    
    IsInTimeMarker = true;
    int TimeMarkerBitIndex = StringPosition / 10;
    // Передаём чистый бит временной метки без меандра
    DataBit = (TimeMarkerBits[TimeMarkerBitIndex] ? -1 : 1);
    DataSignal = complex_number((double)DataBit, 0);
    PilotSignal = complex_number(0, 0);
    // Меандр будет применён в SatIfSignal через XOR
    
    return TRUE;
}
```

## Варианты полярности для тестирования

Если после исправлений приёмники всё ещё не видят сигнал, попробовать:

### Вариант A: Инвертировать меандр
```cpp
int meander = ((currentMs / 10) % 2) ? 0 : 1;  // Поменять местами 0 и 1
```

### Вариант B: Инвертировать интерпретацию навигационного бита
```cpp
int navBit = (DataSignal.real > 0) ? 1 : 0;  // Инверсия
```

### Вариант C: Инвертировать финальную модуляцию
```cpp
PrnValue = complex_number(modulatedBit ? 1.0 : -1.0, 0.0);  // Инверсия
```

## Отладка

Добавить временный вывод для проверки:
```cpp
if (ChipCount % 511000 == 0) {  // Каждую секунду
    printf("[GLONASS Debug] Time=%d ms, PRN=%d, NAV=%d, MEANDER=%d, XOR=%d\n", 
           currentMs, prnBit, navBit, meander, modulatedBit);
}
```

## Проверка корректности

1. **Частота меандра:** Должен меняться каждые 10 мс (0→1→0→1...)
2. **Синхронизация:** Границы меандра должны совпадать с границами миллисекунд
3. **XOR операция:** Все три компонента должны складываться по модулю 2
4. **Спектр:** В спектре сигнала должны быть видны линии от меандра 100 Гц

## Примечания

- Частота PRN для ГЛОНАСС = 511 кГц (0.511 Мчип/с)
- Навигационные данные = 50 бит/с (20 мс на бит)
- Меандр = 100 Гц (10 мс период)
- Все операции - побитовые (XOR), не арифметические (умножение)

## Заключение

Основная ошибка - использование умножения вместо XOR для применения меандра. Это нарушает структуру сигнала ГЛОНАСС и делает его нераспознаваемым для приёмников. После исправления сигнал должен соответствовать стандарту ICD ГЛОНАСС.