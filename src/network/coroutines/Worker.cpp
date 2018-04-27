#include "Worker.h"
#include <logger/Logger.h>
#include <sstream>
#include <thread>
#include <netinet/in.h>
#include <unistd.h>
#include <network/nonblocking/Utils.h>
#include "Client.h"

namespace Afina {
namespace Network {
namespace Coroutines {

void client(Worker *&worker, int &fd) {
    Logger& logger = Logger::Instance();

    Client client(fd, worker->storage);

    while (client.connected()) {
        client.Read();

        if (not client.Process()) {
            logger.write("Internal error during process the input data, close connection");
            break;
        }

        client.Write();
        worker->engine.yield();
    }
}

void acceptor(Worker *worker, int &server_socket) {
    Logger& logger = Logger::Instance();

    struct sockaddr_in client_addr{};
    socklen_t sinSize = sizeof(struct sockaddr_in);

    while (not worker->stop.load()) {

        // Wait for new connections
        int client_fd;
        client_fd = accept(server_socket, (struct sockaddr *) &client_addr, &sinSize);
        if (client_fd == -1) {
            if (errno == EAGAIN) {
                // If no new connections, let do another work
                worker->engine.yield();
            } else {
                logger.write("Error during accept(), errno =", errno);
                worker->stop.store(true);
            }
            continue;
        }

        logger.write("Get new client:", client_fd);
        Utils::make_socket_non_blocking(client_fd);

        auto new_coro = worker->engine.run(&client, worker, client_fd);
        worker->engine.sched(new_coro);
    }
}

void Worker::Run() {
    Logger& logger = Logger::Instance();

    std::stringstream ss;
    ss << "CORO_WORKER_" << worker_number;
    logger.i_am(ss.str());
    logger.write("Hello");

    engine.start(&acceptor, this, server_socket);
    //event_loop.loop()

    has_stopped.store(true);
    stop_cv.notify_all();
}

void Worker::Start() {
    Logger &logger = Logger::Instance();

    try {
        std::thread thread(&Worker::Run, this);
        thread.detach();
    } catch (std::runtime_error &error) {
        logger.write("Error during create thread:", error.what());
    }
}

void Worker::Stop() {
    stop.store(true);
}

void Worker::Join() {
    Logger& logger = Logger::Instance();

    std::unique_lock<std::mutex> lock(mutex);

    while (not has_stopped.load()) {
        stop_cv.wait(lock);
    }

    logger.write("CORO_WORKER", worker_number, "has stopped");
}

}
}
}