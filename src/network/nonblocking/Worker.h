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

using namespace Utils;

/**
 * Class of processing tasks, the worker call process() from epoll
 */
class Task {
public:
    Task():read_fh(-1), write_fh(-1), _should_end(false), _flags(0) {};
    Task(Task&& from) noexcept {
        read_fh = from.read_fh;
        write_fh = from.write_fh;
        _flags = from._flags;

        from.write_fh = -1;
        from.read_fh = -1;
        from._flags = 0;

        buffer = std::move(from.buffer);
        need_write = std::move(from.need_write);
        _should_end = from._should_end;
    }
    Task(int client_fh, int write_fh = -1):
        read_fh(client_fh),
        _should_end(false),
        _flags(0) {

        if (write_fh == -1) {
            this->write_fh = client_fh;
        } else {
            this->write_fh = write_fh;
        }
    }
    ~Task() {
        close(read_fh);
        if (read_fh != write_fh)
            close(write_fh);
    }

    void process(const std::shared_ptr<Afina::Storage> &ps, uint32_t events) {
        Logger &logger = Logger::Instance();

        Socket socket(read_fh, write_fh);

        if (events & EPOLLIN)
            logger.write("EPOLLIN(", EPOLLIN, ")");
        if (events & EPOLLOUT)
            logger.write("EPOLLOUT(", EPOLLOUT, ")");
        if (events & EPOLLHUP)
            logger.write("EPOLLHUP(", EPOLLHUP, ")");
        logger.write("events:", events);


        if (events & EPOLLIN || events & EPOLLOUT) {
            // If we have some data to read/write
            bool success;
            if (events & EPOLLIN) {
                success = socket.SmartRead(buffer);
            } else {
                success = socket.Write(need_write);
            }

            if (success) {
                if (events & EPOLLIN) {
                    socket.command->Execute(*ps, socket.Body(), need_write);
                    need_write += "\r\n";
                    socket.Write(need_write);
                    _flags = EPOLLOUT;
                }
            } else {
                if (socket.socket_error()) {
                    // need delete from epoll
                    _should_end = true;
                    logger.write("Error while processing socket:", read_fh);
                } else if (socket.internal_error()) {
                    need_write = "SERVER_ERROR Internal Error\r\n";
                    socket.Write(need_write);
                } else if (socket.is_closed()) {
                    _should_end = true;
                    logger.write("No data anymore");
                } else if(socket.is_all_data_send()) {
                    _flags = EPOLLIN | EPOLLHUP;
                    logger.write("Data send, can set dawn EPOLLOUT flag");
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

    bool has_data_to_send() const {
        return !need_write.empty();
    }

    uint32_t epoll_flags(int op = -1) {
        if (op == -1) {
            return _flags;
        } else {
            uint32_t save = _flags;
            _flags = 0;
            return save;
        }
    }
private:
    int read_fh;

    // Equal to read_fh by default, case for FIFO files
    int write_fh;

    SmartString buffer;
    std::string need_write;

    bool _should_end;

    uint32_t _flags;
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

    bool stop;

protected:
    /**
     * Method executing by background thread
     */
    void OnRun(int server_socket, int r_fifo, int worker_number);

private:
    std::unordered_map<int, Task> tasks;

    std::shared_ptr<Afina::Storage> storage;

    // File handlers for fifo (read and write)
    std::pair<int, int> fifo;
};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
#endif // AFINA_NETWORK_NONBLOCKING_WORKER_H
