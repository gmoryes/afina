#ifndef AFINA_SERVERIMPL_H
#define AFINA_SERVERIMPL_H

#include <afina/network/Server.h>
#include "Worker.h"

namespace Afina {
namespace Network {
namespace Coroutines {

class Worker;

class ServerImpl : public Server {
public:
    explicit ServerImpl(std::shared_ptr<Afina::Storage> ps);
    ~ServerImpl() override;

    // See Server.h
    void Start(uint32_t, uint16_t) override;
    void StartThreadPool(size_t, size_t, size_t, size_t) {}
    //bool StartFIFO(const std::string& r_fifo, const std::string& w_fifo, bool force) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

    bool StartFIFO(const std::string&, const std::string&, bool force = true) {};

private:
    // Thread that is accepting new connections
    std::vector<std::shared_ptr<Worker>> workers;
};

}
}
}

#endif //AFINA_SERVERIMPL_H
