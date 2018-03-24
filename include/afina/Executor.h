#ifndef AFINA_THREADPOOL_H
#define AFINA_THREADPOOL_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <deque>
#include <string>
#include <thread>
#include <atomic>
#include <logger/Logger.h>
#include <chrono>

namespace Afina {

/**
 * # Thread pool
 */
class Executor;
void perform(Executor *executor, int thread_num);

class Executor {
public:
    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };

    Executor() {}
    bool Start(size_t low_watermark = 1,
               size_t hight_watermark = 1,
               size_t max_queue_size = 1,
               size_t idle_time = 100) {

        low_watermark_   = low_watermark;
        hight_watermark_ = hight_watermark;
        max_queue_size_  = max_queue_size;
        idle_time_       = idle_time;

        Logger& logger = Logger::Instance();
        logger.write("Start create ThreadPool");
        logger.write("ThredPool params");
        logger.write("number of thread: [ low =", low_watermark, "high =", hight_watermark, "]");
        logger.write("max_queue size: ", max_queue_size);
        logger.write("idle_time: ", idle_time);

        buzy_workers.store(0);
        state = State::kRun;

        for (int i = 0; i < low_watermark; i++) {
            try {

                std::thread thread(&perform, this, i);
                threads.push_back(Thread(thread.get_id(), i));
                thread.detach();

            } catch (std::runtime_error &ex) {
                logger.write("Error while thread creating: ", ex.what());
            }
        }

        logger.write("Threads for ThreadPool created!");
    }
    ~Executor() {
        wait_threads();
    }

    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
     * free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
     */

    std::condition_variable thread_done;



    int number_of_active_threads() {
        int result = 0;
        for (auto it = threads.begin(); it != threads.end();) {
            if (it->active) {
                result++;
                ++it;
            } else {
                threads.erase(it);
            }
        }

        return result;
    }

    // Wait until all threads will be done
    void wait_threads() {
        std::unique_lock<std::mutex> lock(mutex);

        Logger& logger = Logger::Instance();
        logger.write("Going to wait threads");
        int num;

        while ((num = number_of_active_threads())) {
            logger.write("Wait for", num, "threads");
            thread_done.wait(lock);
        }

        state = State::kStopped;
    }

    void Stop(bool await = false) {
        Logger& logger = Logger::Instance();
        logger.write("Start stop");

        state = State::kStopping;

        logger.write("Notify send");
        empty_condition.notify_all();
        logger.write("Notify sended");

        if (await)
            wait_threads();
    }

    /**
     * Add function to be executed on the threadpool. Method returns true in case if task has been placed
     * onto execution queue, i.e scheduled for execution and false otherwise.
     *
     * That function doesn't wait for function result. Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    template <typename F, typename... Types> bool Execute(F &&func, Types... args);

    /**
     * Return true if we can add to out thread pool tasks
     */
    bool can_add() {
        std::lock_guard<std::mutex> lock(mutex);

        Logger& logger = Logger::Instance();
        logger.write("Queue size:", tasks.size());

        if (buzy_workers.load() < hight_watermark_)
            return true;

        return tasks.size() < max_queue_size_;
    }

    void make_non_active_this_thread() {
        for (auto& item : threads) {
            if (item.thread_id == std::this_thread::get_id()) {
                item.active = false;
                break;
            }
        }
    }

    /**
     * Return true if thread must be dead, because of idle time,
     * and false if it is not necessary
     */
    bool should_end() {
        Logger& logger = Logger::Instance();

        size_t threads_num = number_of_active_threads();

        if (threads_num > low_watermark_)
            return true;

        return false;
    }
private:

    // No copy/move/assign allowed
    Executor(const Executor &)            = delete;
    Executor(Executor &&)                 = delete;
    Executor &operator=(const Executor &) = delete;
    Executor &operator=(Executor &&)      = delete;

    /**
     * Main function that all pool threads are running. It polls internal task queue and execute tasks
     */
    friend void perform(Executor *executor, int thread_num) {
        std::function<void()> task;

        // Flag of idle time
        bool should_end = false;

        Logger& logger = Logger::Instance();

        // Set name of thread for Logger
        std::stringstream ss;
        ss << "POOL_WORKER_";
        ss << thread_num;
        logger.i_am(ss.str());

        logger.write("Hello");

        // While ThredPool is run
        while (executor->state == Executor::State::kRun) {

            {
                // Critical section for condvar
                std::unique_lock<std::mutex> lock(executor->mutex);

                // Out of loop because of unsuspicious wakeups
                auto start_wait_task_time = std::chrono::system_clock::now();
                while (executor->tasks.empty() && executor->state == Executor::State::kRun) {
                    logger.write("Wait events");

                    executor->empty_condition.wait_until(
                        lock,
                        start_wait_task_time + std::chrono::milliseconds(executor->idle_time_)
                    );

                    auto cur_time = std::chrono::system_clock::now();
                    auto int_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        cur_time - start_wait_task_time
                    );

                    if (int_ms.count() >= executor->idle_time_) {
                        if (executor->should_end()) {
                            logger.write("Goodbye (idle time)");
                            should_end = true;
                            break;
                        }
                        start_wait_task_time = std::chrono::system_clock::now();
                    }
                }

                // We don't know, whether the queue is not empty, or server is stoppping
                if (executor->state != Executor::State::kRun)
                    break;

                if (should_end)
                    break;

                executor->buzy_workers.fetch_add(1);

                // Get task in lock (!)
                task = std::move(executor->tasks.front());
                executor->tasks.pop_front();

                logger.write("Get task, unlock mutex");
            }

            try {
                task();
            } catch (std::runtime_error& error) {
                logger.write("Error while executing task:", error.what());
            }
            executor->buzy_workers.fetch_sub(1);

            logger.write("Task done");
        }

        // Make thread non active for reuse number of worker
        std::lock_guard<std::mutex> lock(executor->mutex);

        executor->make_non_active_this_thread();

        executor->thread_done.notify_one();
        logger.write("Goodbye");
    }

    /**
     * Return number of new worker
     */
    int get_new_thread_number() {
        bool used[hight_watermark_];
        for (int i = 0; i < hight_watermark_; ++i)
            used[i] = false;

        for (auto it = threads.begin(); it != threads.end();) {
            used[it->thread_number] = it->active;

            if (!it->active) {
                threads.erase(it);
            } else {
                ++it;
            }
        }

        for (int i = 0; i < hight_watermark_; ++i)
            if (!used[i])
                return i;

        throw std::runtime_error("Can not find number for new thread");
    }

    /**
      * Mutex to protect state below from concurrent modification
     */
    std::mutex mutex;

    /**
     * Conditional variable to await new data in case of empty queue
     */
    std::condition_variable empty_condition;

    struct Thread {
        Thread(std::thread::id thread_id,
               int thread_number):
            thread_id(thread_id),
            thread_number(thread_number),
            active (true) {}

        std::thread::id thread_id;
        int thread_number;
        bool active;
    };

    /**
     * Vector of actual threads that perform execution, and flag
     * is it active or not
     */
    std::vector<Thread> threads;

    /**
     * Task queue
     */
    std::deque<std::function<void()>> tasks;

    /**
     * Flag to stop bg threads
     */
    std::atomic<State> state;

    /**
     * Vector of threads, that are must be joined
     */

    std::atomic<size_t> buzy_workers;

    size_t
        low_watermark_,   // Min threads number
        hight_watermark_, // Max threads number
        max_queue_size_,  // Max queue size
        idle_time_;       // Max thread idle time
};

template <typename F, typename... Types> bool Executor::Execute(F &&func, Types... args) {
    std::lock_guard<std::mutex> lock(mutex);

    if (state != State::kRun) {
        return false;
    }

    Logger& logger = Logger::Instance();

    auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

    size_t cur_workers_number = buzy_workers.load();

    logger.write("Busy/All:", cur_workers_number, "/", threads.size());

    if (cur_workers_number == threads.size() && cur_workers_number < hight_watermark_) {

        try {
            // Get number of worker
            int new_thread_number = get_new_thread_number();

            logger.write("All workers are buzy, create new", new_thread_number);

            // Create worker
            std::thread thread(&perform, this, new_thread_number);

            // Save it to vector
            threads.push_back(Thread(thread.get_id(), new_thread_number));
            thread.detach();
        } catch (std::runtime_error &ex) {
            logger.write("Error while thread creating: ", ex.what());
        }
    }

    // Enqueue new task
    tasks.push_back(exec);
    empty_condition.notify_one();
    return true;
}

} // namespace Afina

#endif // AFINA_THREADPOOL_H
