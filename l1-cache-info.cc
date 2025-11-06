#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <iomanip>

#define MEASURE_TIME(var_name, code) \
    const auto start_##var_name = std::chrono::steady_clock::now(); \
    code \
    const auto end_##var_name = std::chrono::steady_clock::now(); \
    double var_name = std::chrono::nanoseconds(end_##var_name - start_##var_name).count()

#define FORMAT_SIZE(buf_size) \
    ((buf_size) < 1024 ? std::to_string(buf_size) + " B" : \
     (buf_size) < 1024 * 1024 ? std::to_string((buf_size) / 1024) + " KB" : \
     std::to_string((buf_size) / (1024 * 1024)) + " MB")

void opaque(void *ptr) noexcept {
    asm volatile("" : "+rm"(ptr) : : "memory");
}

// 4KB
constexpr size_t max_page_size = size_t(4) * 1024 * 1024;

// Linked list
/*
Idk why, maybe prefetcher more smarter than me, I cannot see huge difference in latency
between accesses 48Kb and 64Kb. With this structure I do.
*/
struct Elem {
    Elem *next = nullptr;
};

struct Buffer {
    Elem *buf = nullptr;
    Elem start;
    size_t length = 0;

    ~Buffer() noexcept {
        free(buf);
    }
};

Buffer aligned_buffer(size_t size) {
    if (auto* aligned = static_cast<Elem*>(aligned_alloc(max_page_size, size))) {
        return {.buf = aligned, .start = {.next = aligned}, .length = size / sizeof(Elem)};
    } else {
        size_t aligned_size = ((size + max_page_size - 1) / max_page_size) * max_page_size * 2;
        Elem* buf = static_cast<Elem*>(malloc(aligned_size));
        Elem* start = reinterpret_cast<Elem*>(
            (reinterpret_cast<uintptr_t>(buf) + max_page_size - 1) & ~(max_page_size - 1)
        );
        return {.buf = buf, .start = {.next = start}, .length = size / sizeof(Elem)};
    }
}

std::vector<size_t> generate_random_permutation(std::mt19937_64 &rng, size_t elems_cnt) {
    std::vector<size_t> indices(elems_cnt);

    size_t cur_idx = 0;
    for (size_t i = 0; i < elems_cnt; ++i) {
        indices[cur_idx] = i;
        cur_idx++;
    }

    std::shuffle(indices.begin(), indices.end(), rng);

    return indices;
}

Buffer generate_suffle_contignous_buffer(
    std::mt19937_64 &rng,
    size_t buf_size,
    size_t length
) {
    auto abuf = aligned_buffer(buf_size);
    auto *start = abuf.start.next;
    auto *prev = &abuf.start;

    auto order = generate_random_permutation(rng, length);

    for (size_t i = 0; i < length; ++i) {
        // Кидаем next на элемент start + random offset
        prev->next = start + order[i];
        prev = prev->next;
    }

    prev->next = nullptr;
    abuf.length = length;

    return abuf;
}


Buffer generate_sparsed_contignous_buffer(size_t cache_capacity, size_t assoc) {

    auto block_interval = cache_capacity / sizeof(Elem);
    auto block_words = block_interval / assoc;

    // std::cout << cache_capacity << " " << block_words << std::endl;

    auto abuf = aligned_buffer(cache_capacity * assoc);
    auto *start = abuf.start.next;
    auto *prev = &abuf.start;

    // std::cout << start << " " << prev << std::endl;

    for (size_t word_idx = 0; word_idx < block_words; ++word_idx) {
        for (size_t block_idx = 0; block_idx < assoc; ++block_idx) { 
            prev->next = start + block_idx * block_interval + word_idx;
            // std::cout << prev->next << " " << start + block_idx * block_interval + word_idx << std::endl;
            prev = prev->next;
        }
    }

    prev->next = nullptr;
    abuf.length = block_words * assoc;

    return abuf;
}

inline void iterate_over_buffer(Buffer& buf) {
    auto *elem = buf.start.next;

    while (elem->next != nullptr) {
        elem = elem->next;
    }

    opaque(elem);
}

void scramble(std::mt19937_64 &rng, size_t cache_size, size_t multiplier = 8) {
    size_t size = multiplier * cache_size;
    auto abuf = generate_suffle_contignous_buffer(rng, size, size / sizeof(size_t));
    iterate_over_buffer(abuf);
}

std::pair<size_t, double> cache_capacity(std::mt19937_64 &rng, size_t iteration) {
    size_t min_cache_size = 1024; // 1 kiB.
    size_t max_cache_size = size_t(1) * 1024 * 1024; // 1 MiB.
    size_t measure_count = 32;

    std::cout << "\nIteration #" << iteration << "\n";

    while (true) {
        std::vector<std::pair<size_t, double>> avgs;
        // У нас есть какой-то estimate
        size_t cache_size = min_cache_size;

        while (cache_size <= max_cache_size) {
            //Чтобы учесть половинки
            size_t step = cache_size / 2;

            for (size_t n = 0; n < 2 && cache_size <= max_cache_size;
                 ++n, cache_size += step) {
                
                double avg = 0.0;
                for (size_t i = 0; i < measure_count; i++) {
                    auto cbuf = generate_suffle_contignous_buffer(rng, cache_size, cache_size / sizeof(Elem));

                    for (size_t i = 0; i < 4; ++i) {
                        iterate_over_buffer(cbuf);
                    }

                    MEASURE_TIME(result, {
                        for (size_t i = 0; i < 64; ++i) {
                            iterate_over_buffer(cbuf);
                        }
                    });
                    result /= double(cbuf.length * 64);

                    avg += result;
                };
                avg /= measure_count;

                std::cout << "Cache size: " << FORMAT_SIZE(cache_size) 
                          << " Latency: " << std::setprecision(5) << avg << " ns\n";
                avgs.emplace_back(cache_size, avg);
            }
        }

        // idk another way to aproximate throws at start KB
        constexpr size_t avg_window = 8;
        for (size_t i = avg_window; i + 2 < avgs.size(); ++i) {
            double moving_avg = 0;
            for (size_t j = i - avg_window; j < i; ++j)
                moving_avg += avgs[j].second;
            moving_avg /= avg_window;

            double next_jump = avgs[i + 1].second / moving_avg;
            double following_jump = avgs[i + 2].second / moving_avg;

            if (next_jump >= 1.15 && following_jump >= 1.2) {
                std::cout << "MEASURE: Cache size: " << FORMAT_SIZE(avgs[i].first) << " Latency: " << std::setprecision(5) << avgs[i].second << " ns\n";
                return avgs[i];
            }
        }

        std::cout << "Cannot determine cache capacity. Run again.\n\n";
    }
}

std::pair<size_t, double> high_precise_cache_capacity(std::mt19937_64& rng) {
    constexpr size_t test_runs_min = 5;
    constexpr size_t test_runs_max = 11;

    std::unordered_map<size_t, size_t> occurrences;
    std::pair<size_t, double> mode;
    size_t mode_frequency = 0;

    for (size_t iterations = 1; iterations <= test_runs_max; ++iterations) {
        std::pair<size_t, double> result =
            cache_capacity(rng, iterations);
        size_t key = result.first;
        size_t key_occurrences = ++occurrences[key];

        // Если больше чем в половине случаев и минимум итераций мы выполнили
        if (key_occurrences > iterations / 2 && iterations >= test_runs_min) {
            return result;
        }

        if (mode_frequency < key_occurrences) {
            mode_frequency = key_occurrences;
            mode = result;
        }
    }

    // Самый частый результат
    return mode;
}

size_t cache_association(size_t iteration, size_t cache_capacity, double cache_latency) {
    constexpr size_t min_assoc = 1;
    constexpr size_t max_assoc = 32;
    size_t measure_count = 32;

    double lower_step_assoc = 0.0;
    bool step_is_lower = false;

    std::cout << "\nIteration #" << iteration << "\n";

    while (true) {
        for (size_t cache_assoc = min_assoc; cache_assoc <= max_assoc; ++cache_assoc) {
            double avg = 0.0;
            for (size_t i = 0; i < measure_count; ++i) {
                auto buf = generate_sparsed_contignous_buffer(cache_capacity, cache_assoc);

                for (size_t i = 0; i < 4; ++i) {
                    iterate_over_buffer(buf);
                }

                MEASURE_TIME(result, {
                    for (size_t i = 0; i < 64; ++i) {
                        iterate_over_buffer(buf);
                    }
                });
                result /= double(buf.length * 64);

                avg += result;
            }
            avg /= measure_count;

            std::cout << "Cache assoc: " << cache_assoc
                          << " Latency: " << std::setprecision(5) << avg << "ns\n";

            if (step_is_lower && avg >= 1.5 * cache_latency) {
                std::cout << "MEASURE: Cache assoc: " << lower_step_assoc << "\n";
                return lower_step_assoc;
            }

            if (avg <= 1.35 * cache_latency) {
                step_is_lower = true;
                lower_step_assoc = cache_assoc;
            }
            else 
                step_is_lower = false;
        }

        std::cout << "Cannot determine cache associativity. Run again.\n\n";
    }
}

size_t high_precise_cache_association(size_t cache_capacity, double cache_latency) {
    constexpr size_t test_runs_min = 5;
    constexpr size_t test_runs_max = 11;

    std::unordered_map<size_t, size_t> occurrences;
    size_t mode;
    size_t mode_frequency = 0;

    for (size_t iterations = 1; iterations <= test_runs_max; ++iterations) {
        size_t res =
            cache_association(iterations, cache_capacity, cache_latency);
        size_t res_occurrences = ++occurrences[res];

        // Если больше чем в половине случаев и минимум итераций мы выполнили
        if (res_occurrences > iterations / 2 && iterations >= test_runs_min) {
            return res;
        }

        if (mode_frequency < res_occurrences) {
            mode_frequency = res_occurrences;
            mode = res;
        }
    }

    // Самый частый результат
    return mode;
}

size_t cache_line_size(std::mt19937_64& rng, size_t iteration, size_t cache_capacity) {
    constexpr size_t max_line_size = 256;
    size_t measure_count = 32;

    std::cout << "\nIteration #" << iteration << "\n";

    while (true) {
        std::vector<std::pair<size_t, double>> avgs;

        for (size_t line_size = sizeof(size_t) * 2; line_size <= max_line_size;
             line_size <<= 1) {

            double avg = 0.0;

            for (size_t m = 0; m < measure_count; ++m) {
                // сколько "строк" помещается в cache_capacity
                size_t length = cache_capacity / line_size;
                size_t stride_elems = line_size / sizeof(size_t);

                // нам нужно минимум length * stride_elems элементов,
                // что равно cache_capacity / sizeof(size_t)
                size_t elems = cache_capacity / sizeof(size_t);
                size_t bytes = elems * sizeof(size_t);

                // мой враг - моя лень
                size_t* base = nullptr;
                if (auto *aligned = static_cast<size_t*>(std::aligned_alloc(max_page_size, bytes)))
                    base = aligned;
                else {
                    size_t aligned_size = ((bytes + max_page_size - 1) / max_page_size) * max_page_size * 2;
                    base = static_cast<size_t*>(malloc(aligned_size));
                }
                if (!base) {
                    throw std::bad_alloc();
                }

                // случайная перестановка "строк"
                auto order = generate_random_permutation(rng, length);

                // строим односвязный цикл по индексам через stride_elems
                size_t cur_idx = 0;
                for (size_t i = 0; i < length; ++i) {
                    size_t next_idx = order[i] * stride_elems;
                    base[cur_idx] = next_idx;
                    cur_idx = next_idx;
                }
                // последнее звено указывает на начало
                base[cur_idx] = 0;

                opaque(base);
                // засоряем кэш
                scramble(rng, cache_capacity * 4, 2);

                MEASURE_TIME(result, {
                    size_t idx = 0;
                    for (size_t i = 0; i < length; ++i) {
                        idx = base[idx];
                    }
                    opaque(&idx);
                });
                result /= double(length);

                std::free(base);
                avg += result;
            }

            avg /= measure_count;
                      
            std::cout << "Cache line size: " << FORMAT_SIZE(line_size)
                          << " Latency: " << std::setprecision(5) << avg << " ns\n";

            avgs.emplace_back(line_size, avg);
        }

        // ищем "большой скачок" и потом плато
        for (size_t i = 1; i + 1 < avgs.size(); ++i) {
            double lhs = avgs[i].second     / avgs[i - 1].second;
            double rhs = avgs[i + 1].second / avgs[i].second;

            if (lhs >= 1.25 && rhs <= 1 + (lhs - 1) / 2) {
                std::cout << "MEASURE: Cache line size: " << FORMAT_SIZE(avgs[i].first) << "\n";
                return avgs[i].first;
            }
        }

        std::cout << "Cannot determine cache line size. Run again.\n\n";
    }
}

size_t high_precise_cache_line_size(std::mt19937_64& rng, size_t cache_capacity) {
    constexpr size_t test_runs_min = 10;
    constexpr size_t test_runs_max = 25;

    std::unordered_map<size_t, size_t> occurrences;
    size_t mode;
    size_t mode_frequency = 0;

    for (size_t iterations = 1; iterations <= test_runs_max; ++iterations) {
        size_t res =
            cache_line_size(rng, iterations, cache_capacity);
        size_t res_occurrences = ++occurrences[res];

        // Если больше чем в половине случаев и минимум итераций мы выполнили
        if (res_occurrences > iterations / 2 && iterations >= test_runs_min) {
            return res;
        }

        if (mode_frequency < res_occurrences) {
            mode_frequency = res_occurrences;
            mode = res;
        }
    }

    // Самый частый результат
    return mode;
}


int main() {
    std::random_device rnd_dev;
    auto rng = std::mt19937_64{rnd_dev()};

    std::pair<size_t, double> cache_capacity_est = high_precise_cache_capacity(rng);
    // std::pair<size_t, double> cache_capacity_est = {49152, 1.90237};
    std::cout << "\n";

    size_t cache_assoc = high_precise_cache_association(cache_capacity_est.first, cache_capacity_est.second);

    std::cout << "\n";

    size_t line_size = high_precise_cache_line_size(rng, cache_capacity_est.first);

    std::cout << "\n";

    std::cout << "Results: \n" 
              << "Capacity: " << FORMAT_SIZE(cache_capacity_est.first) << "\n"
              << "Associative: " << cache_assoc << "-way" << "\n"
              << "Line Size: " << line_size << std::endl;

    return 0;
}
