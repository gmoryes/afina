#ifndef AFINA_EVENTLOOP_H
#define AFINA_EVENTLOOP_H

#include <vector>
#include <functional>
#include <sstream>
#include <sys/epoll.h>
#include <queue>
#include <string>
#include <unordered_map>
#include <unistd.h>

#include "Utils.h"
#include <atomic>
#include <mutex>

namespace Afina {
using namespace Utils;

namespace Network {
namespace NonBlocking {

// То, что приписывается каждому filehandlr'у
class EventTask {
public:

    EventTask(int fd, uint32_t flags, int epoll_fh):
        fd(fd),
        event_flags(flags),
        read_buffer(),
        _epoll_fh(epoll_fh) {}

    ~EventTask() {
        close(fd);
    }

    /**
     * Вызывается после, того, как epoll_wait вернул наше событие
     * @param flags - Флаг от epoll
     * @return - true, если событие надо удалить из epoll, false - иначе
     */
    bool process(uint32_t flags);

    void add_acceptor(std::function<bool(int)>&& function) {
        acceptor = std::move(function);
    }

    void add_reader(std::function<bool(int, SmartString&)>&& function) {
        reader = std::move(function);
    }

    //void add_writer(std::function<bool(int)>&& function) {}

    bool message_queue_empty() const {
        return write_queue_messages.empty();
    }

    void add_message_to_write(std::string& message) {
        write_queue_messages.emplace(message);
    }

    int fd;
    /**
     * TODO delete this comment
     * Флаги, которые можно передать для async_read, async_write, async_accept.
     * Имеют тип std::atomic<> потому что
     */

    uint32_t event_flags;
private:
    std::mutex mutex;
    SmartString read_buffer;
    std::queue<SmartString> write_queue_messages;

    std::function<bool(int)> acceptor;
    std::function<bool(int, SmartString&)> reader;

    int _epoll_fh;
    //std::function<bool(int)> writer;
};

class EventLoop {
public:
    EventLoop(): _events(nullptr),
                 _epoll_fh(-1),
                 _stop(false),
                 _max_events_number(0) {}

    ~EventLoop() {
        if (_events)
            delete _events;

        if (_epoll_fh != -1)
            close(_epoll_fh);
    }

    bool Start(int events_max_number);

    /**
     * std::function<bool(...)>, callback's возвращют bool, так
     * event_loop понимает - надо ли продолжать работать с текущим filehandler'ом
     */

    /**
     * Ассинхронно принимает новые соединения
     * @param server_socket - серверный сокет, который мы слушаем
     * @param func - функция, которая вызовется в случае нового соединеия
     */
    void async_accept(int server_socket, std::function<bool(int)> func, uint32_t flags = 0);

    /**
     * Асинхронно читает
     * @param fd - откуда читать
     * @param func - bool(int, SmartString&) - пользовательский callback который вызывается каждый
     *               раз при считывании, в нее передается fd с которого произошло чтение
     *               и SmartString& - то, что смогли считать
     */
    void async_read(int fd, std::function<bool(int, SmartString&)> func, uint32_t flags = 0);

    void async_write(int fd, std::string& must_be_written, uint32_t flags = 0);

    void loop();
    void delete_event(int fd);

private:
    epoll_event *_events;

    int _epoll_fh;
    bool _stop;
    int _max_events_number;

    std::atomic<int> _state;
    enum State : uint16_t { WaitEvent, Reading, Writing };

    std::unordered_map<int, EventTask*> events_tasks;
};

}
}
}

#endif //AFINA_EVENTLOOP_H
