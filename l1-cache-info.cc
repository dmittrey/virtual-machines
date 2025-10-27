#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

#define BUFFER_SIZE (8 * 8 * 10000)
#define WORD_NUM (BUFFER_SIZE / sizeof(uint64_t))

static size_t STRIDE = 1;

static uint64_t* buffer = nullptr;

void prepare_cache(size_t N) {
    buffer = (uint64_t*)malloc(BUFFER_SIZE);
    
    // Инициализируем циклический буффер
    for (size_t i = 0; i < WORD_NUM; i += N) {
        buffer[i] = i + N;
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
        std::cout << "STRIDE: " << step << " TIME : " << diff.count() / (WORD_NUM / step) << std::endl;

        free_cache();
    }

    return 0;
}

int main() {
    func();
}