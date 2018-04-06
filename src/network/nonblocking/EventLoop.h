#ifndef AFINA_EVENTLOOP_H
#define AFINA_EVENTLOOP_H

#include <vector>
#include <functional>

#include "Utils.h"

namespace Afina {
namespace Network {
namespace NonBlocking {

class Event {
public:
private:
};

class EventLoop {
public:
    EventLoop() = default;

    void async_accept(int server_socket,
                      std::function<bool()> func,
                      EventLoop *event_loop,
                      int client_fh);

    void async_read(int read_fh, std::function<bool()> func);

private:
    std::vector<Event> _events;
};

}
}
}

#endif //AFINA_EVENTLOOP_H
