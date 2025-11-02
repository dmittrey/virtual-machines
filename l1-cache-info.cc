#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <unordered_map>

extern void opaque(uint64_t);

#define VERIFY_ITER_COUNT 20

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

static size_t cache_line_length() {
    const size_t DEFAULT_TRANSITION_COUNT = 100 * 1000;
    double prev_time = 0.0;

    for (size_t stride = 1; stride < 2048; stride *= 2) {
        size_t *buf = sequence_cyclic_buffer(DEFAULT_TRANSITION_COUNT, stride);

        MEASURE_TIME(measure_time, {
            for (int i = 0; i < 1000; i++) {
                size_t idx = buf[0];
                opaque(buf[idx]);
                // buf[0] = idx;
                do {
                    idx = buf[idx];
                    opaque(buf[idx]);
                    // buf[idx] = idx;
                } while (idx != 0);
            }
        });
        
        std::cout << std::fixed << std::setprecision(16)
                  << "STRIDE: " << std::setw(5) << stride
                  << " TIME : " << std::setw(15) << measure_time / (DEFAULT_TRANSITION_COUNT * 1000);

        // Сравнительные итерации
        if (prev_time) {
            // Сравнение с первой итерацией
            double percent_increase = ((measure_time - prev_time) / prev_time) * 100.0;
            std::cout << " (+" << std::setw(6) << std::setprecision(2) << percent_increase << "%)";

            // В два раза вырос latency
            if (percent_increase >= 50) {
                std::cout << std::endl;
                free(buf);

                return stride * sizeof(size_t);
            }
        }
        
        // Первая итерация
        if (!prev_time) {
            std::cout << " (base)";
        }

        std::cout << std::endl;
        prev_time = measure_time;
        free(buf);
    }

    return 0;
}

static size_t round_to_pow2(size_t value) {
    size_t lower = 1;
    while (lower * 2 <= value)
        lower *= 2;
    size_t upper = lower * 2;
    // При равенстве дистанций выбираем меньший
    return (value - lower <= upper - value) ? lower : upper;
}

static size_t high_precise_cache_line_length() {
    std::vector<size_t> results;

    for (int i = 0; i < VERIFY_ITER_COUNT; i++)
        results.push_back(cache_line_length());

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

    size_t *buf = (size_t*)malloc(buf_size);
    
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

static size_t cache_capacity(size_t actual_stride) {
    double prev_time = 0.0;

    // Начинаем с размера кэш-линии и увеличиваем до 64 МБ(эвристика)
    for (size_t elems_cnt = 1; elems_cnt < 64 * 1024 * 1024; elems_cnt *= 2) {        
        size_t *buf = prepare_random_cyclic_buffer(elems_cnt, actual_stride);

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

        // Форматируем размер буфера для вывода
        std::string size_str = FORMAT_SIZE(elems_cnt * sizeof(size_t));

        std::cout << std::fixed << std::setprecision(16)
                  << "SIZE: " << std::setw(10) << size_str
                  << " TIME/ACCESS: " << std::setw(15) << time_per_access;

        // Сравнительные итерации
        if (prev_time) {
            // Сравнение с предыдущей итерацией
            double percent_increase = ((time_per_access - prev_time) / prev_time) * 100.0;
            std::cout << " (+" << std::setw(6) << std::setprecision(2) << percent_increase << "%)";
            std::cout << std::fixed << std::setprecision(16); // Восстанавливаем точность

            if (percent_increase >= 50) {
                std::cout << std::endl;
                free(buf);
                // If reach boundary => last step was legal
                return elems_cnt * sizeof(size_t);
            }
        }

        if (!prev_time) {
            std::cout << " (base)";
        }

        std::cout << std::endl;
        free(buf);

        prev_time = time_per_access;
    }
    return 0;
}

static size_t high_precise_capacity(size_t actual_stride) {
    std::vector<size_t> results;

    for (int i = 0; i < VERIFY_ITER_COUNT; i++)
        results.push_back(cache_capacity(actual_stride));

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
// static void compute_assoc(size_t line_size, size_t cache_size) {
//     std::cout << "\n========== ASSOCIATIVITY TEST ==========\n";
//     std::cout << std::fixed << std::setprecision(16);

//     // Условный максимум
//     const size_t MAX_ASSOC = 32;

//     const size_t BUF_SIZE = cache_size * 2;
//     buf = (uint64_t*)malloc(BUF_SIZE);
//     if (!buf) {
//         std::cerr << "Memory allocation failed\n";
//         return;
//     }

//     // Шаг выбираем равный размеру кэша: попадание в один и тот же set
//     const size_t STEP = cache_size / sizeof(uint64_t);

//     bool first_iteration = true;
//     double prev_time = 0.0;

//     for (size_t assoc = 1; assoc <= MAX_ASSOC; assoc++) {
//         // Подготавливаем паттерн: assoc конфликтующих адресов
//         for (size_t i = 0; i < assoc; i++) {
//             buf[i * STEP] = (i + 1) * STEP;
//         }
//         buf[(assoc - 1) * STEP] = 0; // замыкаем цикл

//         // Измеряем время обхода
//         MEASURE_TIME(current_time, {
//             size_t idx = 0;
//             for (int i = 0; i < 10000; i++) {
//                 idx = buf[idx];
//                 opaque(buf[idx]);
//             }
//         });

//         std::cout << "ASSOC: " << std::setw(2) << assoc
//                   << " TIME: " << std::setw(15) << current_time;

//         if (!first_iteration) {
//             double percent_increase = ((current_time - prev_time) / prev_time) * 100.0;
//             std::cout << " (+" << std::setw(6)
//                       << std::setprecision(2) << percent_increase << "%)";
//             std::cout << std::fixed << std::setprecision(16);

//             if (percent_increase >= 50.0) {
//                 std::cout << "\n==> Estimated associativity: "
//                           << (assoc - 1) << "-way\n";
//                 free(buf);
//                 return;
//             }
//         } else {
//             std::cout << " (base)";
//             first_iteration = false;
//         }

//         std::cout << std::endl;
//         prev_time = current_time;
//     }

//     std::cout << "\n==> Associativity >= " << MAX_ASSOC << "-way (not detected)\n";
//     free(buf);
// }

int main() {
    // size_t length_size = high_precise_cache_line_length();
    size_t length_size = 64;

    size_t capacity = high_precise_capacity(length_size / sizeof(size_t));

    std::cout << "\n========== RESULTS ==========\n";
    std::cout << "CAPACITY:" << std::setw(10)
              << FORMAT_SIZE(capacity) << std::endl;
    std::cout << "COHERENCE CACHE SIZE:" << std::setw(12)
              << FORMAT_SIZE(length_size) << std::endl;

    // compute_assoc(64, capacity);
}
