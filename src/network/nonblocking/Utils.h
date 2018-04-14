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

#define check_sys_call(call) \
    if ((call) < 0) { \
        std::stringstream s; \
        s << #call << " return -1" << " errno = " << errno \
          << ", at line: " <<  __LINE__ << ", file: " << __FILE__; \
        throw std::runtime_error(s.str()); \
    }

#define check_and_assign_sys_call(res, call) \
    if (((res) = (call)) < 0) { \
        std::stringstream s; \
        s << #call << " return: " << res << " errno = " << errno \
          << ", at line: " <<  __LINE__ << ", file: " << __FILE__; \
        throw std::runtime_error(s.str()); \
    }

namespace Afina {

namespace Utils {

bool is_file_exists(const std::string&);
int make_fifo_file(const std::string&);
void make_socket_non_blocking(int sfd);

/*
 * Класс представляет собой зацикленную строку, динамической длины
 */
class SmartString {
public:
    // Конструктор по умолчанию
    SmartString(): _string(nullptr), _size(0), _free_size(0), _start_pos(0), _end_pos(0) {};

    // Конструктор от char*
    explicit SmartString(const char* buffer);

    // Конструктор от std::string, вызываем конструктор от char*
    explicit SmartString(std::string& data): SmartString(data.c_str()) {};

    //TODO add operator= from rvalue
    // Move constructor
    SmartString(SmartString&& from);

    ~SmartString() {
        if (_string)
            delete[] _string;
    }

    // Нам нужна операция += (быстрая)
    void Put(const char* put_string, size_t put_length);

    /**
     * Нам по факту надо освобождать только первые n байт
     * Функция сдвигает указатель на начало строки на n
     * Она ничего не освобождает итд итп
     * @param n_bytes - сколько байт надо удалить сначала
     */
    void Erase(size_t n_bytes);

    // Возвращает первые n байт строки
    std::string Copy(size_t n_bytes);

    char* data() {
        return _string + _start_pos;
    }

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

    bool empty() const {
        return _free_size == _size;
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

} // namespace Utils
} // namespace Afina

#endif // AFINA_NETWORK_NONBLOCKING_UTILS_H
