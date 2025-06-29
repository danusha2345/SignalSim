#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/stream.hpp>
#include <uhd/types/tune_request.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <complex>
#include <queue>
#include <atomic>
#include <csignal>
#include <chrono>
#include <cmath>
#include <deque>
#include <algorithm>
#include <cctype>

namespace po = boost::program_options;

class StreamingSC8Transmitter {
private:
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::tx_streamer::sptr tx_stream;
    
    // Двойная буферизация для непрерывной передачи
    enum { NUM_BUFFERS = 4 };
    
    struct Buffer {
        std::vector<std::complex<int8_t>> data;
        size_t valid_samples;
        Buffer(size_t size) : data(size), valid_samples(0) {}
    };
    
    std::queue<Buffer*> empty_buffers;
    std::queue<Buffer*> full_buffers;
    boost::mutex buffer_mutex;
    boost::condition_variable buffer_cond;
    
    // Члены класса
    std::string filename;
    size_t buffer_size;
    size_t chunk_size_mb;
    bool repeat;
    bool verbose;
    std::atomic<bool> stop_signal;
    std::atomic<size_t> total_samples_sent;
    std::atomic<size_t> underrun_count;
    std::atomic<size_t> file_loops;
    
    // Для расчёта скорости
    std::deque<std::pair<std::chrono::steady_clock::time_point, size_t>> rate_history;
    boost::mutex rate_mutex;
    
public:
    StreamingSC8Transmitter(const std::string& args, 
                           double master_clock,
                           double rate, 
                           double freq, 
                           double gain,
                           const std::string& file,
                           size_t buff_size,
                           size_t chunk_mb,
                           bool rep = true,
                           bool verb = false) 
        : filename(file), 
          buffer_size(buff_size), 
          chunk_size_mb(chunk_mb),
          repeat(rep), 
          verbose(verb),
          stop_signal(false), 
          total_samples_sent(0), 
          underrun_count(0),
          file_loops(0) {
        
        // Создаём строку аргументов с master_clock_rate
        std::string device_args = args;
        if (master_clock > 0) {
            device_args += str(boost::format(",master_clock_rate=%f") % master_clock);
        }
        
        // Добавляем оптимизации для USB
        device_args += ",num_send_frames=2000";      // Максимум буферов
        device_args += ",send_frame_size=16384";     // Увеличенный размер фрейма
        
        // Для широкой полосы увеличиваем буферы
        if (rate > 30e6) {
            device_args += ",send_buff_size=33554432";  // 32 MB буфер
        }
        
        if (verbose) {
            std::cout << "Device args: " << device_args << std::endl;
        }
        
        // Создаём устройство
        std::cout << "Creating USRP device..." << std::endl;
        usrp = uhd::usrp::multi_usrp::make(device_args);
        
        // Проверяем, загружена ли прошивка FPGA
        try {
            std::cout << "Checking FPGA firmware..." << std::endl;
            
            // Пытаемся получить информацию о тактовой частоте - если прошивка не загружена, это вызовет исключение
            usrp->get_master_clock_rate();
            
            // Дополнительная проверка через сенсоры
            std::vector<std::string> mb_sensors = usrp->get_mboard_sensor_names();
            bool fpga_loaded = false;
            for (const auto& sensor : mb_sensors) {
                if (sensor == "ref_locked" || sensor == "lo_locked") {
                    fpga_loaded = true;
                    break;
                }
            }
            
            if (!fpga_loaded && mb_sensors.empty()) {
                std::cout << "FPGA firmware may not be loaded properly. Attempting to reload..." << std::endl;
                
                // Пересоздаём устройство для перезагрузки прошивки
                usrp.reset();
                boost::this_thread::sleep(boost::posix_time::seconds(2));
                usrp = uhd::usrp::multi_usrp::make(device_args);
                boost::this_thread::sleep(boost::posix_time::seconds(1));
                
                std::cout << "FPGA firmware reloaded." << std::endl;
            } else {
                std::cout << "FPGA firmware is loaded and ready." << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cout << "FPGA firmware not loaded. Loading now..." << std::endl;
            
            // Ждём загрузки
            boost::this_thread::sleep(boost::posix_time::seconds(3));
            
            // Пересоздаём устройство
            usrp.reset();
            usrp = uhd::usrp::multi_usrp::make(device_args);
            
            std::cout << "FPGA firmware loaded." << std::endl;
        }
        
        // Настройка радио
        std::cout << "Setting TX rate to " << rate/1e6 << " Msps..." << std::endl;
        usrp->set_tx_rate(rate);
        
        std::cout << "Setting TX frequency to " << freq/1e6 << " MHz..." << std::endl;
        usrp->set_tx_freq(uhd::tune_request_t(freq));
        
        std::cout << "Setting TX gain to " << gain << " dB..." << std::endl;
        usrp->set_tx_gain(gain);
        
        // Создаём поток
        uhd::stream_args_t stream_args("sc8", "sc8");
        stream_args.args["underflow_policy"] = "next_burst";
        
        try {
            tx_stream = usrp->get_tx_stream(stream_args);
        } catch (const std::exception& e) {
            std::cerr << "Error creating TX stream: " << e.what() << std::endl;
            std::cerr << "Attempting device reset..." << std::endl;
            
            // Сброс и повторная инициализация
            usrp.reset();
            boost::this_thread::sleep(boost::posix_time::seconds(2));
            usrp = uhd::usrp::multi_usrp::make(device_args);
            boost::this_thread::sleep(boost::posix_time::seconds(2));
            
            // Повторная настройка
            usrp->set_tx_rate(rate);
            usrp->set_tx_freq(uhd::tune_request_t(freq));
            usrp->set_tx_gain(gain);
            
            // Повторная попытка создания потока
            tx_stream = usrp->get_tx_stream(stream_args);
            std::cout << "TX stream created after reset." << std::endl;
        }
        
        // Инициализация буферов
        for (size_t i = 0; i < NUM_BUFFERS; i++) {
            empty_buffers.push(new Buffer(buffer_size));
        }
        
        // Вывод информации о настройках
        std::cout << std::endl;
        std::cout << boost::format("Device: %s") % usrp->get_pp_string() << std::endl;
        std::cout << boost::format("Master Clock Rate: %f MHz") % (usrp->get_master_clock_rate()/1e6) << std::endl;
        std::cout << boost::format("Actual TX Rate: %f Msps") % (usrp->get_tx_rate()/1e6) << std::endl;
        std::cout << boost::format("Actual TX Freq: %f MHz") % (usrp->get_tx_freq()/1e6) << std::endl;
        std::cout << boost::format("Actual TX Gain: %f dB") % usrp->get_tx_gain() << std::endl;
        std::cout << boost::format("Buffer size: %lu samples (%.2f MB)") % buffer_size % (buffer_size * 2.0 / 1e6) << std::endl;
        std::cout << boost::format("Chunk size: %lu MB") % chunk_size_mb << std::endl;
        std::cout << std::endl;
        
        // Проверка соотношения master_clock / sample_rate
        double ratio = usrp->get_master_clock_rate() / usrp->get_tx_rate();
        if (std::fabs(ratio - std::round(ratio)) > 0.001) {
            std::cerr << "WARNING: Master clock rate / sample rate is not an integer!" << std::endl;
            std::cerr << boost::format("Ratio: %f") % ratio << std::endl;
            std::cerr << "This may cause performance issues!" << std::endl << std::endl;
        }
    }
    
    ~StreamingSC8Transmitter() {
        while (!empty_buffers.empty()) {
            delete empty_buffers.front();
            empty_buffers.pop();
        }
        while (!full_buffers.empty()) {
            delete full_buffers.front();
            full_buffers.pop();
        }
    }
    
    void start() {
        // Получаем информацию о файле
        std::ifstream test_file(filename, std::ios::binary | std::ios::ate);
        if (!test_file.is_open()) {
            std::cerr << "Error opening file: " << filename << std::endl;
            return;
        }
        size_t file_size = test_file.tellg();
        test_file.close();
        
        std::cout << boost::format("File size: %.2f GB (%lu bytes)") % (file_size / 1e9) % file_size << std::endl;
        std::cout << boost::format("File contains %.2f seconds of data at %.2f Msps") 
            % (file_size / 2.0 / usrp->get_tx_rate()) 
            % (usrp->get_tx_rate() / 1e6) << std::endl << std::endl;
        
        std::cout << "Starting streaming transmission..." << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl << std::endl;
        
        // Запускаем потоки
        boost::thread file_thread(&StreamingSC8Transmitter::file_reader_thread, this);
        boost::thread tx_thread(&StreamingSC8Transmitter::transmit_thread, this);
        
        // Поток для вывода статистики
        boost::thread stats_thread(&StreamingSC8Transmitter::stats_thread, this);
        
        // Ждём завершения
        file_thread.join();
        tx_thread.join();
        stop_signal = true;
        stats_thread.join();
        
        // Финальная статистика
        std::cout << std::endl;
        std::cout << boost::format("Total samples sent: %lu") % total_samples_sent.load() << std::endl;
        std::cout << boost::format("Total data sent: %.2f GB") % (total_samples_sent.load() * 2.0 / 1e9) << std::endl;
        std::cout << boost::format("File loops completed: %lu") % file_loops.load() << std::endl;
        std::cout << boost::format("Underruns: %lu") % underrun_count.load() << std::endl;
    }
    
    void stop() {
        stop_signal = true;
        buffer_cond.notify_all();
    }
    
private:
    void file_reader_thread() {
        // Размер чанка для чтения (в байтах)
        size_t chunk_size_bytes = chunk_size_mb * 1024 * 1024;
        // Убеждаемся, что размер чанка кратен размеру комплексного отсчёта
        chunk_size_bytes = (chunk_size_bytes / sizeof(std::complex<int8_t>)) * sizeof(std::complex<int8_t>);
        
        // Временный буфер для чтения из файла
        std::vector<char> read_buffer(chunk_size_bytes);
        
        // Открываем файл один раз
        std::ifstream infile(filename, std::ios::binary);
        if (!infile.is_open()) {
            std::cerr << "Error opening file: " << filename << std::endl;
            stop_signal = true;
            buffer_cond.notify_all();
            return;
        }
        
        while (!stop_signal) {
            // Получаем пустой буфер
            boost::unique_lock<boost::mutex> lock(buffer_mutex);
            while (empty_buffers.empty() && !stop_signal) {
                buffer_cond.wait(lock);
            }
            
            if (stop_signal) break;
            
            Buffer* buff = empty_buffers.front();
            empty_buffers.pop();
            lock.unlock();
            
            // Читаем данные из файла
            size_t bytes_to_read = std::min(chunk_size_bytes, 
                                           buff->data.size() * sizeof(std::complex<int8_t>));
            infile.read(read_buffer.data(), bytes_to_read);
            size_t bytes_read = infile.gcount();
            
            if (bytes_read == 0 || infile.eof()) {
                // Конец файла
                if (repeat) {
                    // Перематываем файл без закрытия
                    infile.clear();
                    infile.seekg(0, std::ios::beg);
                    file_loops++;
                    
                    // Читаем с начала файла
                    infile.read(read_buffer.data(), bytes_to_read);
                    bytes_read = infile.gcount();
                    
                    if (verbose) {
                        std::cout << boost::format("\nFile loop #%lu started") % file_loops.load() << std::endl;
                    }
                } else {
                    // Не repeat - завершаем
                    lock.lock();
                    empty_buffers.push(buff);
                    stop_signal = true;
                    buffer_cond.notify_all();
                    break;
                }
            }
            
            // Копируем данные в буфер
            if (bytes_read > 0) {
                size_t samples_read = bytes_read / sizeof(std::complex<int8_t>);
                std::memcpy(buff->data.data(), read_buffer.data(), bytes_read);
                buff->valid_samples = samples_read;
                
                // Добавляем в очередь полных буферов
                lock.lock();
                full_buffers.push(buff);
                buffer_cond.notify_all();
            } else {
                // Возвращаем пустой буфер
                lock.lock();
                empty_buffers.push(buff);
            }
        }
        
        infile.close();
    }
    
    void transmit_thread() {
        uhd::tx_metadata_t md;
        md.start_of_burst = false;
        md.end_of_burst = false;
        md.has_time_spec = false;
        
        bool first_packet_ever = true;  // Флаг для самого первого пакета
        
        // Пытаемся установить приоритет потока (может не работать без sudo)
        #ifdef __linux__
        try {
            struct sched_param sp;
            sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
            if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0) {
                if (verbose) {
                    std::cout << "Successfully set real-time thread priority" << std::endl;
                }
            }
        } catch(...) {
            // Игнорируем ошибки приоритета
        }
        #endif
        
        while (!stop_signal || !full_buffers.empty()) {
            // Получаем полный буфер
            boost::unique_lock<boost::mutex> lock(buffer_mutex);
            while (full_buffers.empty() && !stop_signal) {
                buffer_cond.wait(lock);
            }
            
            if (full_buffers.empty() && stop_signal) break;
            
            Buffer* buff = full_buffers.front();
            full_buffers.pop();
            lock.unlock();
            
            // Передаём данные
            size_t samples_sent = 0;
            while (samples_sent < buff->valid_samples && !stop_signal) {
                size_t to_send = std::min(
                    buff->valid_samples - samples_sent,
                    tx_stream->get_max_num_samps()
                );
                
                // start_of_burst только для самого первого пакета всей передачи
                if (first_packet_ever) {
                    md.start_of_burst = true;
                    first_packet_ever = false;
                } else {
                    md.start_of_burst = false;
                }
                
                size_t sent = tx_stream->send(
                    &buff->data[samples_sent],
                    to_send,
                    md,
                    0.1  // timeout
                );
                
                // sent == 0 не обязательно означает underrun
                // Проверяем async сообщения для реальных underruns
                uhd::async_metadata_t async_md;
                while (tx_stream->recv_async_msg(async_md, 0)) {
                    if (async_md.event_code == uhd::async_metadata_t::EVENT_CODE_UNDERFLOW ||
                        async_md.event_code == uhd::async_metadata_t::EVENT_CODE_UNDERFLOW_IN_PACKET) {
                        underrun_count++;
                        if (verbose) {
                            std::cerr << "U" << std::flush;
                        }
                    }
                }
                
                samples_sent += sent;
                
                // Обновляем статистику
                if (sent > 0) {
                    total_samples_sent += sent;
                    
                    // Добавляем в историю для расчёта скорости
                    boost::unique_lock<boost::mutex> rate_lock(rate_mutex);
                    rate_history.push_back(std::make_pair(std::chrono::steady_clock::now(), sent));
                    // Храним историю за последние 5 секунд
                    auto cutoff_time = std::chrono::steady_clock::now() - std::chrono::seconds(5);
                    while (!rate_history.empty() && rate_history.front().first < cutoff_time) {
                        rate_history.pop_front();
                    }
                }
            }
            
            // Возвращаем буфер
            lock.lock();
            empty_buffers.push(buff);
            buffer_cond.notify_all();
        }
        
        // Отправляем EOB
        md.start_of_burst = false;
        md.end_of_burst = true;
        tx_stream->send("", 0, md);
    }
    
    void stats_thread() {
        auto start_time = std::chrono::steady_clock::now();
        size_t last_total_samples = 0;
        auto last_time = start_time;
        
        // Для хранения истории температуры
        double last_temp = 0.0;
        bool has_temp_sensor = false;
        enum { TEMP_MBOARD, TEMP_RX, TEMP_TX } temp_sensor_type = TEMP_MBOARD;
        
        // Проверяем наличие датчика температуры
        std::string temp_sensor_name;
        try {
            // Сначала проверяем mboard сенсоры
            std::vector<std::string> mb_sensors = usrp->get_mboard_sensor_names();
            if (verbose) {
                std::cout << "Mboard sensors: ";
                for (const auto& s : mb_sensors) {
                    std::cout << s << " ";
                }
                std::cout << std::endl;
            }
            
            // Проверяем также RX сенсоры (температура может быть там)
            try {
                std::vector<std::string> rx_sensors = usrp->get_rx_sensor_names();
                if (verbose && !rx_sensors.empty()) {
                    std::cout << "RX sensors: ";
                    for (const auto& s : rx_sensors) {
                        std::cout << s << " ";
                    }
                    std::cout << std::endl;
                }
                
                // Ищем температуру в RX сенсорах
                for (const auto& sensor : rx_sensors) {
                    std::string sensor_lower = sensor;
                    std::transform(sensor_lower.begin(), sensor_lower.end(), sensor_lower.begin(), ::tolower);
                    if (sensor_lower.find("temp") != std::string::npos) {
                        has_temp_sensor = true;
                        temp_sensor_name = sensor;
                        temp_sensor_type = TEMP_RX;
                        if (verbose) {
                            std::cout << "Found RX temperature sensor: " << sensor << std::endl;
                        }
                        break;
                    }
                }
            } catch(...) {}
            
            // Проверяем TX сенсоры
            if (!has_temp_sensor) {
                try {
                    std::vector<std::string> tx_sensors = usrp->get_tx_sensor_names();
                    if (verbose && !tx_sensors.empty()) {
                        std::cout << "TX sensors: ";
                        for (const auto& s : tx_sensors) {
                            std::cout << s << " ";
                        }
                        std::cout << std::endl;
                    }
                    
                    // Ищем температуру в TX сенсорах
                    for (const auto& sensor : tx_sensors) {
                        std::string sensor_lower = sensor;
                        std::transform(sensor_lower.begin(), sensor_lower.end(), sensor_lower.begin(), ::tolower);
                        if (sensor_lower.find("temp") != std::string::npos) {
                            has_temp_sensor = true;
                            temp_sensor_name = sensor;
                            temp_sensor_type = TEMP_TX;
                            if (verbose) {
                                std::cout << "Found TX temperature sensor: " << sensor << std::endl;
                            }
                            break;
                        }
                    }
                } catch(...) {}
            }
            
            // Если всё ещё не нашли, ищем в mboard сенсорах
            if (!has_temp_sensor) {
                for (const auto& sensor : mb_sensors) {
                    std::string sensor_lower = sensor;
                    std::transform(sensor_lower.begin(), sensor_lower.end(), sensor_lower.begin(), ::tolower);
                    if (sensor_lower.find("temp") != std::string::npos) {
                        has_temp_sensor = true;
                        temp_sensor_name = sensor;
                        if (verbose) {
                            std::cout << "Found mboard temperature sensor: " << sensor << std::endl;
                        }
                        break;
                    }
                }
            }
            
            if (!has_temp_sensor && verbose) {
                std::cout << "No temperature sensor found on this device." << std::endl;
            }
        } catch(...) {}
        
        while (!stop_signal) {
            boost::this_thread::sleep(boost::posix_time::seconds(1));
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed_total = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            
            size_t current_total = total_samples_sent.load();
            
            // Расчёт средней скорости за всё время
            double avg_rate_msps = 0;
            if (elapsed_total > 0) {
                avg_rate_msps = static_cast<double>(current_total) / elapsed_total / 1e6;
            }
            
            // Расчёт мгновенной скорости из истории
            double instant_rate_msps = 0;
            {
                boost::unique_lock<boost::mutex> rate_lock(rate_mutex);
                if (!rate_history.empty()) {
                    // Суммируем все отсчёты за последние N секунд
                    size_t samples_in_window = 0;
                    for (const auto& entry : rate_history) {
                        samples_in_window += entry.second;
                    }
                    
                    // Вычисляем временной интервал
                    auto window_duration = rate_history.back().first - rate_history.front().first;
                    double window_seconds = std::chrono::duration<double>(window_duration).count();
                    
                    if (window_seconds > 0.1) {  // Минимум 0.1 секунды для стабильности
                        instant_rate_msps = samples_in_window / window_seconds / 1e6;
                    } else {
                        // Если окно слишком маленькое, используем простой расчёт
                        instant_rate_msps = (current_total - last_total_samples) / 1e6;
                    }
                }
            }
            
            // Получаем температуру каждые 5 секунд
            if (has_temp_sensor && elapsed_total % 5 == 0) {
                try {
                    // Читаем температуру в зависимости от типа сенсора
                    switch (temp_sensor_type) {
                        case TEMP_RX:
                            last_temp = usrp->get_rx_sensor(temp_sensor_name).to_real();
                            break;
                        case TEMP_TX:
                            last_temp = usrp->get_tx_sensor(temp_sensor_name).to_real();
                            break;
                        case TEMP_MBOARD:
                        default:
                            last_temp = usrp->get_mboard_sensor(temp_sensor_name).to_real();
                            break;
                    }
                } catch(const std::exception& e) {
                    if (verbose) {
                        std::cerr << "\nError reading temperature: " << e.what() << std::endl;
                    }
                }
            }
            
            // Проверка очереди буферов
            size_t full_buff_count = full_buffers.size();
            
            // Вывод статистики
            if (current_total > 0) {
                double gb_sent = current_total * 2.0 / 1e9;
                
                std::cout << boost::format("\r[%03d s] Rate: %5.2f Msps (avg: %5.2f), Total: %7.2f GB, Temp: %4.1f°C, Buf: %d/%d, UR: %lu") 
                    % elapsed_total
                    % instant_rate_msps
                    % avg_rate_msps
                    % gb_sent
                    % last_temp
                    % full_buff_count
                    % NUM_BUFFERS
                    % underrun_count.load()
                    << std::flush;
            }
            
            last_total_samples = current_total;
            last_time = now;
        }
    }
};

// Глобальный указатель для обработки сигналов
StreamingSC8Transmitter* g_transmitter = nullptr;

void signal_handler(int sig) {
    std::cout << "\n\nReceived signal " << sig << ", stopping..." << std::endl;
    if (g_transmitter) {
        g_transmitter->stop();
    }
}

int main(int argc, char* argv[]) {
    // Параметры
    double rate, freq, gain, master_clock;
    std::string filename, args;
    size_t buffer_mb, chunk_mb;
    bool repeat, verbose;
    
    // Парсер аргументов
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "Show this help message")
        ("args", po::value<std::string>(&args)->default_value("type=b200"), 
            "UHD device arguments")
        ("file,f", po::value<std::string>(&filename)->default_value("input.sc8"), 
            "Input file with SC8 IQ samples")
        ("rate,r", po::value<double>(&rate)->default_value(46.5e6), 
            "Sample rate in Hz")
        ("master-clock,m", po::value<double>(&master_clock)->default_value(46.5e6), 
            "Master clock rate in Hz (0 = auto)")
        ("freq", po::value<double>(&freq)->default_value(2.45e9), 
            "Center frequency in Hz")
        ("gain,g", po::value<double>(&gain)->default_value(70.0), 
            "TX gain in dB")
        ("buffer", po::value<size_t>(&buffer_mb)->default_value(10), 
            "Buffer size in MB")
        ("chunk", po::value<size_t>(&chunk_mb)->default_value(100), 
            "File read chunk size in MB")
        ("repeat", po::value<bool>(&repeat)->default_value(true), 
            "Repeat file continuously")
        ("verbose,v", po::value<bool>(&verbose)->default_value(false), 
            "Enable verbose output")
    ;
    
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    
    if (vm.count("help")) {
        std::cout << "UHD SC8 Streaming Transmitter for Large Files" << std::endl << std::endl;
        std::cout << desc << std::endl;
        std::cout << "Examples:" << std::endl;
        std::cout << "  " << argv[0] << " --file large_data.sc8 --rate 46.5e6 --freq 2.45e9" << std::endl;
        std::cout << "  " << argv[0] << " --file data.sc8 --chunk 200 --buffer 20" << std::endl;
        return 0;
    }
    
    // Проверка файла
    std::ifstream test_file(filename, std::ios::binary);
    if (!test_file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return 1;
    }
    test_file.close();
    
    // Конвертируем размер буфера в количество отсчётов
    size_t buffer_size = buffer_mb * 1024 * 1024 / sizeof(std::complex<int8_t>);
    
    try {
        // Создаём передатчик
        g_transmitter = new StreamingSC8Transmitter(
            args, master_clock, rate, freq, gain, 
            filename, buffer_size, chunk_mb, repeat, verbose
        );
        
        // Устанавливаем обработчики сигналов
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Запускаем передачу
        g_transmitter->start();
        
        // Очистка
        delete g_transmitter;
        g_transmitter = nullptr;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}