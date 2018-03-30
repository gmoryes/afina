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

namespace Afina {

// Forward declaration, see afina/Storage.h
class Storage;

namespace Network {
namespace NonBlocking {

/**
 * Class of processing tasks, the worker call process() from epoll
 */
class Task {
public:
    Task():client_fh(-1), _should_end(false) {};
    Task(Task&& from) {
        client_fh = from.client_fh;
        from.client_fh = -1;

        buffer = from.buffer;
        need_write = from.need_write;
        _should_end = from._should_end;
    }
    Task(int client_fh): client_fh(client_fh), _should_end(false) {}
    ~Task() {
        close(client_fh);
    }

    void process(const std::shared_ptr<Afina::Storage> &ps, uint32_t events) {
        Logger &logger = Logger::Instance();

        Socket socket(client_fh);

        if (events & EPOLLIN || events & EPOLLOUT) {
            // If we have some data to read/write
            bool success;
            if (events & EPOLLIN) {
                success = socket.Read(buffer);
            } else {
                success = socket.Write(need_write);
            }

            if (success) {
                if (events & EPOLLIN) {
                    socket.command->Execute(*ps, socket.Body(), need_write);
                    need_write += "\r\n";
                    socket.Write(need_write);
                }
            } else {
                if (socket.socket_error()) {
                    // need delete from epoll
                    _should_end = true;
                    logger.write("Error while processing socket:", client_fh);
                } else if (socket.internal_error()) {
                    need_write = "SERVER_ERROR Internal Error\r\n";
                    socket.Write(need_write);
                } else if (socket.is_closed()) {
                    _should_end = true;
                    logger.write("No data anymore");
                } else {
                    logger.write("WTF! I don't know what's happening now!");
                }
            }
        } else if (events & EPOLLHUP) {
            // If client disconnected
            _should_end = true;
        } else {
            logger.write("Unknown event:", events);
        }
    }

    bool can_be_deleted() const {
        return _should_end;
    }
private:
    int client_fh;

    std::string buffer;
    std::string need_write;

    bool _should_end;
};

/**
 * # Thread running epoll
 * On Start spaws background thread that is doing epoll on the given server
 * socket and process incoming connections and its data
 */
class Worker {
public:
    using storage_type = std::shared_ptr<Afina::Storage>;
    //Worker(): storage(-1ll) {}
    explicit Worker(std::shared_ptr<Afina::Storage> ps);
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

    bool stop;

protected:
    /**
     * Method executing by background thread
     */
    void OnRun(int server_socket, int worker_number);

private:
    std::thread thread;
    std::unordered_map<int, Task> tasks;

    std::shared_ptr<Afina::Storage> storage;
};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
#endif // AFINA_NETWORK_NONBLOCKING_WORKER_H
