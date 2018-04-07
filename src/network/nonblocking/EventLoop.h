#ifndef AFINA_EVENTLOOP_H
#define AFINA_EVENTLOOP_H

#include <vector>
#include <functional>
#include <sstream>
#include <sys/epoll.h>
#include <queue>
#include <string>

#include "Utils.h"

#define check_sys_call(call) \
    if ((call) < 0) { \
        std::stringstream s; \
        s << "call return -1" << " errno = ", errno; \
        throw std::runtime_error(s.str()); \
    }

#define check_and_assign_sys_call(res, call) \
    if (((res) = (call)) < 0) { \
        std::stringstream s; \
        s << "call return res" << " errno = ", errno; \
        throw std::runtime_error(s.str()); \
    }

namespace Afina {
using namespace Utils;

namespace Network {
namespace NonBlocking {

// То, что приписывается каждому filehandlr'у
class EventTask {
public:
    EventTask(int read_fh, int write_fh = -1): read_buffer(), _is_server(false), _read_fh(read_fh) {
        if (write_fh != -1)
            _write_fh = write_fh;
        else
            _write_fh = _read_fh;
    }
    EventTask(int read_fh, bool is_server, int write_fh = -1): EventTask(read_fh, write_fh), _is_server(is_server) {}

    /**
     * Вызывается после, того,  как epoll_wait вернул наше событие
     * @param flags - Флаг от epoll
     */
    void process(uint32_t flags);

    void add_acceptor(std::function<bool(int)>&& function) {
        acceptor = std::move(function);
    }

    void add_reader(std::function<bool(SmartString&, EventTask&)>&& function) {
        reader = std::move(function);
    }

    void add_writer(std::function<bool(int)>&& function) {
        if (not writer)

    }

    int write_fh() const {
        return _write_fh;
    }
private:
    SmartString& read_buffer;
    std::queue<SmartString&> must_be_written;
    bool _is_server;

    int _read_fh;
    int _write_fh;
    std::function<bool(int)> acceptor;
    std::function<bool(SmartString&, EventTask&)> reader;
    std::function<bool(int)> writer;
};

class EventLoop {
public:
    EventLoop(): _events(nullptr), _epoll_fh(-1), _stop(false), _max_events_number(0) {};

    ~EventLoop() {
        if (_events)
            delete _events;

    }

    bool Start(int events_max_number);

    /**
     * std::function<bool(...)>, callback возвращют bool, так
     * event_loop понимает - надо ли продолжать работать с текущим filehandler'ом
     */

    /**
     * Ассинхронно принимает новые соединения
     * @param server_socket - серверный сокет, который мы слушаем
     * @param func - функция, которая вызовется в случае нового соединеия
     * @param client_fh - новый сокет от клиента
     */
    void async_accept(int server_socket, std::function<bool(int)>&& func);

    /**
     * Асинхронно читает
     * @param read_fh - откуда читать
     * @param func - то, что вызовется, когда чтение окончится
     * @param buffer - буффер, куда будет сохранена считанная информация
     */
    void async_read(int read_fh, std::function<bool(SmartString&, EventTask&)>&& func);

    void async_write(std::string& must_be_written, EventTask* event_task);

    void loop();

private:

    epoll_event *_events;

    int _epoll_fh;
    bool _stop;
    int _max_events_number;
};

}
}
}

#endif //AFINA_EVENTLOOP_H
