#ifndef AFINA_CLIENT_H
#define AFINA_CLIENT_H

#include <unistd.h>
#include <network/nonblocking/Utils.h>
#include <logger/Logger.h>
#include <memory>
#include <afina/Storage.h>

/* Look at /proc/sys/net/ipv4/tcp_rmem and /proc/sys/net/ipv4/tcp_wmem
 * Min size of buffer is set to 4096, so let it 4096 in programm
 */
#define BUFFER_SIZE 4096

namespace Afina {
namespace Network {
namespace Coroutines {

class Client {
public:
    explicit Client(int fd, std::shared_ptr<Afina::Storage> &storage):
        _fd(fd), _connected(true), storage(storage) {}

    ~Client() {
        close(_fd);
    }

    // Read data from _fd
    void Read();

    // Write data to _fd
    void Write();

    // Process the data, that has been read
    bool Process();

    bool connected() const {
        return _connected;
    }

private:
    // File descriptor for client write/read
    int _fd;

    // Flag if client is still connected
    bool _connected;

    Utils::SmartString read_buffer;
    Utils::SmartString write_buffer;

    Protocol::Parser parser;
    std::shared_ptr<Afina::Storage> storage;
};

}
}
}

#endif //AFINA_CLIENT_H
