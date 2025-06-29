# Makefile для потокового SC8 передатчика

CXX = g++
CXXFLAGS = -std=c++14 -O3 -march=native -Wall -Wextra

UHD_INCLUDE = -I/usr/local/include
UHD_LIB_PATH = -L/usr/local/lib -Wl,-rpath,/usr/local/lib

LIBS = -luhd -lboost_system -lboost_thread -lboost_program_options -lpthread
LDFLAGS = $(UHD_LIB_PATH) $(LIBS)

# Имя исполняемого файла
TARGET = uhd_sc8_tx_stream

# Исходный файл
SOURCE = uhd_sc8_tx_streaming.cpp

# Правило по умолчанию
all: clean $(TARGET) check-libs

# Компиляция
$(TARGET): $(SOURCE)
	@echo "Компиляция с UHD 4.8.0..."
	$(CXX) $(CXXFLAGS) $(UHD_INCLUDE) -o $(TARGET) $(SOURCE) $(LDFLAGS)
	@echo "Компиляция завершена!"
	
# Проверка используемых библиотек
check-libs:
	@echo ""
	@echo "Проверка связанных библиотек:"
	@ldd $(TARGET) | grep -E "(libuhd|libboost)" | head -5
	@echo ""
	@echo "Версия UHD в бинарнике:"
	@strings $(TARGET) | grep -i "UHD_" | head -1 || echo "Версия не найдена в строках"
	@echo ""

# Отладочная версия
debug: CXXFLAGS = -std=c++14 -g -O0 -Wall -Wextra -DDEBUG
debug: clean $(TARGET)

# Очистка
clean:
	@rm -f $(TARGET)
	@echo "Очистка выполнена"


# Быстрый запуск с параметрами
run: $(TARGET)
	./$(TARGET) --file "/home/progs/GPS_BDS_GAL_GLO_L1G1_46MHz_3min.C8" \
		--rate 46.5e6 \
		--master-clock 46.5e6 \
		--freq 1582.2105e6 \
		--gain 45 \
		--buffer 20 \
		--chunk 100

.PHONY: all clean debug run