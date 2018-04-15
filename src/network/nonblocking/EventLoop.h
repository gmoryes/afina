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
#include <sys/eventfd.h>

#include "Utils.h"
#include <atomic>
#include <condition_variable>

namespace Afina {
using namespace Utils;

namespace Network {
namespace NonBlocking {

enum State { Created, Running, Stopping, Stopped };

// То, что приписывается каждому filehandlr'у
class EventTask {
public:
    EventTask(int fd, uint32_t flags, int epoll_fh):
        read_buffer(),
        fd(fd),
        event_flags(flags),
        _epoll_fh(epoll_fh) {}

    ~EventTask() {
        close(fd);
    }

    /**
     * Вызывается после, того, как epoll_wait вернул наше событие
     * @param flags - Флаг от epoll
     * @return - true, если событие надо удалить из epoll, false - иначе
     */
    bool process(uint32_t flags, State event_loop_state);

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
    uint32_t event_flags;
private:
    int _epoll_fh;
    SmartString read_buffer;
    std::queue<SmartString> write_queue_messages;

    std::function<bool(int)> acceptor;
    std::function<bool(int, SmartString&)> reader;
    //std::function<bool(int)> writer;
};

struct EventLooPInterrupter {

    EventLooPInterrupter();
    ~EventLooPInterrupter();

    void interrupt();
    void init(int);

    int fd;
    int epoll_fd;

};

class EventLoop {
public:
    EventLoop(): _events(nullptr),
                 _epoll_fh(-1),
                 _max_events_number(0),
                 interrupter() {
        _stop.store(false);
        state = State::Created;
    };

    ~EventLoop() {
        if (_events)
            delete[] _events;
    }

    bool Start(int events_max_number);

    // Set flag "need to stop"
    void Stop();

    /**
     * Wait for 3000ms of eventloop, and
     * shutdown _epoll_fh if time is out
     */
    void Join();

    /**
     * std::function<bool(...)>, callback возвращют bool, так
     * event_loop понимает - надо ли продолжать работать с текущим filehandler'ом
     */

    /**
     * Ассинхронно принимает новые соединения
     * @param server_socket - серверный сокет, который мы слушаем
     * @param func - функция, которая вызовется в случае нового соединеия
     */
    void async_accept(int server_socket, std::function<bool(int)> func);

    /**
     * Асинхронно читает
     * @param fd - откуда читать
     * @param func - bool(int, SmartString&) - пользовательский callback который вызывается каждый
     *               раз при считывании, в нее передается fd с которого произошло чтение
     *               и SmartString& - то, что смогли считать
     */
    void async_read(int fd, std::function<bool(int, SmartString&)> func, uint32_t flags = 0);

    void async_write(int fd, std::string& must_be_written);

    void loop();
    void delete_event(int fd);

private:

    epoll_event *_events;

    int _epoll_fh;
    std::atomic<bool> _stop;
    int _max_events_number;

    /**
     * Нужен для того, чтобы проверять события на наличие в epoll - алтернатива syscall + проверка errno
     * Также необходим для корректного завершения работы и освобождения памяти
     */
    std::unordered_map<int, EventTask*> events_tasks;

    std::atomic<State> state;

    // Condvar для нотификации остановки работы
    std::condition_variable cv;

    // Wait time in seconds (set to 3000ms)
    static int StopWaitTime;
    static int LastWriteWaitTime;
    static int WaitAfterCloseEpoll;

    EventLooPInterrupter interrupter;

    /**
     * Ждет ms миллисекунд остановки EventLoop'a
     * @param ms
     * @return true, если EventLoop остановился и наоборот
     */
    bool wait_for_stopping(int ms);

    /**
     * Проходит по всем имеющимся таскам и освобождает память, использует для этого event_tasks
     */
    void clear_tasks_data();
};

}
}
}

#endif //AFINA_EVENTLOOP_H
