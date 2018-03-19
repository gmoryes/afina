#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <thread>

#include <pthread.h>
#include <signal.h>
#include <chrono>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <afina/Storage.h>
#include <protocol/Parser.h>
#include <afina/execute/Command.h>

#include <sys/time.h>
#include <chrono>
#include <ctime>

#include <logger/Logger.h>
#include <executor/Executor.h>

namespace Afina {
namespace Network {
namespace Blocking {

void *ServerImpl::RunAcceptorProxy(void *p) {
    ServerImpl *srv = reinterpret_cast<ServerImpl *>(p);
    try {
        srv->RunAcceptor();
    } catch (std::runtime_error &ex) {
        std::cerr << "Server fails: " << ex.what() << std::endl;
    }
    return 0;
}

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps) : Server(ps) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint32_t port,
                       uint16_t workers = 1,
                       size_t low_watermark = 1,
                       size_t hight_watermark = 1,
                       size_t max_queue_size = 1,
                       size_t idle_time = 100) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    thread_pool.Start(
        low_watermark,
        hight_watermark,
        max_queue_size,
        idle_time
    );

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Setup server parameters BEFORE thread created, that will guarantee
    // variable value visibility
    listen_port = port;

    // The pthread_create function creates a new thread.
    //
    // The first parameter is a pointer to a pthread_t variable, which we can use
    // in the remainder of the program to manage this thread.
    //
    // The second parameter is used to specify the attributes of this new thread
    // (e.g., its stack size). We can leave it NULL here.
    //
    // The third parameter is the function this thread will run. This function *must*
    // have the following prototype:
    //    void *f(void *args);
    //
    // Note how the function expects a single parameter of type void*. We are using it to
    // pass this pointer in order to proxy call to the class member function. The fourth
    // parameter to pthread_create is used to specify this parameter value.
    //
    // The thread we are creating here is the "server thread", which will be
    // responsible for listening on port 23300 for incoming connections. This thread,
    // in turn, will spawn threads to service each incoming connection, allowing
    // multiple clients to connect simultaneously.
    // Note that, in this particular example, creating a "server thread" is redundant,
    // since there will only be one server thread, and the program's main thread (the
    // one running main()) could fulfill this purpose.
    running.store(true);
    if (pthread_create(&accept_thread, NULL, ServerImpl::RunAcceptorProxy, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    running.store(false);
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_join(accept_thread, 0);

    //threads_status.join();
    thread_pool.Stop(true);
}

// See Server.h
void ServerImpl::RunAcceptor() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    Logger& logger = Logger::Instance();

    // For IPv4 we use struct sockaddr_in:
    // struct sockaddr_in {
    //     short int          sin_family;  // Address family, AF_INET
    //     unsigned short int sin_port;    // Port number
    //     struct in_addr     sin_addr;    // Internet address
    //     unsigned char      sin_zero[8]; // Same size as struct sockaddr
    // };
    //
    // Note we need to convert the port to network order

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(listen_port); // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    // Arguments are:
    // - Family: IPv4
    // - Type: Full-duplex stream (reliable)
    // - Protocol: TCP
    int server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    // when the server closes the socket,the connection must stay in the TIME_WAIT state to
    // make sure the client received the acknowledgement that the connection has been terminated.
    // During this time, this port is unavailable to other processes, unless we specify this option
    //
    // This option let kernel knows that we are OK that multiple threads/processes are listen on the
    // same port. In a such case kernel will balance input traffic between all listeners (except those who
    // are closed already)
    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    // Bind the socket to the address. In other words let kernel know data for what address we'd
    // like to see in the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    // Start listening. The second parameter is the "backlog", or the maximum number of
    // connections that we'll allow to queue up. Note that listen() doesn't block until
    // incoming connections arrive. It just makes the OS aware that this process is willing
    // to accept connections on this socket (which is bound to a specific IP and port)
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t sinSize = sizeof(struct sockaddr_in);

    fd_set rfds;
    struct timeval tv;


    int select_retval;
    logger.i_am(std::string("MASTER"));
    int task_id = 0;
    while (running.load()) {

        tv.tv_sec = 0;
        tv.tv_usec = 1000000;
        FD_ZERO(&rfds);
        FD_SET(server_socket, &rfds);
        select_retval = select(server_socket + 1, &rfds, NULL, NULL, &tv);
        if (select_retval == -1) {
            std::cerr << "[main] Error while select" << std::endl;
            break;
        } else if (!select_retval) {
            continue;
        }

        bool close_immediately = !thread_pool.can_add();
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
            close(server_socket);
            throw std::runtime_error("Socket accept() failed");
        }

        // If recv INT signal
        if (!running.load()) {
            logger.write("I'm going to die");
            close(server_socket);
            continue;
        }

        // If no workers
        if (close_immediately) {
            logger.write("ThreadPool is buzy, close connection :(");
            close(client_socket);
            continue;
        }

        try {
            thread_pool.lock();
            thread_pool.Execute(Task(pStorage, client_socket));
            thread_pool.unlock();
            task_id++;
        } catch (std::runtime_error &ex) {
            std::cerr << ex.what() << std::endl;
        }

    }
    close(server_socket);
}

Socket::Socket(int fh) : _fh(fh), _good(true), _empty(false) {}

Socket::~Socket() {}

void Socket::Read(std::string &out) {

    Logger& logger = Logger::Instance();

    char buffer[32];
    ssize_t has_read = 0;

    size_t parsed = 0;
    has_read = read(_fh, buffer, 32);
    if (!has_read) {
        _empty = true;
        return;
    }

    if (has_read < 0) {
        _good = false;
        return;
    }

    do {
        logger.write("has_read =", has_read);

        out += std::string(buffer, buffer + has_read);

        size_t cur_parsed;
        bool find_command = parser.Parse(std::string(out.data() + parsed), cur_parsed);
        parsed += cur_parsed;
        if (find_command) {
            // Get command

            uint32_t body_size;
            command = parser.Build(body_size);
            body = std::string(out.begin() + parsed,
                               out.begin() + parsed + body_size);

            body_size = (body_size == 0) ? body_size : body_size + 2; // \r\n

            if (out.size() < parsed + body_size) {
                // Read not enough, try again
                continue;
            }

            out.erase(out.begin(), out.begin() + parsed + body_size);
            parser.Reset();
            return;
        }
    } while ((has_read = read(_fh, buffer, 32)) > 0);

    // Should exit in loop
    _good = false;
}

bool Socket::good() const {
    return _good;
}

bool Socket::is_empty() const {
    return _empty;
}

bool Socket::_make_non_blocking() {
    Logger& logger = Logger::Instance();
    int flags = fcntl(_fh, F_GETFL, 0);
    if (fcntl(_fh, F_SETFL, flags | O_NONBLOCK)) {

        logger.write("Can not change flags of file handler =", _fh);

        return false;
    }

    return true;
}

void Socket::Write(std::string &out) {
    Logger& logger = Logger::Instance();
    int has_send_all = 0;
    ssize_t has_send_now;
    while (has_send_all != out.size()) {
        has_send_now = send(_fh, out.data(), out.size(), 0);
        if (has_send_now == -1) {
            logger.write("Error durind send data to", _fh, "errno =", errno);
            _good = false;
            break;
        } else {
            logger.write("Write to", _fh, has_send_now);
            has_send_all += has_send_now;
        }
    }
}
} // namespace Blocking
} // namespace Network
} // namespace Afina
