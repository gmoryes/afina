#include "EventLoop.h"
#include <logger/Logger.h>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>

namespace Afina {
namespace Network {
namespace NonBlocking {

int EventLoop::StopWaitTime = 3000;
int EventLoop::WaitAfterCloseEpoll = 1000;
int EventLoop::LastWriteWaitTime = 1000;

bool EventTask::process(uint32_t flags, State state) {
    Logger& logger = Logger::Instance();

    if (flags & EPOLLIN && state == State::Running) {
        if (not reader) { // Если нет reader'a, то пришло событие на server socket

            struct sockaddr_in client_addr{};
            socklen_t sinSize = sizeof(struct sockaddr_in);

            // Создаем новое подключение
            int client_fh;
            client_fh = accept(fd, (struct sockaddr *) &client_addr, &sinSize);
            if (client_fh == -1 && errno == EAGAIN) {
                logger.write("accept() -1, try again later");
                return false;
            }

            logger.write("Get new client =", client_fh);

            make_socket_non_blocking(client_fh);

            // Вызываем callback, который нам передали, передаем
            // ему в аргументы новый сокет
            acceptor(client_fh);

        } else {

            ssize_t has_read = 0;
            char buffer[BUFFER_SIZE];

            while ((has_read = read(fd, buffer, BUFFER_SIZE)) > 0) {
                logger.write("Has read", has_read, "bytes from socket:", fd);

                read_buffer.Put(buffer, has_read);

                // Вызываем callback, который нам передали, он
                // возвращает нам "должны ли мы продолжать работу с данным клиентом"
                bool should_continue = reader(fd, read_buffer);

                if (not should_continue) {
                    logger.write("End working with:", fd);
                    return true; // Delete event from epoll
                }
            }

            if (has_read < 0) {
                if (errno == EAGAIN) {
                    logger.write("No data for read, socket(", fd, ") is still open, wait...");
                } else {
                    logger.write("Error during read(), errno =", errno);
                    return true; // Delete event from epoll
                }
            } else {
                logger.write("Client close connection, goodbye");
                return true; // Delete event from epoll
            }
        }
    }

    if (flags & EPOLLOUT) {
        while (not write_queue_messages.empty()) {
            SmartString& cur_message = write_queue_messages.front();
            ssize_t has_write;
            size_t need_write = std::min(size_t(BUFFER_SIZE), cur_message.size());
            while ((has_write = write(fd, cur_message.data(), need_write)) > 0) {
                logger.write("Has write", has_write, "bytes to", fd);

                cur_message.Erase(has_write);
                need_write = std::min(size_t(BUFFER_SIZE), cur_message.size());

                if (need_write == 0)
                    break;
            }

            if (cur_message.empty()) {
                write_queue_messages.pop();
            } else {
                if (has_write < 0) {
                    if (errno == EAGAIN) {
                        logger.write("Socket(", fd, " is overloaded now, wait...");
                        break;
                    } else {
                        logger.write("Error during write(), errno =", errno);
                        return true; // Delete event from epoll
                    }
                }
            }
        }

        if (write_queue_messages.empty()) { // Если сообщений больше нет, надо опустить флаг EPOLLOUT
            epoll_event ev{};
            event_flags &= ~EPOLLOUT;
            ev.events = event_flags;
            ev.data.ptr = this;
            check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_MOD, fd, &ev));
        }
    }

    if (flags & EPOLLHUP) {
        return true; // Delete event from epoll
    }

    return false;
}

void EventLoop::async_accept(int server_socket, std::function<bool(int)> func) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLHUP;

    auto task = new EventTask(server_socket, ev.events, _epoll_fh);
    task->add_acceptor(std::move(func));
    ev.data.ptr = task;

    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, server_socket, &ev));
    events_tasks[server_socket] = task;
}

void EventLoop::async_read(int fd, std::function<bool(int, SmartString&)> func, uint32_t flags) {
    epoll_event ev{};
    if (!flags)
        ev.events = EPOLLIN | EPOLLHUP;
    else
        ev.events = flags;

    auto task = new EventTask(fd, ev.events, _epoll_fh);
    task->add_reader(std::move(func));
    ev.data.ptr = task;

    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, fd, &ev));
    events_tasks[fd] = task;
}


void EventLoop::async_write(int fd, std::string& must_be_written) {
    epoll_event ev{};
    auto it = events_tasks.find(fd);

    if (it != events_tasks.end()) {
        auto event_task = it->second;
        // Если в epoll уже лежит такой filehandler, в таком случае мы должны просто добавить
        // новое сообщение в таск этого filehandler'a

        if (event_task->message_queue_empty()) { // Если сообщений еще нет, надо добавить флаг EPOLLOUT
            event_task->event_flags |= EPOLLOUT;
            ev.events = event_task->event_flags;
            ev.data.ptr = event_task;
            check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_MOD, fd, &ev));
        }

        event_task->add_message_to_write(must_be_written); // Добавляем в очередь новое сообщение

    } else {
        // Иначе добавляем новое событие в epoll
        ev.events = EPOLLOUT;
        auto task = new EventTask(fd, ev.events, _epoll_fh);
        task->add_message_to_write(must_be_written);
        ev.data.ptr = task;

        events_tasks[fd] = task;
        check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, fd, &ev));
    }
}

void EventLoop::loop() {
    Logger& logger = Logger::Instance();
    state = State::Running;

    while (not _stop) {

        int events_count = epoll_wait(_epoll_fh, _events, _max_events_number, -1); // -1 - Timeout
        if (events_count < 0) {
            logger.write("epoll_wait() -1, errno(", errno, ")");
            break;
        }

        if (_stop)
            // task->process() will do only write operations
            state = State::Stopping;

        for (int i = 0; i < events_count; i++) {
            if (_events[i].data.ptr == &interrupter) {
                logger.write("Get interrupter in loop");
                _stop.store(true);
                break;
            }

            auto task = static_cast<EventTask*>(_events[i].data.ptr);
            bool should_delete = false, was_error = false;
            try {
                should_delete = task->process(_events[i].events, state);
            } catch (std::runtime_error& error) {
                logger.write("Can not execute task, desc:", error.what());
                was_error = true;
            }

            if (should_delete || was_error) {
                delete_event(task->fd);
                delete task;
            }
        }
    }

    logger.write("Stopping");
    state = State::Stopping;

    EventLoop::clear_tasks_data();

    state = State::Stopped;

    logger.write("Send notify of done");
    cv.notify_all();
}

bool EventLoop::Start(int events_max_number) {
    _max_events_number = events_max_number;
    _events = new epoll_event[events_max_number];

    check_and_assign_sys_call(_epoll_fh, epoll_create(events_max_number));

    interrupter.init(_epoll_fh);
    epoll_event ev{};

    ev.events = EPOLLET | EPOLLIN | EPOLLHUP;
    ev.data.ptr = &interrupter;
    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, interrupter.fd, &ev));

    return true;
}

void EventLoop::delete_event(int fd) {
    epoll_event ev{};
    events_tasks.erase(fd);
    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_DEL, fd, &ev));
}

void EventLoop::Stop() {
    _stop.store(true);
}

void EventLoop::Join() {
    Logger& logger = Logger::Instance();

    if (state == State::Stopped)
        return;

    /**
     * Вначале ждем 3 секунды, возможно EventLoop сам заметит, что надо
     * остановиться. По прошествии 3-ех секунд, закрываем epoll fd
     */
    if (EventLoop::wait_for_stopping(EventLoop::StopWaitTime))
        return;

    logger.write("Send notify to epoll");
    interrupter.interrupt();

    /**
     * Когда мы закрыли epoll fd, даем EventLoop еще 1 секунду, чтобы
     * завершить корректно свою работу
     */
    if (EventLoop::wait_for_stopping(EventLoop::WaitAfterCloseEpoll))
        return;

    throw std::runtime_error("Can not stop EventLoop");
}

bool EventLoop::wait_for_stopping(int ms) {
    Logger& logger = Logger::Instance();
    std::mutex mutex;

    auto start_wait_time = std::chrono::system_clock::now();
    while (state != State::Stopped) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_until(
            lock,
            start_wait_time + std::chrono::milliseconds(ms),
            [this]() { return state.load() == State::Stopped; }
        );

        if (state == State::Stopped) {
            logger.write("EventLoop stopped!");
            return true;
        }

        auto cur_time = std::chrono::system_clock::now();
        auto int_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            cur_time - start_wait_time
        );

        if (int_ms.count() > ms) {
            logger.write("EventLoop stop wait time is out");
            return false;
        }
    }

    return state == State::Stopped;
}

void EventLoop::clear_tasks_data() {
    Logger& logger = Logger::Instance();
    logger.write("Start clear");
    for (auto& item : events_tasks) {
        close(item.first);
        delete item.second;
    }
    logger.write("End clear");
}

void EventLooPInterrupter::interrupt() {
    uint64_t counter(1UL);
    int result;
    check_and_assign_sys_call(result, write(fd, &counter, sizeof(uint64_t)));
}

EventLooPInterrupter::EventLooPInterrupter(): epoll_fd(-1) {
    check_and_assign_sys_call(fd, eventfd(0, 0));
}

EventLooPInterrupter::~EventLooPInterrupter() {
    close(fd);
}

void EventLooPInterrupter::init(int epoll_fd) {
    this->epoll_fd = epoll_fd;
}
}
}
}