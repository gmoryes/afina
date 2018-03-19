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

        tasks_in_progress.store(0);

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
                threads.push_back(Thread(std::thread(&perform, this, i), i));
            } catch (std::runtime_error &ex) {
                logger.write("Error while thread creating: ", ex.what());
            }
        }

        logger.write("Threads for ThreadPool created!");
    }
    ~Executor() {}

    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
     * free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
     */
    void Stop(bool await = false) {
        Logger& logger = Logger::Instance();
        logger.write("start stop");
        state = State::kStopping;
        logger.write("notify send");
        empty_condition.notify_all();
        logger.write("notify sended");
        if (await) {
            for (int i = 0; i < threads.size(); i++) {
                logger.write("Wait join of", threads[i].thread.get_id());
                threads[i].thread.join();
            }
            state = State::kStopped;
        }
    }

    /**
     * Add function to be executed on the threadpool. Method returns true in case if task has been placed
     * onto execution queue, i.e scheduled for execution and false otherwise.
     *
     * That function doesn't wait for function result. Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    template <typename F, typename... Types> bool Execute(F &&func, Types... args);

    // Return true if we can add to out thread pool tasks
    bool can_add() {
        std::lock_guard<std::mutex> lock(mutex);

        Logger& logger = Logger::Instance();
        logger.write("Queue size:", tasks.size());

        if (buzy_workers.load() < hight_watermark_)
            return true;


        return tasks.size() < max_queue_size_;

    }

    // No copy/move/assign allowed
    Executor(const Executor &)            = delete;
    Executor(Executor &&)                 = delete;
    Executor &operator=(const Executor &) = delete;
    Executor &operator=(Executor &&)      = delete;

    void lock() {
        mutex.lock();
    }

    void unlock() {
        mutex.unlock();
    }

    size_t alive_threads_size() {
        size_t result = 0;
        for (auto& thread : threads) {
            if (thread.active)
                result++;
        }

        return result;
    }

    bool should_end() {
        Logger& logger = Logger::Instance();

        size_t threads_num = alive_threads_size();
        auto cur_thread_id = std::this_thread::get_id();

        logger.write("threads num:", threads_num);
        logger.write("low:", low_watermark_);

        if (threads_num > low_watermark_) {
            // Add thread to queue to join
            logger.write("must die");
            must_be_joined.push_back(cur_thread_id);

            // Mark this thread as non-active
            for (auto& thread : threads) {
                if (thread.thread.get_id() == cur_thread_id) {
                    thread.active = false;
                    break;
                }
            }

            return true;
        }

        return false;
    }
private:

    std::atomic<int> tasks_in_progress;
    /**
     * Main function that all pool threads are running. It polls internal task queue and execute tasks
     */
    friend void perform(Executor *executor, int thread_num) {
        std::unique_lock<std::mutex> lock(executor->mutex);

        Logger& logger = Logger::Instance();

        std::stringstream ss;
        ss << "POOL_WORKER_";
        ss << thread_num;
        logger.i_am(ss.str());

        logger.write("Hello");

        // While ThredPool is run
        while (executor->state == Executor::State::kRun) {

            while (executor->tasks.empty() && executor->state == Executor::State::kRun) {
                logger.write("Wait events");
                auto prev_time = std::chrono::system_clock::now();
                executor->empty_condition.wait_until(
                    lock,
                    prev_time + std::chrono::milliseconds(executor->idle_time_)
                );

                auto cur_time = std::chrono::system_clock::now();
                auto int_ms = std::chrono::duration_cast<std::chrono::milliseconds>(cur_time - prev_time);
                if (int_ms.count() >= executor->idle_time_) {
                    if (executor->should_end()) {
                        logger.write("Goodbye (idle time)");
                        return;
                    }
                }
            }

            if (executor->state != Executor::State::kRun)
                break;

            executor->buzy_workers.fetch_add(1);

            // Get task in lock (!)
            auto task = std::move(executor->tasks.front());
            executor->tasks.pop_front();

            logger.write("Get task, unlock mutex");
            executor->mutex.unlock();

            task();
            executor->buzy_workers.fetch_sub(1);

            logger.write("Task done, wait lock");
            executor->mutex.lock();
        }

        logger.write("Goodbye");
    }

    int get_new_thread_number() {
        Logger& logger = Logger::Instance();

        bool used[hight_watermark_];
        for (int i = 0; i < hight_watermark_; ++i)
            used[i] = false;

        for (int i = 0; i < threads.size(); ++i) {
            logger.write("thread:", i, "is ", threads[i].active, " number:", threads[i].thread_number);
            used[threads[i].thread_number] = threads[i].active;
        }

        for (int i = 0; i < hight_watermark_; ++i) {
            if (!used[i])
                return i;
        }

        std::__throw_runtime_error("Can not find number for new thread");
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
        Thread(std::thread thread,
               int thread_number):
            thread(std::move(thread)),
            thread_number(thread_number),
            active(true) {}

        std::thread thread;
        int thread_number;
        bool active;
    };

    /**
     * Vector of actual threads that perorm execution, and flag
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
    State state;

    /**
     * Vector of threads, that are must be joined
     */
    std::vector<std::thread::id> must_be_joined;

    std::atomic<size_t> buzy_workers;

    size_t
        low_watermark_,   // Min threads number
        hight_watermark_, // Max threads number
        max_queue_size_,  // Max queue size
        idle_time_;       // Max thread idle time
};

template <typename F, typename... Types> bool Executor::Execute(F &&func, Types... args) {
    if (state != State::kRun) {
        return false;
    }

    Logger& logger = Logger::Instance();

    auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

    size_t cur_workers_number = buzy_workers.load();
    logger.write("Busy/All:", cur_workers_number, "/", threads.size());

    for (auto thread_id : must_be_joined) {
        for (auto it = threads.begin(); it != threads.end();) {
            if (it->thread.get_id() == thread_id) {
                logger.write("Wait join because of idle:", thread_id);
                it->thread.join();
                threads.erase(it);
            } else {
                ++it;
            }
        }
    }
    must_be_joined.clear();

    if (cur_workers_number == threads.size() && cur_workers_number < hight_watermark_) {

        int cur_threads_num = static_cast<int>(threads.size());
        try {
            int new_thread_number = get_new_thread_number();
            logger.write("All workers are buzy, create new", new_thread_number);
            threads.push_back(Thread(std::thread(&perform, this, new_thread_number), new_thread_number));
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
