#ifndef AFINA_WORKER_H
#define AFINA_WORKER_H

#include <afina/Storage.h>
#include <memory>
#include <atomic>
#include <afina/coroutine/Engine.h>
#include <condition_variable>

namespace Afina {
namespace Network {
namespace Coroutines {

class Worker {
public:
    explicit Worker(std::shared_ptr<Afina::Storage> storage, int n, int fd):
        storage(std::move(storage)),
        worker_number(n),
        server_socket(fd) {

        stop.store(false);
        has_stopped.store(false);
    }

    ~Worker() = default;

    // Start worker
    void Start();

    // Function that runs in thread
    void Run();

    // Stop worker
    void Stop();

    // Wait stopping of workers
    void Join();

    // Main coroutine, that accept new connections
    friend void acceptor(Worker *worker, int &server_socket);

    // Coro-function, that runs in coro, that work with new client
    friend void client(Worker *&worker, int &fd);

private:
    std::shared_ptr<Afina::Storage> storage;
    int worker_number;
    int server_socket;

    std::atomic<bool> stop;
    std::atomic<bool> has_stopped;
    Coroutine::Engine engine;
    std::mutex mutex;
    std::condition_variable stop_cv;
};

}
}
}

#endif //AFINA_WORKER_H
