#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>



// Тип задачи для пула потоков — функция без аргументов и возвращаемого значения
using task_type = std::function<void()>;

// Класс пула потоков с поддержкой work stealing
class ThreadPool {
public:
    // Конструктор: инициализация пула потоков
    ThreadPool(size_t n = std::thread::hardware_concurrency()) {
        if (n == 0) n = 4; // Если не удалось определить количество ядер, используем 4 потока
        m_done.store(false); // Флаг завершения работы пула
        m_queues.resize(n); // Создаем очереди задач для каждого потока
        // Для каждого потока создаем свой мьютекс и condition_variable
        for (size_t i = 0; i < n; ++i) {
            m_queue_mutexes.emplace_back(std::make_unique<std::mutex>());
            m_queue_cvs.emplace_back(std::make_unique<std::condition_variable>());
        }
        // Запускаем рабочие потоки
        for (size_t i = 0; i < n; ++i) {
            m_workers.emplace_back(&ThreadPool::worker_loop, this, i);
        }
    }

    // Деструктор: корректно завершаем работу всех потоков
    ~ThreadPool() {
        m_done.store(true); // Устанавливаем флаг завершения
        for (auto &cv : m_queue_cvs) cv->notify_all(); // Пробуждаем все потоки
        for (auto &t : m_workers) if (t.joinable()) t.join(); // Дожидаемся завершения всех потоков
    }

    // Добавить задачу в пул потоков
    void push_task(task_type t) {
        size_t idx = m_index.fetch_add(1, std::memory_order_relaxed) % m_queues.size(); // Выбираем очередь по кругу
        {
            std::lock_guard<std::mutex> l(*m_queue_mutexes[idx]); // Захватываем мьютекс очереди
            m_queues[idx].push_front(std::move(t)); // Кладем задачу в начало очереди
        }
        m_queue_cvs[idx]->notify_one(); // Пробуждаем поток, ожидающий задачу
    }

private:
    // Основной цикл работы потока
    void worker_loop(size_t my_index) {
        while (!m_done.load()) { // Пока пул не завершен
            task_type task;
            // Пытаемся взять задачу из своей очереди
            if (try_pop_from_queue(my_index, task)) {
                task(); // Выполняем задачу
                continue;
            }
            bool stolen = false;
            // Пытаемся украсть задачу из других очередей
            for (size_t i = 0; i < m_queues.size(); ++i) {
                size_t idx = (my_index + 1 + i) % m_queues.size();
                if (try_steal_from_queue(idx, task)) {
                    task(); // Выполняем украденную задачу
                    stolen = true;
                    break;
                }
            }
            if (stolen) continue;

            // Если задач нет, ждем появления новых
            std::unique_lock<std::mutex> lk(*m_queue_mutexes[my_index]);
            m_queue_cvs[my_index]->wait_for(lk, std::chrono::milliseconds(50), [&]() {
                return !m_queues[my_index].empty() || m_done.load();
            });
        }
    }

    // Взять задачу из своей очереди
    bool try_pop_from_queue(size_t idx, task_type &out) {
        std::lock_guard<std::mutex> l(*m_queue_mutexes[idx]);
        if (m_queues[idx].empty()) return false;
        out = std::move(m_queues[idx].front());
        m_queues[idx].pop_front();
        return true;
    }

    // Украсть задачу из чужой очереди (с конца)
    bool try_steal_from_queue(size_t idx, task_type &out) {
        std::lock_guard<std::mutex> l(*m_queue_mutexes[idx]);
        if (m_queues[idx].empty()) return false;
        out = std::move(m_queues[idx].back());
        m_queues[idx].pop_back();
        return true;
    }

private:
    std::vector<std::deque<task_type>> m_queues; // Очереди задач для каждого потока
    std::vector<std::thread> m_workers; // Вектор рабочих потоков
    std::vector<std::unique_ptr<std::condition_variable>> m_queue_cvs; // Условные переменные для очередей
    std::vector<std::unique_ptr<std::mutex>> m_queue_mutexes; // Мьютексы для очередей
    std::atomic<size_t> m_index {0}; // Индекс для round-robin распределения задач
    std::atomic<bool> m_done; // Флаг завершения работы пула
};