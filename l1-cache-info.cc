#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cmath>

#define CACHE_FLUSH_SIZE (64 * 1024 * 1024) // 64 MB для гарантированного вытеснения

static volatile uint64_t* dummy = nullptr;
static volatile uint64_t* dummy2 = nullptr;

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
    const size_t stride = 64; // Шаг в 64 элемента (512 байт) для прохода по разным кэш-линиям
    
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
    prepare_cache();

    std::cout << std::fixed << std::setprecision(9);

    // Вытесняем кэш перед холодным чтением
    flush_cache();
    
    const double cold_diff = read_time(dummy, 0);
    
    // Еще раз вытесняем перед серией измерений
    flush_cache();
    
    // Делаем измерения с большим шагом по разным кэш-линиям
    const size_t elements = CACHE_FLUSH_SIZE / sizeof(uint64_t);
    for (int i = 0; i < 100; i++) {
        size_t index = (i * 16) % elements; // Большой шаг
        std::cout << "Cache[" << index << "]: diff = " << read_time(dummy, index) << "\n";
    }

    // Холодное чтение из второго массива (должен быть промах)
    flush_cache();
    const double cold_diff1 = read_time(dummy2, CACHE_FLUSH_SIZE / sizeof(uint64_t) - 1000);

    std::cout << "Cold Cache: diff = " << cold_diff << "\n";
    std::cout << "Cold Cache1: diff = " << cold_diff1 << "\n";

    free_cache();
    return 0;
}

int main() {
    func();
}