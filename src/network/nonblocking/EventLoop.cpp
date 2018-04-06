#include "EventLoop.h"


namespace Afina {
namespace Network {
namespace NonBlocking {

void EventLoop::async_accept(int server_socket, std::function<bool()> func, EventLoop *event_loop, int client_fh) {

}

}
}
}