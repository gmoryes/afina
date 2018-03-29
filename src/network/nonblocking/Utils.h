#ifndef AFINA_NETWORK_NONBLOCKING_UTILS_H
#define AFINA_NETWORK_NONBLOCKING_UTILS_H

#include <string>
#include <protocol/Parser.h>
#include <afina/execute/Command.h>

namespace Afina {
namespace Network {
namespace NonBlocking {

void make_socket_non_blocking(int sfd);

// Class Socket for read all data from it
class Socket {
public:
    Socket(int fh);
    ~Socket();

    // Read all data from socket
    bool Read(std::string& out);

    // Write all data to socket
    bool Write(std::string& out);

    // Check if was an error
    bool good() const;

    // Check the availability of data on socket right NOW
    bool is_empty() const;

    // Check if socket closed
    bool is_closed();

    Protocol::Parser::Command command;
    std::string& Body() { return body; }

private:
    int _fh;

    // Was error during operations
    bool _good;

    // Is no data in socket right NOW
    bool _empty;

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
