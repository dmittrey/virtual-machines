#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

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

    for (size_t step = 1; step < 1024; step *= 2) {
        prepare_cache(step);

        size_t idx = 0;
        uint64_t acc = 0;

        const auto start = std::chrono::steady_clock::now();
        acc += buffer[idx];
        idx = buffer[idx];
        do {
            // std::cout << idx << std::endl;
            acc += buffer[idx];
            idx = buffer[idx];
        } while (idx != 0);
        const auto end = std::chrono::steady_clock::now();

        const std::chrono::duration<double> diff = end - start;
        std::cout << "STRIDE: " << std::setw(5) << step << " COUNT: " << std::setw(7) << count << " TIME : " << std::setw(15) << diff.count() << std::endl;

        free_cache();
    }

    return 0;
}

int main() {
    func();
}