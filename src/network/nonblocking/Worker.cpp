#include "Worker.h"

#include <iostream>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "Utils.h"

#include <string>
#include <sstream>
#include <utility>
#include <logger/Logger.h>

#define EPOLL_SIZE 10000

namespace Afina {
namespace Network {
namespace NonBlocking {

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps,
               const std::pair<int, int>& fifo):
    stop(false),
    storage(std::move(ps)),
    fifo(fifo) {}

// See Worker.h
Worker::~Worker() {
    stop = false;
}

// See Worker.h
void Worker::OnRun(int server_socket, int r_fifo, int worker_number) {
    Logger& logger = Logger::Instance();

    std::stringstream ss;
    ss << "WORKER_" << worker_number;
    logger.i_am(ss.str());

    logger.write("Hello");

    struct epoll_event ev, events[EPOLL_SIZE];

    int epoll_fd;
    epoll_fd = epoll_create(EPOLL_SIZE);
    if (epoll_fd < 0) {
        logger.write("epoll_create() -1, errno =", errno);
        return;
    }

    // Add server socket
    ev.data.fd = server_socket;
    ev.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev);

    // Add fifo if need
    if (fifo.first != -1) {
        ev.data.fd = fifo.first;
        ev.events = EPOLLIN | EPOLLEXCLUSIVE;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fifo.first, &ev);
    }

    int events_count;
    struct sockaddr_in client_addr;
    socklen_t sinSize = sizeof(struct sockaddr_in);

    while(not stop) {
        events_count = epoll_wait(epoll_fd, events, EPOLL_SIZE, -1); // Timeout -1

        for (int i = 0; i < events_count; i++) {
            int socket_fh = events[i].data.fd;
            if (socket_fh == server_socket) {
                int client = accept(server_socket, (struct sockaddr *) &client_addr, &sinSize);
                if (client < 0) {
                    logger.write("accept() return -1, errno:", errno);
                    continue;
                }
                make_socket_non_blocking(client);
                ev.data.fd = client;
                ev.events = EPOLLIN;

                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client, &ev);

                tasks.insert(std::make_pair(client, std::move(Task(client))));
            } else {
                auto& task = tasks[socket_fh];
                task.process(storage, events[i].events);
                if (task.can_be_deleted()) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, socket_fh, &ev);
                    tasks.erase(socket_fh);
                }
            }
        }
    }
}

// See Worker.h
void Worker::Start(int server_socket, int worker_number) {
    Logger& logger = Logger::Instance();

    try {
        std::thread thread(&Worker::OnRun, this, server_socket, fifo.first, worker_number);
        thread.detach();
    } catch (std::runtime_error& error) {
        logger.write(
            "Error while creating", worker_number, "worker",
            "Error:", error.what()
        );
    }
}

// See Worker.h
void Worker::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    stop = true;
}

// See Worker.h
void Worker::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // TODO: Nothing here lol
}


} // namespace NonBlocking
} // namespace Network
} // namespace Afina
