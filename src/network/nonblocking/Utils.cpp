#include "Utils.h"

#include <stdexcept>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

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

bool is_file_exists(const std::string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

Socket::Socket(int fh) : _fh(fh),
                         _socket_error(false),
                         _internal_error(false),
                         _closed(false) {}

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
        _socket_error = true;
        return false;
    }

    do {
        logger.write("has_read =", has_read);

        out += std::string(buffer, buffer + has_read);

        logger.write("Current buffer:", out);

        size_t cur_parsed;
        bool find_command;
        try {
            find_command = parser.Parse(std::string(out.data() + parsed), cur_parsed);
        } catch (std::runtime_error& error) {
            logger.write("Error during parser.Parce(), desc:", error.what());
            _internal_error = true;
            return false;
        }
        parsed += cur_parsed;
        if (find_command) {
            // Get command

            uint32_t body_size;
            try {
                command = parser.Build(body_size);
            } catch (std::runtime_error& error) {
                logger.write("Error during Build(), desc:", error.what());
                _internal_error = true;
                return false;
            }
            body = std::string(out.begin() + parsed,
                               out.begin() + parsed + body_size);

            body_size = (body_size == 0) ? body_size : body_size + 2; // \r\n

            if (out.size() < parsed + body_size) {
                // Read not enough, try again
                continue;
            }

            // Delete data, that we has parsed
            out.erase(out.begin(), out.begin() + parsed + body_size);
            parser.Reset();
            return true;
        }
    } while ((has_read = read(_fh, buffer, 32)) > 0);

    if (!(has_read < 0 && errno == EAGAIN)) {
        logger.write("Error during read from socket(", _fh, "), errno =", errno);
        _socket_error = true;
    }

    return false;
}

bool Socket::socket_error() const {
    return _socket_error;
}

bool Socket::internal_error() const {
    return _internal_error;
}

bool Socket::Write(std::string &out) {
    Logger& logger = Logger::Instance();

    int has_send_all = 0;
    ssize_t has_send_now;
    bool result = false;

    while (has_send_all != out.size()) {

        has_send_now = send(_fh, out.data(), out.size(), 0);

        if (has_send_now < 0 && errno == EAGAIN) {
            // Write again later
            logger.write("Socket(", _fh, ")_ is overhead now");
            result = true;
        } else if (has_send_now < 0 && errno != EAGAIN) {
            // send return -1, but errno != EAGAIN => error
            _socket_error = true;
            logger.write("Error during send data to socket(", _fh, "), errno = ", errno);
        } else if (has_send_now > 0) {
            // Send ok
            logger.write("Write to", _fh, has_send_now);
            has_send_all += has_send_now;
            result = true;
        } else {
            // Send return zero => socket is closed
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
