# Сравнение модуляции сигналов BeiDou B1I и B1C

## Анализ реализации модуляции в IFdataGen

### 1. B1I - BPSK модуляция

**Характеристики:**
- Использует простую BPSK модуляцию
- Частота: 1561.098 МГц
- Скорость чипов: 2.046 Mcps
- Период PRN кода: 1 мс (2046 чипов)
- Только data канал, без pilot

**Реализация в коде:**
```cpp
// PrnGenerate.cpp, строки 234-239
case SIGNAL_INDEX_B1I:
case SIGNAL_INDEX_B2I:
    DataPrn  = GetGoldCode(B1IPrnInit[Svid-1], 0x59f, 0x2aa, 0x7c1, 2046, 11, 2046);
    PilotPrn = NULL;  // Нет pilot канала
    Attribute = &PrnAttributes[5];  // {2046, 1, 1, 0} - без BOC
    break;
```

**Генерация сигнала (SatelliteSignal.cpp, строки 222-227):**
```cpp
case SIGNAL_INDEX_B1I:
    DataSignal = complex_number((double)DataBit, 0);  // Только реальная часть
    PilotSignal = complex_number(0, 0);  // Нет pilot
    break;
```

### 2. B1C - BOC(1,1) + QMBOC модуляция

**Характеристики:**
- Использует BOC(1,1) для data канала
- Использует QMBOC(6,1,4/33) для pilot канала
- Частота: 1575.42 МГц (та же, что и GPS L1)
- Скорость чипов: 1.023 Mcps (но с BOC удваивается до 2.046)
- Период PRN кода: 10 мс (10230 чипов)
- Имеет и data, и pilot каналы

**Реализация в коде:**
```cpp
// PrnGenerate.cpp, строки 245-249
case SIGNAL_INDEX_B1C: 
    DataPrn  = GetB1CWeil(B1CDataTruncation[Svid-1], B1CDataPhaseDiff[Svid-1]);
    PilotPrn = GetB1CWeil(B1CPilotTruncation[Svid-1], B1CPilotPhaseDiff[Svid-1]);
    Attribute = &PrnAttributes[1];  // {2046, 10, 10, PRN_ATTRIBUTE_BOC}
    break;
```

**Генерация сигнала (SatelliteSignal.cpp, строки 218-221):**
```cpp
case SIGNAL_INDEX_B1C: 
    DataSignal = complex_number(0, -DataBit * AMPLITUDE_1_4);     // I/Q = 0/-0.5
    PilotSignal = complex_number(PilotBit * AMPLITUDE_29_44, 0);  // I/Q = 0.812/0
    break;
```

### 3. BOC модуляция в SatIfSignal.cpp

**Обработка BOC (строки 118-154):**
```cpp
const int IsBoc = (PrnSequence->Attribute->Attribute) & PRN_ATTRIBUTE_BOC;

// Для BOC делим индекс чипа пополам
DataChip = ChipCount % DataLength;
if (IsBoc)
    DataChip >>= 1;  // Деление на 2

// Применение PRN
PrnValue = DataSignal * (DataPrn[DataChip] ? -1.0 : 1.0);

// Для BOC инвертируем сигнал на второй половине чипа
if (IsBoc && (ChipCount & 1))  // если нечетный индекс
    PrnValue *= -1.0;
```

### 4. Ключевые различия

| Параметр | B1I | B1C |
|----------|-----|-----|
| Модуляция | BPSK | BOC(1,1) + QMBOC |
| Частота | 1561.098 МГц | 1575.42 МГц |
| Скорость чипов | 2.046 Mcps | 1.023 Mcps (×2 с BOC) |
| Период кода | 1 мс | 10 мс |
| Data канал | Есть (100% мощности) | Есть (25% мощности) |
| Pilot канал | Нет | Есть (75% мощности) |
| Вторичный код | Нет | 1800 бит |
| PRN генерация | Gold код | Weil код |

### 5. QMBOC для pilot канала B1C

В текущей реализации QMBOC(6,1,4/33) **НЕ реализован полностью**. Код использует упрощенную BOC(1,1) модуляцию для обоих каналов:

1. Data канал: BOC(1,1) с амплитудой 1/4 (корректно)
2. Pilot канал: BOC(1,1) с амплитудой 29/44 (должен быть QMBOC)

**Что должно быть для QMBOC:**
- 29/33 времени: BOC(1,1)
- 4/33 времени: BOC(6,1)
- Переключение между компонентами синхронизировано с вторичным кодом

### 6. Рекомендации по улучшению

Для полной реализации QMBOC нужно:
1. Добавить генерацию BOC(6,1) компонента
2. Реализовать переключение между BOC(1,1) и BOC(6,1) на основе вторичного кода
3. Правильно масштабировать амплитуды компонентов

### 7. Пример использования

**Только B1I:**
```bash
./IFdataGen presets/BDS_B1I_Only.json
```

**Только B1C:**
```bash
./IFdataGen presets/BDS_B1C_Only.json
```

**Оба сигнала (требуется широкая полоса):**
```json
{
    "output": {
        "sampleFreq": 20.0,
        "centerFreq": 1568.259,
        ...
    }
}
```