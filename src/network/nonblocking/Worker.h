#ifndef AFINA_NETWORK_NONBLOCKING_WORKER_H
#define AFINA_NETWORK_NONBLOCKING_WORKER_H

#include <memory>
#include <thread>
#include <mutex>
#include <unordered_map>
#include "Utils.h"
#include <logger/Logger.h>

#include <unistd.h>
#include <sys/epoll.h>
#include "EventLoop.h"

namespace Afina {

// Forward declaration, see afina/Storage.h
class Storage;

namespace Network {
namespace NonBlocking {

using namespace Utils;

/**
 * # Thread running epoll
 * On Start spaws background thread that is doing epoll on the given server
 * socket and process incoming connections and its data
 */
class Worker {
public:
    using storage_type = std::shared_ptr<Afina::Storage>;
    using SharedParsers = std::vector<std::shared_ptr<Protocol::Parser>>;

    //Worker(): storage(-1ll) {}
    Worker(std::shared_ptr<Afina::Storage> ps, const std::pair<int, int>& fifo);
    Worker(Worker&&) = default;
    ~Worker();

    /**
     * Spaws new background thread that is doing epoll on the given server
     * socket. Once connection accepted it must be registered and being processed
     * on this thread
     */
    void Start(int server_socket, int worker_number);

    /**
     * Signal background thread to stop. After that signal thread must stop to
     * accept new connections and must stop read new commands from existing. Once
     * all readed commands are executed and results are send back to client, thread
     * must stop
     */
    void Stop();

    /**
     * Blocks calling thread until background one for this worker is actually
     * been destoryed
     */
    void Join();

    friend void test(Worker& self, int a, int b);
    friend bool acceptor(Worker& worker, int client_fh);
    friend bool reader(Worker& worker, std::shared_ptr<Protocol::Parser>& parser, int fd, SmartString& buffer);
    //friend bool writer(Worker& worker, EventTask& event_task, int has_written);

    bool stop;

protected:
    /**
     * Method executing by background thread
     */
    void OnRun(int server_socket, int worker_number, int r_fifo);

private:
    EventLoop event_loop;
    SharedParsers parsers;

    std::shared_ptr<Afina::Storage> storage;

    // File handlers for fifo (read and write)
    std::pair<int, int> fifo;
};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
#endif // AFINA_NETWORK_NONBLOCKING_WORKER_H
