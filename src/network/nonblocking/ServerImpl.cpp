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

#define EPOLL_SIZE 10000

namespace Afina {

using namespace Utils;

namespace Network {
namespace NonBlocking {

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps):
    Server(std::move(ps)),
    r_fifo(-1),
    w_fifo(-1) {}

// See Server.h
ServerImpl::~ServerImpl() {}

bool ServerImpl::StartFIFO(const std::string& r_fifo_name,
                           const std::string& w_fifo_name,
                           bool force) {

    r_fifo = make_fifo_file(r_fifo_name);
    if (not w_fifo_name.empty())
        w_fifo = make_fifo_file(w_fifo_name);

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
    check_sys_call(pthread_sigmask(SIG_BLOCK, &sig_mask, NULL));

    // Create server socket
    struct sockaddr_in server_addr;
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

    make_socket_non_blocking(server_socket);
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    event_loop = std::make_shared<EventLoop>();
    event_loop->Start(EPOLL_SIZE);
    std::pair<int, int> fifo = std::make_pair(r_fifo, w_fifo);

    workers.reserve(n_workers);
    for (int i = 0; i < n_workers; i++) {
        auto new_worker = Worker::Create(pStorage, event_loop, fifo);
        workers.push_back(new_worker);
        workers.back()->Start(server_socket, i);
    }
}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    for (auto &worker : workers) {
        worker->Stop();
    }
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    for (auto &worker : workers) {
        worker->Join();
    }
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
