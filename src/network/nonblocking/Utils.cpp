#include "Utils.h"

#include <stdexcept>
#include <algorithm>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

#include <logger/Logger.h>

namespace Afina {
namespace Utils {

void make_socket_non_blocking(int sfd) {
    int flags, s;
    Logger& logger = Logger::Instance();

    flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        logger.write("fcntl() -1, 1, errno =", errno, "fd =", sfd);
        throw std::runtime_error("Failed to call fcntl to get socket flags");
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if (s == -1) {
        logger.write("fcntl() -1, 2, errno =", errno, "fd =", sfd);
        throw std::runtime_error("Failed to call fcntl to set socket flags");
    }
}

bool is_file_exists(const std::string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

Socket::Socket(int read_fh, int write_fh = -1):
    _read_fh(read_fh),
    _socket_error(false),
    _internal_error(false),
    _all_data_send(true),
    _closed(false) {

    if (write_fh == -1) {
        _write_fh  = read_fh;
    } else {
        _write_fh = write_fh;
    }
}

bool Socket::is_closed() {
    return _closed;
}

bool Socket::Read(std::string &out) {

    Logger& logger = Logger::Instance();

    char buffer[BUFFER_SIZE];
    ssize_t has_read = 0;

    size_t parsed = 0;
    has_read = read(_read_fh, buffer, BUFFER_SIZE);
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

        size_t cur_parsed;
        bool find_command;
        try {
            //find_command = parser.Parse(out.data(), out.size(), cur_parsed);
        } catch (std::runtime_error& error) {
            logger.write("Error during parser.Parse(), desc:", error.what());
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
    } while ((has_read = read(_read_fh, buffer, BUFFER_SIZE)) > 0);

    if (!(has_read < 0 && errno == EAGAIN)) {
        logger.write("Error during read from socket(", _read_fh, "), errno =", errno);
        _socket_error = true;
    }

    return false;
}

bool Socket::SmartRead(SmartString& out) {
    Logger& logger = Logger::Instance();

    ssize_t has_read = 0;
    char buffer[BUFFER_SIZE];

    while ((has_read = read(_read_fh, buffer, BUFFER_SIZE)) > 0) {
        logger.write("Has read", has_read, "bytes from socket:", _read_fh);

        out.Put(buffer);

        size_t cur_parsed;
        bool find_command;
        try {
            find_command = parser.Parse(out, out.size(), cur_parsed);
        } catch (std::runtime_error& error) {
            logger.write("Error during parser.Parse(), desc:", error.what());
            _internal_error = true;
            return false;
        }

        out.Erase(cur_parsed);

        if (find_command) { // Get command

            uint32_t body_size;
            try {
                command = parser.Build(body_size);
            } catch (std::runtime_error& error) {
                logger.write("Error during Build(), desc:", error.what());
                _internal_error = true;
                return false;
            }

            body_size = (body_size == 0) ? body_size : body_size + 2; // \r\n

            if (out.size() < body_size)
                continue; // Read not enough, try again

            body = out.Copy(body_size); // Copy body from read buffer to current body
            out.Erase(body_size);       // Delete body from read buffer

            parser.Reset(); // Delete data, that we has parsed
            return true;
        }
    }

    if (has_read < 0) {
        if (errno == EAGAIN) {
            logger.write("Socket is overloaded, wait...");
            return true;
        } else {
            logger.write("Error during read(), errno =", errno);
            _socket_error = true;
            return false;
        }
    } else {
        logger.write("Client close connection, goodbye");
        _closed = true;
        return false;
    }
}

bool Socket::socket_error() const {
    return _socket_error;
}

bool Socket::internal_error() const {
    return _internal_error;
}

bool Socket::is_all_data_send() {
    if (_all_data_send) {
        _all_data_send = false;
        return true;
    } else {
        return false;
    }
}

bool Socket::Write(std::string &out) {

    if (!out.size()) {
        _all_data_send = true;
        return false;
    }

    Logger& logger = Logger::Instance();

    int has_send_all = 0;
    ssize_t has_send_now;
    bool success = false;

    while (has_send_all != out.size()) {

        has_send_now = send(
            _write_fh,
            out.data() + has_send_all,
            std::min(size_t(4096), size_t(out.size() - has_send_all)),
            0
        );

        if (has_send_now > 0) {
            // Send ok
            logger.write("Write to", _write_fh, has_send_now);
            has_send_all += has_send_now;
            success = true;
            continue;
        }

        if (has_send_now < 0 && errno == EAGAIN) {
            // Write again later
            logger.write("Socket(", _write_fh, ")_ is overhead now");
            success = true;
        } else if (has_send_now < 0 && errno != EAGAIN) {
            // send return -1, but errno != EAGAIN => error
            _socket_error = true;
            logger.write("Error during send data to socket(", _write_fh, "), errno = ", errno);
            success = false;
        } else {
            // Send return zero => socket is closed
            _closed = true;
            success = false;
        }
        break;
    }

    // Delete data, that we has sent
    out.erase(out.begin(), out.begin() + has_send_all);
    return success;
}

SmartString::SmartString(const char *buffer) : SmartString() {
    size_t len = strlen(buffer);
    _size = len;
    _end_pos = _size;

    _string = new char[len];
    std::memcpy(_string, buffer, len);
}

void SmartString::Put(const char *put_string) {
    size_t put_length = strlen(put_string);

    if (_free_size >= put_length) {
        // Если у нас есть место для новой строки
        for (size_t i = 0; i < put_length; i++)
            _string[(_end_pos + i) % _size] = put_string[i];

        _end_pos += put_length;
        _end_pos %= _size;
        _free_size -= put_length;
    } else {
        // Выделяем новую память в размере того, что нам не хватило
        auto new_string = new char[_size + put_length - _free_size];

        // Выделили памяти больше, теперь скопируем нашу непонятно как
        // лежащую строку в нормальном порядке в новую память
        for (size_t i = 0; i < _size; i++)
            new_string[i] = _string[(_start_pos + i) % _size];

        // Удаляем старую память
        if (_string)
            delete[] _string;
        _string = new_string;

        // Вычисляем конец строки (конечная позиция - это
        // вся память минус то, что было свободно)
        _end_pos = _size - _free_size;

        // Новый размер - это старый плюс то, что добавили
        _size = _size + put_length - _free_size;

        // Строка начинается теперь с 0
        _start_pos = 0;

        // Копируем строку, которую надо добавить
        // в конец нашей новой строки
        std::memcpy(_string + _end_pos, put_string, put_length);

        // Конец в конце :/
        _end_pos = _size;

        // Свободного места у нас нет
        _free_size = 0;
    }
}

void SmartString::Erase(size_t n_bytes) {
    size_t used_memory = _size - _free_size;
    if (n_bytes >= _size || n_bytes >= used_memory) {
        _start_pos = 0;
        _end_pos = 0;
        _free_size = _size;
        return;
    }

    // n_bytes < _size and n_bytes < used_memory
    // So we just move _start_pos for n_bytes
    _start_pos = (_start_pos + n_bytes) % _size;
    _free_size += n_bytes;
}

std::string SmartString::Copy(size_t n_bytes) {
    std::string result;
    result.reserve(n_bytes);

    for (size_t i = 0; i < n_bytes; i++)
        result.push_back(_string[(_start_pos + i) % _size]);

    return result;
}
} // namespace Utils
} // namespace Afina
