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
    int flags;

    check_and_assign_sys_call(flags, fcntl(sfd, F_GETFL, 0));

    flags |= O_NONBLOCK;
    check_sys_call(fcntl(sfd, F_SETFL, flags));
}

bool is_file_exists(const std::string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

void delete_if_need(const std::string& name, bool force) {
    Logger& logger = Logger::Instance();

    if (name == "/dev/null")
        return;

    if (is_file_exists(name)) {
        logger.write("File:", name, "already exists");
        logger.write("Going to unlink it");
        check_sys_call(unlink(name.c_str()));
    }
}


int make_fifo_file(const std::string& name) {
    int fd;
    if (is_file_exists(name))
        check_sys_call(unlink(name.c_str()));
    check_sys_call(mkfifo(name.c_str(), 0777));

    check_and_assign_sys_call(fd, open(name.c_str(), O_RDWR));
    make_socket_non_blocking(fd);

    return fd;
}

SmartString::SmartString(const char *buffer) : SmartString() {
    size_t len = strlen(buffer);
    _size = len;
    _end_pos = _size;

    _string = new char[len];
    std::memcpy(_string, buffer, len);
}

void SmartString::Put(const char *put_string, size_t put_length) {

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

SmartString::SmartString(SmartString &&from) {
    _string = from._string;
    from._string = nullptr;

    _size = from._size;
    _free_size = from._free_size;
    _start_pos = from._start_pos;
    _end_pos = from._end_pos;
}
} // namespace Utils
} // namespace Afina
