#include "Utils.h"

#include <stdexcept>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <logger/Logger.h>

namespace Afina {
namespace Network {
namespace NonBlocking {

void make_socket_non_blocking(int sfd) {
    int flags, s;

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("Failed to call fcntl to get socket flags");
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1) {
        throw std::runtime_error("Failed to call fcntl to set socket flags");
    }
}

Socket::Socket(int fh) : _fh(fh), _good(true), _empty(false), _closed(false) {}

Socket::~Socket() {}

bool Socket::is_closed() {
    return _closed;
}

bool Socket::Read(std::string &out) {

    Logger& logger = Logger::Instance();

    char buffer[32];
    ssize_t has_read = 0;

    size_t parsed = 0;
    has_read = read(_fh, buffer, 32);
    if (!has_read) {
        _closed = true;
        return false;
    }

    if (has_read < 0 && errno != EAGAIN) {
        _good = false;
        return false;
    }

    do {
        logger.write("has_read =", has_read);

        out += std::string(buffer, buffer + has_read);

        logger.write("Current buffer:", out);

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
            return true;
        }
    } while ((has_read = read(_fh, buffer, 32)) > 0);

    if (!(has_read < 0 && errno == EAGAIN)) {
        logger.write("Error during read from socket(", _fh, "), errno =", errno);
        _good = false;
    }

    return false;
}

bool Socket::good() const {
    return _good;
}

bool Socket::is_empty() const {
    return _empty;
}

bool Socket::Write(std::string &out) {
    Logger& logger = Logger::Instance();

    int has_send_all = 0;
    ssize_t has_send_now;
    bool result = false;

    while (has_send_all != out.size()) {
        has_send_now = send(_fh, out.data(), out.size(), 0);
        if (has_send_now < 0 && errno == EAGAIN) {
            logger.write("Socket(", _fh, ")_ is overhead now");
            result = true;
        } else if (has_send_now < 0 && errno != EAGAIN) {
            _good = false;
            logger.write("Error during send data to socket(", _fh, "), errno = ", errno);
        } else if (has_send_now) {
            logger.write("Write to", _fh, has_send_now);
            has_send_all += has_send_now;
            result = true;
        } else {
            _closed = true;
        }
    }

    // Delete data, that we has sent
    out.erase(out.begin(), out.begin() + has_send_all);
    return result;
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
