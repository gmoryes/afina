#include "EventLoop.h"
#include <logger/Logger.h>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>

namespace Afina {
namespace Network {
namespace NonBlocking {

bool EventTask::process(uint32_t flags) {
    std::lock_guard<std::mutex> lock(mutex);
    Logger& logger = Logger::Instance();
    epoll_event ev{};

    if (flags & EPOLLIN) {
        if (not reader) { // Если нет reader'a, то пришло событие на server socket

            struct sockaddr_in client_addr;
            socklen_t sinSize = sizeof(struct sockaddr_in);

            // Создаем новое подключение
            int client_fh;
            check_and_assign_sys_call(client_fh, accept(fd, (struct sockaddr *) &client_addr, &sinSize));

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
            event_flags &= ~EPOLLOUT;

            ev.events = event_flags;
            ev.data.ptr = this;

            check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_MOD, fd, &ev));
        }
    }

    if (flags & EPOLLHUP) {
        return true; // Delete event from epoll
    }

    if (event_flags & EPOLLONESHOT) { // If event was EPOLLONESHOT, must re-arm fd
        ev.events = event_flags;
        ev.data.ptr = this;
        check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_MOD, fd, &ev));
    }

    return false;
}

void EventLoop::async_accept(int server_socket, std::function<bool(int)> func, uint32_t flags) {
    epoll_event ev{};

    if (not flags)
        flags = EPOLLIN | EPOLLHUP | EPOLLEXCLUSIVE;
    ev.events = flags;

    auto task = new EventTask(server_socket, flags, _epoll_fh);
    task->add_acceptor(std::move(func));
    ev.data.ptr = task;

    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, server_socket, &ev));
}

void EventLoop::async_read(int fd, std::function<bool(int, SmartString&)> func, uint32_t flags) {
    epoll_event ev{};
    if (not flags)
        flags = EPOLLIN | EPOLLET | EPOLLONESHOT;

    ev.events = flags;

    auto task = new EventTask(fd, flags, _epoll_fh);
    task->add_reader(std::move(func));
    ev.data.ptr = task;

    events_tasks[fd] = task;
    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, fd, &ev));
}


void EventLoop::async_write(int fd, std::string& must_be_written, uint32_t flags) {
    epoll_event ev{};
    auto it = events_tasks.find(fd);

    if (it != events_tasks.end()) {
        auto event_task = it->second;
        // Если в epoll уже лежит такой filehandler, в таком случае мы должны просто добавить
        // новое сообщение в таск этого filehandler'a

        if (event_task->message_queue_empty()) { // Если сообщений еще нет, надо добавить флаг EPOLLOUT

            if (not flags)
                flags = EPOLLOUT;

            event_task->event_flags |= flags;

            ev.events = event_task->event_flags;
            ev.data.ptr = event_task;
            //check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_MOD, fd, &ev));
        }

        event_task->add_message_to_write(must_be_written); // Добавляем в очередь новое сообщение

    } else {
        // Иначе добавляем новое событие в epoll
        epoll_event ev{};
        if (not flags)
            flags = EPOLLOUT | EPOLLET | EPOLLONESHOT;
        ev.events = flags;

        auto task = new EventTask(fd, flags, _epoll_fh);
        task->add_message_to_write(must_be_written);
        ev.data.ptr = task;

        events_tasks[fd] = task;
        check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, fd, &ev));
    }
}

void EventLoop::loop() {
    Logger& logger = Logger::Instance();

    while (not _stop) {
        int events_count = epoll_wait(_epoll_fh, _events, _max_events_number, -1); // -1 - Timeout
        logger.write("Wake up, event recv(", events_count, ")");
        for (int i = 0; i < events_count; i++) {
            auto task = static_cast<EventTask*>(_events[i].data.ptr);
            logger.write("Get task(", task->fd, ")");
            bool should_delete = false, was_error = false;
            try {
                should_delete = task->process(_events[i].events);
            } catch (std::runtime_error& error) {
                logger.write("Can not execute task, desc:", error.what());
                was_error = true;
            }

            if (should_delete || was_error) {
                logger.write("Delete from epoll:", task->fd);
                //delete_event(task->fd);
                delete task;
            }
        }
    }
}

bool EventLoop::Start(int events_max_number) {
    _max_events_number = events_max_number;
    _events = new epoll_event[events_max_number];

    check_and_assign_sys_call(_epoll_fh, epoll_create(events_max_number));
}

void EventLoop::delete_event(int fd) {
    epoll_event ev{};

    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_DEL, fd, &ev));
}

}
}
}