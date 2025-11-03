#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <unordered_map>

extern void opaque(uint64_t);

const size_t VERIFY_ITER_COUNT = 10;

const size_t DEFAULT_TRANSITION_COUNT = 1024 * 1024;

#define MEASURE_TIME(var_name, code) \
    const auto start_##var_name = std::chrono::steady_clock::now(); \
    code \
    const auto end_##var_name = std::chrono::steady_clock::now(); \
    const double var_name = (end_##var_name - start_##var_name).count()

#define FORMAT_SIZE(buf_size) \
    ((buf_size) < 1024 ? std::to_string(buf_size) + " B" : \
     (buf_size) < 1024 * 1024 ? std::to_string((buf_size) / 1024) + " KB" : \
     std::to_string((buf_size) / (1024 * 1024)) + " MB")

/*
 * @property transitions_count - Кол-во переходов в буфере
 * @property stride - Который элемент следующий
 * stride = 4
 * 0-1-2-3-4-5-6-7-8
 * |       |       |
 * ------------------
 */
size_t* sequence_cyclic_buffer(size_t transitions_count, size_t stride) {
    // Создаем буффер чтобы вместить столько элементов сколько хотим переходов + компенсируем пропуски stride
    size_t buf_size = transitions_count * sizeof(size_t) * stride;

    size_t *buf = (size_t*)malloc(buf_size);
    
    // Инициализируем циклический буффер
    size_t word_cnt = buf_size / sizeof(size_t);
    for (size_t i = 0; i < word_cnt; i += stride) {
        buf[i] = i + stride;
    }
    // Замыкаем
    buf[word_cnt - stride] = 0;
    return buf;
}

static size_t cache_line_length(size_t capacity) {
    double prev_time_per_access = 0.0;
    const double THRESHOLD_PERCENT = 50.0; // порог для "яркого выброса"

    for (size_t stride = 1; stride < 2048; stride *= 2) {
        size_t *buf = sequence_cyclic_buffer(capacity / sizeof(size_t), stride);

        size_t acc = 0;

        // --- flush cache ---
        size_t cache_flush_size = capacity * 8;
        size_t *flush_buf = (size_t*)malloc(cache_flush_size);
        for (size_t i = 0; i < cache_flush_size / sizeof(size_t); ++i) {
            flush_buf[i] = i;
            opaque(flush_buf[i]);
        }
        size_t tmp = 0;
        for (size_t i = 0; i < cache_flush_size / sizeof(size_t); ++i)
            tmp += flush_buf[i];
        opaque(tmp);
        free(flush_buf);
        // --- end flush ---

        MEASURE_TIME(measure_time, {
            size_t idx = buf[0];
            do {
                idx = buf[idx];
                acc += idx;
            } while (idx != 0);
            opaque(acc);
        });

        double time_per_access = measure_time / (capacity / sizeof(size_t));

        std::cout << std::fixed << std::setprecision(16)
                  << "STRIDE: " << std::setw(5) << stride
                  << " TIME : " << std::setw(15) << time_per_access;

        if (prev_time_per_access > 0.0) {
            double percent_increase = ((time_per_access - prev_time_per_access) / prev_time_per_access) * 100.0;
            std::cout << " (+" << std::setw(6) << std::setprecision(2) << percent_increase << "%)";

            // Если первый значительный рост, возвращаем stride
            if (percent_increase >= THRESHOLD_PERCENT) {
                std::cout << std::endl;
                free(buf);
                return stride * sizeof(size_t);
            }
        } else {
            std::cout << " (base)";
        }

        std::cout << std::endl;
        prev_time_per_access = time_per_access;
        free(buf);
    }

    return 0; // не нашли выброса
}


static size_t round_to_pow2(size_t value) {
    size_t lower = 1;
    while (lower * 2 <= value)
        lower *= 2;
    size_t upper = lower * 2;
    // При равенстве дистанций выбираем меньший
    return (value - lower <= upper - value) ? lower : upper;
}

static size_t high_precise_cache_line_length(size_t capacity) {
    std::vector<size_t> results;

    for (int i = 0; i < VERIFY_ITER_COUNT * 100; i++)
        results.push_back(cache_line_length(capacity));

    std::unordered_map<size_t, size_t> freq;
    for (size_t r : results)
        freq[r]++;

    // Ищем наиболее часто встречающееся значение
    size_t mode_val = 0;
    size_t max_count = 0;
    for (auto& [val, count] : freq) {
        if (count > max_count) {
            max_count = count;
            mode_val = val;
        }
    }

    // Если распределение равномерное (все встречаются 1 раз), тогда усредняем
    bool all_unique = true;
    for (auto& [val, count] : freq) {
        if (count > 1) {
            all_unique = false;
            break;
        }
    }

    if (all_unique) {
        // fallback к средневзвешенному, если равномерное распределение
        double weighted_sum = 0.0;
        size_t total_count = 0;
        for (auto& [val, count] : freq) {
            weighted_sum += (double)val * count;
            total_count += count;
        }
        double weighted_avg = weighted_sum / total_count;
        return round_to_pow2((size_t)weighted_avg);
    } else {
        // иначе возвращаем моду (самое частое значение)
        return mode_val;
    }
}

size_t* prepare_random_cyclic_buffer(size_t elems_cnt, size_t stride) {
    size_t buf_size = elems_cnt * stride * sizeof(uint64_t);

    size_t *buf = (size_t*)aligned_alloc(buf_size, buf_size);
    
    // Создаем случайный паттерн обхода (держим в голове stride)
    std::vector<size_t> indices(elems_cnt);
    size_t cur_idx = 0;
    for (size_t i = 0; i < elems_cnt * stride; i += stride) {
        indices[cur_idx] = i;
        cur_idx++;
    }
    
    // Перемешиваем индексы
    for (size_t i = 0; i < elems_cnt; i++) {
        size_t j = rand() % (i + 1);
        std::swap(indices[i], indices[j]);
    }
    
    // Создаем циклический список со случайным порядком
    for (size_t i = 0; i < elems_cnt - 1; i++) {
        buf[indices[i]] = indices[i + 1];
    }
    buf[indices[elems_cnt - 1]] = indices[0];
    return buf;
}

static size_t cache_capacity() {
    double prev_time = 0.0;
    double max_increase = 0.0;
    size_t max_capacity = 1;

    for (size_t elems_cnt = 1; elems_cnt < (1 * 1024 * 1024) / sizeof(size_t); elems_cnt *= 2) {        
        size_t *buf = prepare_random_cyclic_buffer(elems_cnt, 1);

        size_t idx = 0;
        for (size_t i = 0; i < elems_cnt; i++) {
            idx = buf[idx];
            opaque(buf[idx]);
        }

        MEASURE_TIME(measure_time, {
            size_t idx = 0;
            opaque(buf[idx]);
            for (size_t i = 0; i < elems_cnt * 10000; i++) {
                idx = buf[idx];
                opaque(buf[idx]);
            }
        });

        // Нормализуем на количество доступов, получаем время на один доступ
        double time_per_access = measure_time / (elems_cnt * 10000);

        // Форматируем реальный размер буфера (учитываем stride)
        size_t buf_bytes = elems_cnt * sizeof(size_t);
        std::string size_str = FORMAT_SIZE(buf_bytes);

        std::cout << std::fixed << std::setprecision(16)
                  << "SIZE: " << std::setw(10) << size_str
                  << " TIME/ACCESS: " << std::setw(15) << time_per_access;

        // Сравнительные итерации
        if (prev_time) {
            // Сравнение с предыдущей итерацией
            double percent_increase = ((time_per_access - prev_time) / prev_time) * 100.0;
            std::cout << " (+" << std::setw(6) << std::setprecision(2) << percent_increase << "%)";
            std::cout << std::fixed << std::setprecision(16); // Восстанавливаем точность

            if (percent_increase > max_increase) {
                max_increase = percent_increase;
                max_capacity = buf_bytes / 2;
            }
        }

        if (!prev_time) {
            std::cout << " (base)";
        }

        std::cout << std::endl;
        free(buf);

        prev_time = time_per_access;
    }
    return max_capacity;
}

static size_t high_precise_capacity() {
    std::vector<size_t> results;

    for (int i = 0; i < VERIFY_ITER_COUNT; i++)
        results.push_back(cache_capacity());

    std::unordered_map<size_t, size_t> freq;
    for (size_t r : results)
        freq[r]++;

    // Ищем наиболее часто встречающееся значение
    size_t mode_val = 0;
    size_t max_count = 0;
    for (auto& [val, count] : freq) {
        if (count > max_count) {
            max_count = count;
            mode_val = val;
        }
    }

    // Если распределение равномерное (все встречаются 1 раз), тогда усредняем
    bool all_unique = true;
    for (auto& [val, count] : freq) {
        if (count > 1) {
            all_unique = false;
            break;
        }
    }

    if (all_unique) {
        // fallback к средневзвешенному, если равномерное распределение
        double weighted_sum = 0.0;
        size_t total_count = 0;
        for (auto& [val, count] : freq) {
            weighted_sum += (double)val * count;
            total_count += count;
        }
        double weighted_avg = weighted_sum / total_count;
        return round_to_pow2((size_t)weighted_avg);
    } else {
        // иначе возвращаем моду (самое частое значение)
        return mode_val;
    }
}

/*
Пусть у нас прямое отображение => если повторяется индекс и разный тэг, то доступ будет долгим из-за вытеснения

| tag(X бит) | index(9 бит) | offset(6 бит) |

После вычисления capacity и длины линейки мы точно знаем длину индекса и offset,
следовательно можем попробовать замер:
Estimate0:
    addr0 = | tag(y) | index(x)     | offset(-) |
    addr1 = | tag(y) | index(x + 1) | offset(-) |
    // Читаем 100'000 раз
Estimate1:
    addr0 = | tag(y)     | index(x) | offset(-) |
    addr1 = | tag(y + 1) | index(x) | offset(-) |
    // Читаем 100'000

Если у меня прямое отображение то ухудшение скорости доступа будет ощутимым
Иначе либо X-way assoc либо полно-ассоциативный кэш

хм, а если допустим просто наращивать шаг, то есть от 1 до 32
1 => полностью ассоциативный кэш (в set могут быть разные тэги)
2 - cache_lines_count - 1 => X - way
cache_lines_count => direct mapping
*/
static size_t compute_assoc(size_t cache_size, size_t line_size) {
    const size_t accesses = 200000;
    const size_t repeats = 10;
    std::vector<double> times;

    size_t stride = cache_size / sizeof(size_t); // шаг, чтобы все линии в одном set

    for (size_t ways = 1; ways <= 32; ways++) {
        size_t *buf = sequence_cyclic_buffer(ways, stride);

        // Погоняем по буферу чтобы прогреть кэш и нарваться на побольше конфликтов
        MEASURE_TIME(measure_time, {
            size_t idx = 0;
            for (size_t r = 0; r < repeats; ++r) {
                for (size_t i = 0; i < accesses; ++i) {
                    idx = buf[idx];
                    opaque(idx);
                }
            }
        });

        double time_per_access = (measure_time / (repeats * accesses));
        times.push_back(time_per_access);

        std::cout << std::fixed << std::setprecision(16)
                  << "WAYS: " << std::setw(10) << ways
                  << " TIME/ACCESS: " << std::setw(15) << time_per_access;

        if (ways > 1) {
            double prev = times[ways - 2];
            double delta = ((time_per_access - prev) / prev) * 100.0;
            std::cout << "  (" << std::setw(6) << delta << "%)";
            if (delta > 30.0) {
                std::cout << "  <-- likely conflict" << std::endl;
                std::cout << "\nEstimated cache associativity ≈ "
                          << (ways - 1) << "-way" << std::endl;
                free(buf);
                return ways - 1;
            }
        } else {
            std::cout << "  (base)";
        }

        std::cout << std::endl;
        free(buf);
    }

    std::cout << "\nAssociativity not detected up to 32-way" << std::endl;
    return 32;
}

static size_t high_precise_assoc(size_t cache_size, size_t line_size) {
    std::vector<size_t> results;

    for (int i = 0; i < VERIFY_ITER_COUNT; i++)
        results.push_back(compute_assoc(cache_size, line_size));

    std::unordered_map<size_t, size_t> freq;
    for (size_t r : results)
        freq[r]++;

    // Ищем наиболее часто встречающееся значение
    size_t mode_val = 0;
    size_t max_count = 0;
    for (auto& [val, count] : freq) {
        if (count > max_count) {
            max_count = count;
            mode_val = val;
        }
    }

    // Если распределение равномерное (все встречаются 1 раз), тогда усредняем
    bool all_unique = true;
    for (auto& [val, count] : freq) {
        if (count > 1) {
            all_unique = false;
            break;
        }
    }

    if (all_unique) {
        // fallback к средневзвешенному, если равномерное распределение
        double weighted_sum = 0.0;
        size_t total_count = 0;
        for (auto& [val, count] : freq) {
            weighted_sum += (double)val * count;
            total_count += count;
        }
        double weighted_avg = weighted_sum / total_count;
        return round_to_pow2((size_t)weighted_avg);
    } else {
        // иначе возвращаем моду (самое частое значение)
        return mode_val;
    }
}

int main() {
    size_t capacity = high_precise_capacity();
    // size_t capacity = 32768;
    size_t length_size = high_precise_cache_line_length(capacity);
    // size_t length_size = 64;
    size_t nways = high_precise_assoc(capacity, length_size);

    std::cout << "\n========== RESULTS ==========\n";
    std::cout << "CAPACITY:" << std::setw(20)
              << FORMAT_SIZE(capacity) << std::endl;
    std::cout << "CACHE LINE SIZE:" << std::setw(12)
              << FORMAT_SIZE(length_size) << std::endl;
    std::cout << "ASSOCIATIVE:" << std::setw(13)
              << nways << "-Ways" << std::endl;
}
