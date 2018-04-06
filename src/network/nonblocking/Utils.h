#ifndef AFINA_NETWORK_NONBLOCKING_UTILS_H
#define AFINA_NETWORK_NONBLOCKING_UTILS_H

#include <string>
#include <protocol/Parser.h>
#include <afina/execute/Command.h>

#include <cstring>

/* Look at /proc/sys/net/ipv4/tcp_rmem and /proc/sys/net/ipv4/tcp_wmem
 * Min size of buffer is set to 4096, so let it 4096 in programm
 */
#define BUFFER_SIZE 4096

namespace Afina {

namespace Utils {

bool is_file_exists(const std::string&);

void make_socket_non_blocking(int sfd);

/*
 * Класс представляет собой зацикленную строку, динамической длины
 */
class SmartString {
public:
    // Конструктор по умолчанию
    SmartString(): _string(nullptr), _free_size(0), _start_pos(0), _end_pos(0) {};

    // Конструктор от char*
    SmartString(const char* buffer);

    // Нам нужна операция += (быстрая)
    void Put(const char* put_string);

    // Нам по факту надо освобождать только первые n байт
    // Функция сдвигает указатель на начало строки на n
    // Она ничего не освобождает итд итп
    void Erase(size_t n_bytes);

    // Возвращает первые n байт строки
    std::string Copy(size_t n_bytes);

    char& operator[] (size_t i) const {
        if (i >= _size)
            return _string[_size - 1];

        return _string[(_start_pos + i) % _size];
    }

    char& operator[] (size_t i) {
        if (i >= _size)
            return _string[_size - 1];

        return _string[(_start_pos + i) % _size];
    }

    size_t size() const {
        return _size - _free_size;
    }

    ~SmartString() {
        if (_string)
            delete[] _string;
    }
private:

    // Сама строка
    char* _string;

    // Сколько памяти выделено
    size_t _size;

    // Сколько свободного места осталось
    size_t _free_size;

    // Где начало строки в нашем зацикленном массиве
    size_t _start_pos;

    // Где конец
    size_t _end_pos;

};

// Class Socket for read all data from it
class Socket {
public:

    // write_fh equal to read_fh by default
    Socket(int read_fh, int write_fh);
    ~Socket() = default;

    // Read data from socket, return true is find command
    bool Read(std::string& out);

    bool SmartRead(SmartString& out);

    // Write data to socket, return true if some data has been writen
    bool Write(std::string& out);

    // Check if was an error on socket
    bool socket_error() const;

    // Check if was an internal error
    bool internal_error() const;

    // Check if socket closed
    bool is_closed();

    // Check if we has sent all data, for set down EPOLLOUT flag
    bool is_all_data_send();

    Protocol::Parser::Command command;
    std::string& Body() { return body; }

private:
    int _read_fh;
    int _write_fh;

    // Was error during operations with socket
    bool _socket_error;

    // Error, but we able to send error msg to client
    bool _internal_error;

    // Is socket closed
    bool _closed;

    // Is all data has sent
    bool _all_data_send;

    Protocol::Parser parser;

    std::string body;
    std::string data;
};

} // namespace Utils
} // namespace Afina

#endif // AFINA_NETWORK_NONBLOCKING_UTILS_H
