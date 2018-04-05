#ifndef AFINA_NETWORK_NONBLOCKING_UTILS_H
#define AFINA_NETWORK_NONBLOCKING_UTILS_H

#include <string>
#include <protocol/Parser.h>
#include <afina/execute/Command.h>

namespace Afina {
namespace Network {
namespace NonBlocking {

bool is_file_exists(const std::string&);

void make_socket_non_blocking(int sfd);

// Class Socket for read all data from it
class Socket {
public:

    // write_fh equal to read_fh by default
    Socket(int read_fh, int write_fh);
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

} // namespace NonBlocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_NONBLOCKING_UTILS_H
