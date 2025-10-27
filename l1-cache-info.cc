#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

#define CACHE_FLUSH_SIZE (64 * 1024 * 1024) // 64 MB для гарантированного вытеснения

static volatile uint64_t* dummy = nullptr;
static volatile uint64_t* dummy2 = nullptr;

// Структура для хранения статистики по каждому типу измерения
struct MeasurementStats {
    std::string name;
    std::vector<double> values;
    
    double getAverage() const {
        if (values.empty()) return 0.0;
        double sum = 0.0;
        for (double val : values) {
            sum += val;
        }
        return sum / values.size();
    }
    
    double getStdDev() const {
        if (values.size() <= 1) return 0.0;
        double avg = getAverage();
        double sumSq = 0.0;
        for (double val : values) {
            double diff = val - avg;
            sumSq += diff * diff;
        }
        return std::sqrt(sumSq / (values.size() - 1));
    }
};

void prepare_cache() {
    dummy = (volatile uint64_t*)malloc(CACHE_FLUSH_SIZE);
    dummy2 = (volatile uint64_t*)malloc(CACHE_FLUSH_SIZE);
    
    // Обязательно инициализируем память
    for (size_t i = 0; i < CACHE_FLUSH_SIZE / sizeof(uint64_t); i++) {
        dummy[i] = i;
        dummy2[i] = i;
    }
}

void free_cache() {
    free((void*)dummy);
    free((void*)dummy2);
}

// Функция для вытеснения кэша
void flush_cache() {
    const size_t elements = CACHE_FLUSH_SIZE / sizeof(uint64_t);
    const size_t stride = 8; // Шаг в 8 элементов (64 байт) для прохода по разным кэш-линиям
    
    volatile uint64_t sum = 0;
    for (size_t i = 0; i < elements; i += stride) {
        sum += dummy[i];
    }
}

double read_time(volatile uint64_t* src, size_t idx) {
    volatile uint64_t read; // volatile обязательно!

    const auto start = std::chrono::steady_clock::now();
    read = src[idx];
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> diff = end - start;

    return diff.count();
}

int func() {
    std::cout << std::fixed << std::setprecision(9);
    
    // Векторы для хранения всех измерений
    MeasurementStats cold_L3, cold_L3_2, cold_L2, hot_L1, hot_L1_2, hot_L2, hot_L2_2, hot_L2_3, hot_L2_6, hot_L2_7;
    cold_L3.name = "Cold Cache enter L3";
    cold_L3_2.name = "Cold Cache1 enter L3";
    cold_L2.name = "Cold Cache2 enter L2";
    hot_L1.name = "Hot Cache L1";
    hot_L1_2.name = "Hot Cache1 L1";
    hot_L2.name = "Hot Cache2 L2";
    hot_L2_2.name = "Hot Cache3 L2";
    hot_L2_3.name = "Hot Cache4 L2";
    
    const int NUM_RUNS = 100;
    
    for (int run = 0; run < NUM_RUNS; ++run) {
        // Вытесняем кэш перед каждым набором измерений
        prepare_cache();
        
        // Выполняем все измерения для текущего прогона
        cold_L3.values.push_back(read_time(dummy, 0));
        cold_L3_2.values.push_back(read_time(dummy2, 0));
        cold_L2.values.push_back(read_time(dummy, 1000));
        
        // "Горячие" чтения - без вытеснения кэша между ними
        hot_L1.values.push_back(read_time(dummy2, 1));
        hot_L1_2.values.push_back(read_time(dummy2, 4));

        hot_L2.values.push_back(read_time(dummy2, 62));

        free_cache();
    }

    for (int run = 0; run < NUM_RUNS; ++run) {
        // Вытесняем кэш перед каждым набором измерений
        prepare_cache();
        
        // Выполняем все измерения для текущего прогона
        cold_L3.values.push_back(read_time(dummy, 0));
        cold_L3_2.values.push_back(read_time(dummy2, 0));
        cold_L2.values.push_back(read_time(dummy, 1000));
        
        // "Горячие" чтения - без вытеснения кэша между ними
        hot_L1.values.push_back(read_time(dummy2, 1));
        hot_L1_2.values.push_back(read_time(dummy2, 4));

        hot_L2_2.values.push_back(read_time(dummy2, 48));

        free_cache();
    }

    for (int run = 0; run < NUM_RUNS; ++run) {
        // Вытесняем кэш перед каждым набором измерений
        prepare_cache();
        
        // Выполняем все измерения для текущего прогона
        cold_L3.values.push_back(read_time(dummy, 0));
        cold_L3_2.values.push_back(read_time(dummy2, 0));
        cold_L2.values.push_back(read_time(dummy, 1000));
        
        // "Горячие" чтения - без вытеснения кэша между ними
        hot_L1.values.push_back(read_time(dummy2, 1));
        hot_L1_2.values.push_back(read_time(dummy2, 4));

        hot_L2_3.values.push_back(read_time(dummy2, 5));

        free_cache();
    }

    for (int run = 0; run < NUM_RUNS; ++run) {
        // Вытесняем кэш перед каждым набором измерений
        prepare_cache();
        
        // Выполняем все измерения для текущего прогона
        cold_L3.values.push_back(read_time(dummy, 0));
        cold_L3_2.values.push_back(read_time(dummy2, 0));
        cold_L2.values.push_back(read_time(dummy, 1000));
        
        // "Горячие" чтения - без вытеснения кэша между ними
        hot_L1.values.push_back(read_time(dummy2, 1));
        hot_L1_2.values.push_back(read_time(dummy2, 4));

        hot_L2_6.values.push_back(read_time(dummy2, 6));

        free_cache();
    }

    for (int run = 0; run < NUM_RUNS; ++run) {
        // Вытесняем кэш перед каждым набором измерений
        prepare_cache();
        
        // Выполняем все измерения для текущего прогона
        cold_L3.values.push_back(read_time(dummy, 0));
        cold_L3_2.values.push_back(read_time(dummy2, 0));
        cold_L2.values.push_back(read_time(dummy, 1000));
        
        // "Горячие" чтения - без вытеснения кэша между ними
        hot_L1.values.push_back(read_time(dummy2, 1));
        hot_L1_2.values.push_back(read_time(dummy2, 4));

        hot_L2_7.values.push_back(read_time(dummy2, 7));

        free_cache();
    }

    std::cout << "Cold Cache enter L3: diff = " << cold_L3.getAverage() << std::endl;
    std::cout << "Cold Cache1 enter L3: diff = " << cold_L3_2.getAverage() << std::endl;

    std::cout << "Cold Cache enter L2: diff = " << cold_L2.getAverage() << std::endl;

    std::cout << "Hot Cache[1] L1: diff = " << hot_L1.getAverage() << std::endl;
    std::cout << "Hot Cache[4] L1: diff = " << hot_L1_2.getAverage() << std::endl;
    std::cout << "Hot Cache[5] L1: diff = " << hot_L2_3.getAverage() << std::endl;
    std::cout << "Hot Cache[6] L1: diff = " << hot_L2_6.getAverage() << std::endl;
    std::cout << "Hot Cache[7] L1: diff = " << hot_L2_7.getAverage() << std::endl;
    std::cout << "Hot Cache[48] L2: diff = " << hot_L2_2.getAverage() << std::endl;
    std::cout << "Hot Cache[62] L2: diff = " << hot_L2.getAverage() << std::endl;
    return 0;
}

int main() {
    func();
}