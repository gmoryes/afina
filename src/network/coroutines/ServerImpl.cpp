#include "ServerImpl.h"
#include <logger/Logger.h>
#include <signal.h>
#include <network/nonblocking/Utils.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <afina/Storage.h>

namespace Afina {
namespace Network {
namespace Coroutines {

ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps):
    Server(std::move(ps)) {}

ServerImpl::~ServerImpl() = default;

void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    Logger& logger = Logger::Instance();

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    check_sys_call(pthread_sigmask(SIG_BLOCK, &sig_mask, NULL));

    // Create server socket
    struct sockaddr_in server_addr{};
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    int server_socket;
    check_and_assign_sys_call(server_socket, socket(PF_INET, SOCK_STREAM, IPPROTO_TCP));

    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    Utils::make_socket_non_blocking(server_socket);
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    workers.reserve(n_workers);
    for (int i = 0; i < n_workers; i++) {
        auto new_worker = new Worker(pStorage, i, server_socket);
        workers.emplace_back(new_worker);
        workers.back()->Start();
    }
}

void ServerImpl::Stop() {
    for (auto &worker : workers) {
        worker->Stop();
    }
}

void ServerImpl::Join() {
    for (auto &worker : workers) {
        worker->Join();
    }
}

}
}
}

