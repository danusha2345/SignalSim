# IFdataGen Standalone

IFdataGen - автономный генератор промежуточной частоты (IF) сигналов GNSS. Эта версия оптимизирована для простой сборки и использования без зависимостей от Visual Studio или CMake.

## Описание

IFdataGen генерирует реалистичные GNSS сигналы промежуточной частоты для тестирования приёмников и алгоритмов обработки сигналов. Поддерживаются системы GPS, GLONASS, Galileo и BeiDou.

### Поддерживаемые системы и сигналы:
- **GPS**: L1 C/A, L1C, L2C, L2P, L5
- **GLONASS**: G1, G2 (FDMA), G3/L3OC (CDMA)
- **Galileo**: E1, E5a, E5b, E6
- **BeiDou**: B1I, B1C, B2I, B2a, B2b, B3I

⚠️ **Примечание**: B1C может не детектироваться некоторыми приёмниками без наличия B1I. См. [документацию](docs/B1C_receiver_detection_issue.md).

## Требования

- C++ компилятор (g++ или совместимый)
- Make
- OpenMP (опционально, для параллельной обработки)

## Структура директорий

```
IFdataGen_standalone/
├── EphData/                    # RINEX эфемериды
│   └── rinex_v3_20251560000.rnx
├── IFdataGen/                  # Директория сборки
│   ├── IFdataGen              # Исполняемый файл
│   └── obj/                   # Объектные файлы
├── generated_files/            # Примеры сгенерированных файлов
│   ├── BDS_B1C_only_3min.C8
│   ├── GPS_BDS_GAL_L1_5MHz_3min.C8
│   └── ...
├── inc/                        # Заголовочные файлы
├── obj/                        # Временные объектные файлы
├── presets/                    # JSON конфигурации
│   ├── BDS_B1C_Only.json      # Только BeiDou B1C
│   ├── GPS_L5_only.json       # Только GPS L5
│   ├── GPS_BDS_GAL_L1_5MHz.json  # Мульти-GNSS L1
│   └── ...
└── src/                        # Исходный код
```

## Сборка

### Linux/Unix/MSYS2
```bash
make                    # Сборка с автоопределением OpenMP
make clean             # Очистка файлов сборки
make rebuild           # Полная пересборка
```

Makefile автоматически определяет наличие OpenMP и включает параллелизацию при возможности.

## Использование

### Базовый запуск
```bash
./IFdataGen/IFdataGen presets/GPS_BDS_GAL_L1_5MHz.json
```

### Примеры запуска с разными пресетами
```bash
# Только GPS L5 сигнал
./IFdataGen/IFdataGen presets/GPS_L5_only.json

# Мульти-GNSS L1 диапазон (GPS + BeiDou + Galileo)
./IFdataGen/IFdataGen presets/GPS_BDS_GAL_L1_B1I_18MHz.json

# Только ГЛОНАСС G1
./IFdataGen/IFdataGen presets/GLO_G1_only.json
```

## Доступные пресеты

### Одиночные системы
- `GPS_L5_only.json` - GPS L5 (1176.45 МГц)
- `BDS_B1I_Only.json` - BeiDou B1I (1561.098 МГц)
- `BDS_B1C_Only.json` - BeiDou B1C (1575.42 МГц) ⚠️
- `BDS_B1I_B1C_dual.json` - BeiDou B1I+B1C (рекомендуется)
- `GAL_E5a_only.json` - Galileo E5a (1176.45 МГц)
- `GLO_G1_only.json` - ГЛОНАСС G1 (1602 МГц, FDMA)
- `GLO_G3_only.json` - ГЛОНАСС G3/L3OC (1202.025 МГц, CDMA)

### Мульти-системные конфигурации
- `GPS_BDS_GAL_L1_5MHz.json` - L1 диапазон, 5 МГц полоса
- `GPS_BDS_GAL_L1_B1I_18MHz.json` - Расширенный L1 с B1I, 18 МГц
- `GPS_BDS_GAL_L5E5aB2a_24MHz.json` - L5/E5a/B2a диапазон
- `GPS_BDS_GAL_GLO_L1G1_46MHz.json` - Все системы L1+G1, 46 МГц

## Формат выходных данных

Генератор создаёт файлы `.C8` содержащие:
- 8-битные I/Q отсчёты (signed char)
- Комплексный сигнал основной полосы
- Частота дискретизации согласно JSON конфигурации
- Формат: чередующиеся I и Q байты

### Пример структуры данных
```
[I0][Q0][I1][Q1][I2][Q2]...
```
где каждый I/Q компонент - знаковый 8-битный целочисленный тип (-128 до 127).

## Создание собственных конфигураций

Скопируйте любой пресет из папки `presets/` и модифицируйте параметры:

```json
{
    "output": {
        "type": "IFdata",
        "format": "IQ8",
        "sampleFreq": 15.0,      // МГц
        "centerFreq": 1568.259,  // МГц
        "name": "output.C8"
    },
    "systemSelect": {
        "GPS": ["L1CA", "L1C"],
        "BDS": ["B1I", "B1C"],
        "Galileo": ["E1"]
    }
}
```

## Производительность

- **CPU версия**: ~0.6x real-time для 59 каналов
- **С OpenMP**: улучшение до 2-3x на многоядерных процессорах
- Рекомендуется использовать SSD для записи больших файлов

## Примеры сгенерированных файлов

В папке `generated_files/` находятся готовые примеры для тестирования:
- 3-минутные записи различных конфигураций
- Разные частоты дискретизации (5-46 МГц)
- Одиночные и мульти-системные сигналы

## Параметры JSON конфигурации

### Основные секции:
- **time** - время начала симуляции (UTC)
- **trajectory** - траектория движения приёмника
- **ephemeris** - путь к файлу эфемерид RINEX
- **output** - параметры выходного файла
- **systemSelect** - выбор GNSS систем и сигналов
- **power** - настройки мощности сигналов

### Поддерживаемые сигналы:
- **GPS**: L1CA, L1C, L2C, L2P, L5
- **GLONASS**: G1, G2 (FDMA), G3/L3OC (CDMA)
- **Galileo**: E1, E5a, E5b, E6
- **BeiDou**: B1I, B1C, B2a, B2I, B2b, B3I

## Известные особенности

- **BeiDou B1C**: Многие приёмники (включая UM980) используют B1I-ассистированный захват B1C. Для надёжного тестирования рекомендуется генерировать B1I+B1C вместе.
- **ГЛОНАСС**: Использует FDMA, каждый спутник на своей частоте (FreqID от -7 до +6).

## Полезные ссылки

- [Документация по B1C](docs/B1C_receiver_detection_issue.md)
- [Настройка UM980 для B1C](docs/UM980_B1C_configuration.md)
- [Структура навигационных сообщений](docs/complete_gnss_navigation_messages_structure.md)
- [Реализация временной метки ГЛОНАСС](docs/GLONASS_Time_Marker_Implementation.md)
- [Полный аудит ГЛОНАСС](docs/GLONASS_Complete_Audit_Report.md)

## Лицензия

Смотрите файл LICENSE в корневой директории проекта.