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

void free_cache() {
    free((void*)buffer);
}

float func() {
    std::cout << std::fixed << std::setprecision(16);

    double prev_time = 0.0;
    bool first_iteration = true;

    size_t cache_size = 0;

    for (size_t step = 1; step < 1024; step *= 2) {
        prepare_cache(step);

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; i++) {
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
            if (percent_increase >= 50 && cache_size == 0)
                cache_size = step * sizeof(uint64_t);
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

    std::cout << "COHERENCE CACHE SIZE: " << cache_size << std::endl;

    return 0;
}

int main() {
    func();
}