#include <atomic>
#include <chrono>
#include <ctime>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <deque>
#include <algorithm>
#include <exception>
#include <windows.h>
#include "ThreadPool.h"



// Структура для отслеживания состояния быстрой сортировки
struct QuicksortState {
    std::shared_ptr<std::promise<void>> prom; //  для ожидания завершения сортировки
    std::shared_ptr<std::atomic<int>> counter; // Счетчик активных задач
    std::shared_ptr<std::exception_ptr> except_ptr; // Указатель на исключение
    std::shared_ptr<std::mutex> except_mtx; // Мьютекс для обработки исключений
    std::shared_ptr<bool> except_set; // Флаг, что исключение уже установлено

    QuicksortState()
        : prom(std::make_shared<std::promise<void>>()),
          counter(std::make_shared<std::atomic<int>>(0)),
          except_ptr(std::make_shared<std::exception_ptr>()),
          except_mtx(std::make_shared<std::mutex>()),
          except_set(std::make_shared<bool>(false))
    {}
};

// Запустить задачу сортировки в пуле потоков и увеличить счетчик активных задач
void spawn_task_in_pool(ThreadPool &pool, std::shared_ptr<QuicksortState> state, task_type job) {
    state->counter->fetch_add(1, std::memory_order_relaxed); // Увеличиваем счетчик задач
    pool.push_task([state, job = std::move(job)]() mutable {
        try {
            job(); // Выполняем задачу сортировки
        } catch(...) {
            // Если возникло исключение, сохраняем его
            std::lock_guard<std::mutex> l(*state->except_mtx);
            if (!*state->except_set) {
                *state->except_ptr = std::current_exception();
                *state->except_set = true;
            }
        }
        // Уменьшаем счетчик задач, если все задачи завершены — завершаем promise
        int prev = state->counter->fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            std::lock_guard<std::mutex> l(*state->except_mtx);
            if (*state->except_set) {
                state->prom->set_exception(*state->except_ptr);
            } else {
                state->prom->set_value();
            }
        }
    });
}

// Рекурсивная задача быстрой сортировки для пула потоков
void quicksort_job(ThreadPool &pool, int* array, long left, long right, std::shared_ptr<QuicksortState> state, long threshold) {
    const long tiny = 1000; // Порог для сортировки маленьких участков стандартным алгоритмом
    if (left >= right) return;
    if (right - left <= tiny) {
        std::sort(array + left, array + right + 1); // Сортируем маленький участок
        return;
    }

    // Разбиение массива
    long l = left, r = right;
    int pivot = array[(l + r) / 2];
    do {
        while (array[l] < pivot) ++l;
        while (array[r] > pivot) --r;
        if (l <= r) {
            std::swap(array[l], array[r]);
            ++l; --r;
        }
    } while (l <= r);

    // Определяем, стоит ли запускать подзадачи параллельно
    bool left_big = (r - left) > threshold;
    bool right_big = (right - l) > threshold;

    // Запускаем подзадачи в пуле потоков, если участок достаточно большой
    if (left_big && right_big) {
        spawn_task_in_pool(pool, state, [=, &pool]() {
            quicksort_job(pool, array, left, r, state, threshold);
        });
        quicksort_job(pool, array, l, right, state, threshold);
    } else if (left_big) {
        spawn_task_in_pool(pool, state, [=, &pool]() {
            quicksort_job(pool, array, left, r, state, threshold);
        });
        quicksort_job(pool, array, l, right, state, threshold);
    } else if (right_big) {
        spawn_task_in_pool(pool, state, [=, &pool]() {
            quicksort_job(pool, array, l, right, state, threshold);
        });
        quicksort_job(pool, array, left, r, state, threshold);
    } else {
        // Если участок маленький, сортируем оба участка последовательно
        quicksort_job(pool, array, left, r, state, threshold);
        quicksort_job(pool, array, l, right, state, threshold);
    }
}

// Асинхронный запуск быстрой сортировки через пул потоков
std::future<void> quicksort_async(ThreadPool &pool, int* array, long left, long right, long threshold = 100000) {
    auto state = std::make_shared<QuicksortState>(); // Создаем объект состояния сортировки
    // Запускаем корневую задачу сортировки
    spawn_task_in_pool(pool, state, [=, &pool]() {
        quicksort_job(pool, array, left, right, state, threshold);
    });
    return state->prom->get_future(); // Возвращаем future для ожидания завершения сортировки
}

int main() {
    SetConsoleOutputCP(65001); // Установить кодировку UTF-8 для корректного вывода на русском языке
    constexpr long N = 1000000; 
    std::cout << "Размер массива: " << N << std::endl;

    int *arr1 = new int[N];
    int *arr2 = new int[N];
    std::mt19937 rng(0); // Генератор случайных чисел
    std::uniform_int_distribution<int> dist(0, 1000000); // Диапазон случайных чисел

    // Заполняем оба массива случайными числами
    for (long i = 0; i < N; ++i) {
        arr1[i] = dist(rng);
        arr2[i] = arr1[i];
    }

    {
        // Сортировка с использованием пула потоков
        clock_t time_start = clock();
        ThreadPool pool;
        auto fut = quicksort_async(pool, arr1, 0, N-1, 100000);
        fut.wait(); // Ожидаем завершения сортировки
        clock_t time_end = clock();
        std::cout << "Время быстрой сортировки с пулом потоков: " << double(time_end - time_start) / double(CLOCKS_PER_SEC) << " с" << std::endl;
        try {
            fut.get();
            std::cout << "Без ошибок!" << std::endl;
        } catch(const std::exception& e) {
            std::cout << "Была ошибка: " << e.what() <<  std::endl;
        }

        
    }

    {
        // Последовательная сортировка стандартным алгоритмом
        clock_t time_start = clock();
        std::sort(arr2, arr2 + N);
        clock_t time_end = clock();
        std::cout << "Время последовательной сортировки: " << double(time_end - time_start) / double(CLOCKS_PER_SEC) << " с" << std::endl;
        
    }

    // Освобождаем память
    delete [] arr1;
    delete [] arr2;
    return 0;
}