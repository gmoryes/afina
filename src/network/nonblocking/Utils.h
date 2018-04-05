#ifndef AFINA_NETWORK_NONBLOCKING_UTILS_H
#define AFINA_NETWORK_NONBLOCKING_UTILS_H

#include <string>
#include <protocol/Parser.h>
#include <afina/execute/Command.h>

/* Look at /proc/sys/net/ipv4/tcp_rmem and /proc/sys/net/ipv4/tcp_wmem
 * Min size of buffer is set to 4096, so let it 4096 in programm
 */
#define BUFFER_SIZE 4096

namespace Afina {
namespace Network {
namespace NonBlocking {

void make_socket_non_blocking(int sfd);

// Class Socket for read all data from it
class Socket {
public:
    explicit Socket(int fh);
    ~Socket() = default;

    // Read data from socket, return true is find command
    bool Read(std::string& out);

    // Write data to socket, return true if some data has been writen
    bool Write(std::string& out);

    // Check if was an error on socket
    bool socket_error() const;

    // Check if was an internal error
    bool internal_error() const;

    // Check if socket closed
    bool is_closed();

    Protocol::Parser::Command command;
    std::string& Body() { return body; }

private:
    int _fh;

    // Was error during operations with socket
    bool _socket_error;

    // Error, but we able to send error msg to client
    bool _internal_error;

    // Is socket closed
    bool _closed;

    Protocol::Parser parser;

    std::string body;
    std::string data;
};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_NONBLOCKING_UTILS_H
