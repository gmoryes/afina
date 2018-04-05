#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <afina/Storage.h>

#include "Utils.h"
#include "Worker.h"

namespace Afina {
namespace Network {
namespace NonBlocking {

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps):
    Server(std::move(ps)),
    r_fifo(-1),
    w_fifo(-1) {}

// See Server.h
ServerImpl::~ServerImpl() {}

bool delete_if_need(const std::string& name, bool force) {
    Logger& logger = Logger::Instance();

    if (name == "/dev/null")
        return true;

    if (is_file_exists(name)) {
        logger.write("File:", name, "already exists");
        logger.write("Going to unlink it");
        if (unlink(name.c_str()) < 0) {
            logger.write("unlink() -1, errno =", errno);
            return false;
        } else {
            return true;
        }
    }
}

bool ServerImpl::StartFIFO(const std::string& r_fifo_name,
                           const std::string& w_fifo_name,
                           bool force) {

    Logger& logger = Logger::Instance();

    delete_if_need(r_fifo_name, force);
    delete_if_need(w_fifo_name, force);

    if (mkfifo(r_fifo_name.c_str(), 0666) < 0) {
        logger.write("mkfifo()1 -1, errno =", errno);
        return false;
    }

    if (mkfifo(w_fifo_name.c_str(), 0666) < 0) {
        logger.write("mkfifo()2 -1, errno =", errno);
        return false;
    }

    r_fifo = open(r_fifo_name.c_str(), O_RDWR);
    if (r_fifo < 0) {
        logger.write("Can not open file:", r_fifo_name,
                     "for O_RDWR, errno =", errno);
        return false;
    }

    w_fifo = open(w_fifo_name.c_str(), O_RDWR);
    if (w_fifo < 0) {
        logger.write("Can not open file:", w_fifo_name,
                     "for O_RDWR, errno =", errno);
        return false;
    }

    make_socket_non_blocking(r_fifo);
    make_socket_non_blocking(w_fifo);

    return true;

    return false;

    r_fifo = open(r_fifo_name.c_str(), O_RDONLY);
    if (r_fifo <= 0) {
        logger.write("Can not open file", r_fifo_name, "errno =", errno);
        return false;
    }

    w_fifo = open(w_fifo_name.c_str(), O_WRONLY);
    if (w_fifo <= 0) {
        logger.write("Can not open file", w_fifo_name, "errno =", errno);
        return false;
    }

    return true;
}

// No thread pool in this server type
void ServerImpl::StartThreadPool(size_t, size_t, size_t, size_t) {}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    Logger& logger = Logger::Instance();

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Create server socket
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    int server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    make_socket_non_blocking(server_socket);
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    std::pair<int, int> fifo = std::make_pair(r_fifo, w_fifo);
    for (int i = 0; i < n_workers; i++) {
        workers.emplace_back(pStorage, fifo);
    }

    for (int i = 0; i < n_workers; i++) {
        workers[i].Start(server_socket, i);
    }

}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    for (auto &worker : workers) {
        worker.Stop();
    }
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    for (auto &worker : workers) {
        worker.Join();
    }
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
