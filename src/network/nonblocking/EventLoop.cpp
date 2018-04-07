#include "EventLoop.h"
#include <logger/Logger.h>
#include <netinet/in.h>
#include <unistd.h>

namespace Afina {
namespace Network {
namespace NonBlocking {

void EventTask::process(uint32_t flags) {
    Logger& logger = Logger::Instance();

    if (flags & EPOLLIN) {
        if (_is_server) {

            struct sockaddr_in client_addr;
            socklen_t sinSize = sizeof(struct sockaddr_in);

            // Создаем новое подключение
            int client_fh;
            check_and_assign_sys_call(client_fh, accept(_read_fh, (struct sockaddr *) &client_addr, &sinSize));

            logger.write("Get new client =", client_fh);

            make_socket_non_blocking(client_fh);

            // Вызываем callback, который нам передали, передаем
            // ему в аргументы новый сокет
            acceptor(client_fh);

        } else {

            ssize_t has_read = 0;
            char buffer[BUFFER_SIZE];

            while ((has_read = read(_read_fh, buffer, BUFFER_SIZE)) > 0) {
                logger.write("Has read", has_read, "bytes from socket:", _read_fh);

                read_buffer.Put(buffer);

                bool should_continue = reader(read_buffer, *this);

                if (not should_continue) {
                    logger.write("End working with:", _read_fh);
                    //TODO should delete this socket from epoll
                }
            }

            if (has_read < 0) {
                if (errno == EAGAIN) {
                    logger.write("Socket is overloaded, wait...");
                } else {
                    logger.write("Error during read(), errno =", errno);
                    //TODO should delete this socket from epoll
                }
            } else {
                logger.write("Client close connection, goodbye");
                //TODO should delete this socket from epoll
            }
        }
    }
}

void EventLoop::async_accept(int server_socket, std::function<bool(int)>&& func) {
    epoll_event ev{};
    ev.data.fd = server_socket;
    ev.events = EPOLLIN | EPOLLHUP;

    auto task = new EventTask(server_socket, true);
    task->add_acceptor(std::move(func));
    ev.data.ptr = task;

    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, server_socket, &ev));
}

void EventLoop::async_read(int read_fh, std::function<bool(SmartString&, EventTask&)>&& func) {
    epoll_event ev{};
    ev.data.fd = read_fh;
    ev.events = EPOLLIN | EPOLLHUP;

    auto task = new EventTask(read_fh);
    task->add_reader(std::move(func));
    ev.data.ptr = task;

    check_sys_call(epoll_ctl(_epoll_fh, EPOLL_CTL_ADD, read_fh, &ev));
}


void EventLoop::async_write(std::string &must_be_written, EventTask *event_task) {

}

void EventLoop::loop() {
    Logger& logger = Logger::Instance();

    while (not _stop) {
        int events_count = epoll_wait(_epoll_fh, _events, _max_events_number, -1); // -1 - Timeout

        for (int i = 0; i < events_count; i++) {
            int events = _events[i].events;

            auto task = static_cast<EventTask*>(_events[i].data.ptr);
            try {
                task->process(_events[i].events);
            } catch (std::runtime_error& error) {
                logger.write("Can not execute task, desc:", error.what());
            }
        }
    }
}

bool EventLoop::Start(int events_max_number) {
    _max_events_number = events_max_number;
    _events = new epoll_event[events_max_number];

    check_and_assign_sys_call(_epoll_fh, epoll_create(events_max_number));
}


}
}
}