#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

extern void opaque(uint64_t);

#define DEFAULT_BUFFER_SIZE (8 * 8 * 10000)

static uint64_t* buffer = nullptr;

static size_t count = 0;

void prepare_cache(size_t N) {
    size_t BUFFER_SIZE = DEFAULT_BUFFER_SIZE * N;
    size_t WORD_NUM = BUFFER_SIZE / sizeof(uint64_t);

    buffer = (uint64_t*)malloc(BUFFER_SIZE);
    count = 0;
    
    // Инициализируем циклический буффер
    for (size_t i = 0; i < WORD_NUM; i += N) {
        buffer[i] = i + N;
        count++;
    }
    buffer[WORD_NUM - N] = 0;
}

void prepare_buffer1(size_t buf_size) {
    size_t WORD_NUM = buf_size / sizeof(uint64_t);


    buffer = (uint64_t*)malloc(buf_size);
    count = 0;
    
    // Создаем случайный паттерн обхода (перемешанный linked list)
    std::vector<size_t> indices(WORD_NUM);
    for (size_t i = 0; i < WORD_NUM; i++) {
        indices[i] = i;
    }
    
    // Перемешиваем индексы
    for (size_t i = WORD_NUM - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        std::swap(indices[i], indices[j]);
    }
    
    // Создаем циклический список со случайным порядком
    for (size_t i = 0; i < WORD_NUM - 1; i++) {
        buffer[indices[i]] = indices[i + 1];
    }
    buffer[indices[WORD_NUM - 1]] = indices[0];
}


void free_cache() {
    free((void*)buffer);
}


size_t calculate_coherence_size() {
    std::cout << std::fixed << std::setprecision(16);

    double prev_time = 0.0;
    bool first_iteration = true;

    size_t cache_size = 0;

    for (size_t step = 1; step < 1024; step *= 2) {
        prepare_cache(step);

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; i++) {
            size_t idx = 0;
            uint64_t acc = 0;
            acc += buffer[idx];
            idx = buffer[idx];
            do {
                acc += buffer[idx];
                idx = buffer[idx];
                opaque(acc);
            } while (idx != 0);
        }
        const auto end = std::chrono::steady_clock::now();

        const std::chrono::duration<double> diff = end - start;
        double current_time = diff.count();
        
        std::cout << "STRIDE: " << std::setw(5) << step 
                  << " COUNT: " << std::setw(7) << count 
                  << " TIME : " << std::setw(15) << current_time;
        
        if (!first_iteration) {
            double percent_increase = ((current_time - prev_time) / prev_time) * 100.0;
            if (percent_increase >= 50 && cache_size == 0) {
                cache_size = step * sizeof(uint64_t);
                std::cout << " (+" << std::setw(6) << std::setprecision(2) << percent_increase << "%)";
                std::cout << std::endl;
                free_cache();
                break;
            }
            std::cout << " (+" << std::setw(6) << std::setprecision(2) << percent_increase << "%)";
            std::cout << std::fixed << std::setprecision(16); // Восстанавливаем точность
        } else {
            std::cout << " (base)";
            first_iteration = false;
        }
        std::cout << std::endl;

        prev_time = current_time;
        free_cache();
    }

    return cache_size;
}

size_t func(size_t coherence_size) {
    double prev_time = 0.0;
    bool first_iteration = true;

    size_t boundary = 0;

    std::cout << std::fixed << std::setprecision(16);

    // Начинаем с размера кэш-линии и увеличиваем до нескольких МБ
    for (size_t buf_size = coherence_size; buf_size <= 32 * 1024 * 1024; buf_size *= 2) {
        size_t WORD_NUM = buf_size / sizeof(uint64_t);
        
        prepare_buffer1(buf_size);

        // Определяем количество итераций для достаточно долгого измерения
        size_t iterations = std::max(1000000UL, 10 * 1024 * 1024 / WORD_NUM);

        const auto start = std::chrono::steady_clock::now();
        
        size_t idx = 0;
        uint64_t acc = 0;
        for (size_t i = 0; i < iterations; i++) {
            acc += buffer[idx];
            idx = buffer[idx];
        }
        opaque(acc);
        
        const auto end = std::chrono::steady_clock::now();

        const std::chrono::duration<double> diff = end - start;
        // Нормализуем на количество доступов, получаем время на один доступ
        double time_per_access = diff.count() / iterations;

        // Форматируем размер буфера для вывода
        std::string size_str;
        if (buf_size < 1024) {
            size_str = std::to_string(buf_size) + " B";
        } else if (buf_size < 1024 * 1024) {
            size_str = std::to_string(buf_size / 1024) + " KB";
        } else {
            size_str = std::to_string(buf_size / (1024 * 1024)) + " MB";
        }

        std::cout << "SIZE: " << std::setw(10) << size_str
                  << " TIME/ACCESS: " << std::setw(15) << time_per_access;

        if (!first_iteration) {
            double percent_increase = ((time_per_access - prev_time) / prev_time) * 100.0;
            std::cout << " (+" << std::setw(7) << std::setprecision(2) << percent_increase << "%)";
            std::cout << std::fixed << std::setprecision(16);
            
            // Автоматическое определение границ кэшей
            if (percent_increase > 30.0) {
                std::cout << std::endl;
                free_cache();
                // If reach boundary => last step was legal
                return buf_size / 2;
            }
        } else {
            std::cout << " (base)";
            first_iteration = false;
        }
        std::cout << std::endl;

        prev_time = time_per_access;
        free_cache();
    }
    return 0;
}


int main() {
    size_t csize = calculate_coherence_size();
    // size_t csize = 64;
    std::cout << "COHERENCE CACHE SIZE: " << csize << std::endl;
    std::cout << std::endl;

    size_t bsize = func(csize);

    std::string size_str;
        if (bsize < 1024) {
            size_str = std::to_string(bsize) + " B";
        } else if (bsize < 1024 * 1024) {
            size_str = std::to_string(bsize / 1024) + " KB";
        } else {
            size_str = std::to_string(bsize / (1024 * 1024)) + " MB";
        }
    std::cout << "CAPACITY: " << std::setw(10) << size_str << std::endl;
}