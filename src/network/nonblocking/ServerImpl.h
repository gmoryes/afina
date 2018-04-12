#ifndef AFINA_NETWORK_NONBLOCKING_SERVER_H
#define AFINA_NETWORK_NONBLOCKING_SERVER_H

#include <vector>

#include <afina/network/Server.h>
#include "EventLoop.h"

namespace Afina {
namespace Network {
namespace NonBlocking {

// Forward declaration, see Worker.h
class Worker;

/**
 * # Network resource manager implementation
 * Epoll based server
 */
class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<Afina::Storage> ps);
    ~ServerImpl();

    // See Server.h
    void Start(uint32_t, uint16_t) override;
    void StartThreadPool(size_t, size_t, size_t, size_t) override;
    bool StartFIFO(const std::string& r_fifo, const std::string& w_fifo, bool force) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

private:
    // Port to listen for new connections, permits access only from
    // inside of accept_thread
    // Read-only
    uint32_t listen_port;

    // Thread that is accepting new connections
    std::vector<std::shared_ptr<Worker>> workers;

    // File handlers for fifo files
    int r_fifo, w_fifo;

    // EventLoop
    std::shared_ptr<EventLoop> event_loop;
};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_NONBLOCKING_SERVER_H
