# UHD SC8 TX Streaming Player

Высокопроизводительный плеер для передачи больших файлов с IQ-данными через USRP устройства.

## Требования

### Системные требования
- **ОС**: Linux (Ubuntu 20.04+, Debian 10+), Windows 10/11, macOS
- **Процессор**: x86_64, рекомендуется 4+ ядер для высоких скоростей
- **Память**: минимум 4 GB RAM, рекомендуется 8+ GB
- **Диск**: SSD рекомендуется для файлов >1 GB

### Программные зависимости

#### 1. UHD (USRP Hardware Driver) 4.8.0.0
```bash
# Ubuntu/Debian
sudo add-apt-repository ppa:ettusresearch/uhd
sudo apt update
sudo apt install libuhd-dev uhd-host

# Или сборка из исходников для версии 4.8.0.0
git clone https://github.com/EttusResearch/uhd.git
cd uhd
git checkout v4.8.0.0
cd host
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j4
sudo make install
sudo ldconfig
```

#### 2. Boost библиотеки
```bash
# Ubuntu/Debian
sudo apt install libboost-all-dev

# Минимально необходимые компоненты:
# - boost-program-options
# - boost-thread
# - boost-system
```

#### 3. Компилятор C++
```bash
# GCC 7+ или Clang 6+
sudo apt install build-essential cmake
```

#### 4. Драйверы USB (для B-серии)
```bash
# Установка правил udev
cd /usr/local/lib/uhd/utils
sudo cp uhd-usrp.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### Настройка после установки

#### 1. Загрузка образов FPGA
```bash
# Загрузить образы для всех устройств
sudo uhd_images_downloader

# Проверка версии UHD
uhd_config_info --version
```

#### 2. Проверка устройства
```bash
# Найти подключенные USRP
uhd_find_devices

# Проверить информацию об устройстве
uhd_usrp_probe
```

#### 3. Настройка сети для X310/N-серии
```bash
# Установить MTU 9000 для 10 GigE
sudo ip link set dev eth0 mtu 9000

# Настроить IP в той же подсети
sudo ip addr add 192.168.10.1/24 dev eth0
```

#### 4. Оптимизация производительности (Linux)
```bash
# Увеличить размер буферов
sudo sysctl -w net.core.rmem_max=50000000
sudo sysctl -w net.core.wmem_max=50000000

# Отключить CPU throttling
sudo cpupower frequency-set -g performance
```

## Возможности

- Поддержка формата SC8 (8-bit signed complex)
- Передача файлов любого размера с использованием двойной буферизации
- Автоматическое повторение файла для непрерывной передачи
- Мониторинг производительности в реальном времени
- Обработка underrun и оптимизация для USB/Ethernet соединений
- Поддержка всех USRP устройств через UHD

## Компиляция

```bash
mkdir build
cd build
cmake ..
make
```

## Использование

```bash
./uhd_sc8_tx_streaming [параметры]
```

## Параметры командной строки

| Параметр | Короткая форма | Описание | Значение по умолчанию |
|----------|----------------|----------|----------------------|
| `--help` | `-h` | Показать справку | - |
| `--args` | - | Аргументы UHD устройства | `type=b200` |
| `--file` | `-f` | Входной файл с SC8 IQ отсчётами | `input.sc8` |
| `--rate` | `-r` | Частота дискретизации в Гц | `46.5e6` (46.5 МГц) |
| `--master-clock` | `-m` | Тактовая частота в Гц (0 = авто) | `46.5e6` (46.5 МГц) |
| `--freq` | - | Центральная частота в Гц | `2.45e9` (2.45 ГГц) |
| `--gain` | `-g` | Усиление передатчика в дБ | `70.0` |
| `--buffer` | - | Размер буфера в МБ | `10` |
| `--chunk` | - | Размер чанка чтения файла в МБ | `100` |
| `--repeat` | - | Повторять файл непрерывно | `true` |
| `--verbose` | `-v` | Включить подробный вывод | `false` |

## Примеры использования

### Базовая передача сигнала GPS L1
```bash
./uhd_sc8_tx_streaming --file gps_l1_signal.sc8 --rate 4e6 --freq 1575.42e6 --gain 60
```

### Передача широкополосного сигнала с X310
```bash
./uhd_sc8_tx_streaming \
    --args "type=x300,addr=192.168.10.2" \
    --file wideband_100MHz.sc8 \
    --rate 100e6 \
    --master-clock 200e6 \
    --freq 2.4e9 \
    --gain 20 \
    --buffer 50 \
    --chunk 500
```

### Передача ГЛОНАСС L1 с B200mini
```bash
./uhd_sc8_tx_streaming \
    --args "type=b200" \
    --file glonass_l1.sc8 \
    --rate 16e6 \
    --master-clock 32e6 \
    --freq 1602e6 \
    --gain 70
```

### Тестирование с одиночным проходом
```bash
./uhd_sc8_tx_streaming \
    --file test_signal.sc8 \
    --rate 10e6 \
    --freq 433e6 \
    --repeat false \
    --verbose true
```

### Передача через USB 2.0 с оптимизацией
```bash
./uhd_sc8_tx_streaming \
    --args "type=b200,num_send_frames=128" \
    --file narrowband.sc8 \
    --rate 1e6 \
    --freq 868e6 \
    --buffer 5 \
    --chunk 50
```

### Использование с USRP N210 через Ethernet
```bash
./uhd_sc8_tx_streaming \
    --args "type=usrp2,addr=192.168.10.3" \
    --file data.sc8 \
    --rate 25e6 \
    --master-clock 100e6 \
    --freq 900e6 \
    --gain 15
```

## Формат файла SC8

SC8 (signed complex 8-bit) - формат представления IQ данных:
- Каждый отсчёт: 2 байта (1 байт I, 1 байт Q)
- Диапазон значений: -128 до +127
- Порядок байт: I0, Q0, I1, Q1, ...


## Мониторинг производительности

Во время работы плеер отображает:
```
[120 s] Rate: 46.50 Msps (avg: 46.48), Total: 22.32 GB, Temp: 45.2°C, Buf: 2/4, UR: 0
```

- **Time**: Время работы в секундах
- **Rate**: Текущая скорость передачи (мгновенная и средняя)
- **Total**: Общий объём переданных данных
- **Temp**: Температура устройства
- **Buf**: Заполненность буферов (полные/всего)
- **UR**: Количество underrun

## Управление

- **Ctrl+C** - корректная остановка передачи
- **SIGTERM** - завершение процесса

## Оптимизация производительности

### Для высоких скоростей (>30 Msps)
1. Используйте Ethernet подключение (X310, N210)
2. Увеличьте размеры буферов: `--buffer 50 --chunk 500`
3. Убедитесь, что master_clock кратна sample_rate

### Для USB соединений
1. USB 3.0 обязателен для скоростей >10 Msps
2. Используйте короткий качественный USB кабель
3. Подключайте напрямую к компьютеру (без хабов)

### Предотвращение underrun
1. Закройте ненужные приложения
2. Отключите энергосбережение CPU
3. Используйте SSD для файлов >1 GB
4. Запускайте с правами реального времени (Linux):
   ```bash
   sudo chrt -f 50 ./uhd_sc8_tx_streaming --file data.sc8
   ```

## Известные особенности

1. **B200/B210**: Максимальная скорость ~61.44 Msps через USB 3.0
2. **X310**: Поддерживает до 200 Msps через 10 GigE
3. **Температура**: При длительной передаче на максимальной мощности возможен перегрев

## Решение типичных проблем

### Ошибка "No devices found"
```bash
# Проверить подключение
lsusb | grep -i ettus  # для USB устройств
ip addr show           # для сетевых устройств

# Перезагрузить драйверы
sudo rmmod usb_uhd
sudo modprobe usb_uhd
```

### Ошибка "Failed to load FPGA image"
```bash
# Переустановить образы
sudo uhd_images_downloader -t b2xx  # для B200/B210
sudo uhd_images_downloader -t x3xx  # для X310
```

### Постоянные underrun
1. Проверьте загрузку CPU: `htop`
2. Проверьте скорость USB: `lsusb -t`
3. Для сети проверьте потери пакетов: `ping -c 1000 192.168.10.2`

### Компиляция на Windows (MSYS2)
```bash
# Установить зависимости
pacman -S mingw-w64-x86_64-uhd mingw-w64-x86_64-boost mingw-w64-x86_64-cmake

# Компиляция
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
```

## Лицензия

Код распространяется под лицензией проекта SignalSim.
