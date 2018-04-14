#include "Worker.h"

#define EPOLL_SIZE 10000

namespace Afina {
namespace Network {
namespace NonBlocking {

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps,
               const std::pair<int, int>& fifo):
    storage(std::move(ps)),
    fifo(fifo) {
}

bool reader(const std::shared_ptr<Worker>& worker,
            const std::shared_ptr<Protocol::Parser>& parser,
            int fd,
            SmartString& buffer) {

    Logger& logger = Logger::Instance();
    size_t cur_parsed;
    bool find_command;
    while (not buffer.empty()) {
        try {
            find_command = parser->Parse(buffer, buffer.size(), cur_parsed);
        } catch (std::runtime_error &error) {
            logger.write("Error during parser.Parse(), desc:", error.what());
            return false;
        }

        buffer.Erase(cur_parsed);

        if (find_command) { // Get command

            uint32_t body_size = parser->GetBodySize();

            if (buffer.size() < body_size)
                return true; // Read not enough, try again

            Protocol::Parser::Command command;
            try {
                command = parser->Build();
            } catch (std::runtime_error &error) {
                logger.write("Error during Build(), desc:", error.what());
                return false;
            }

            std::string must_be_written;
            command->Execute(*(worker->storage), buffer.Copy(body_size), must_be_written);
            must_be_written += "\r\n";

            body_size = (body_size == 0) ? body_size : body_size + 2; // \r\n
            buffer.Erase(body_size); // Delete body from read buffer
            parser->Reset(); // Delete data, that we has parsed

            // Передаем в event_loop строку для записи
            if (fd == worker->fifo.first) {
                if (worker->fifo.second != -1)
                    worker->event_loop.async_write(worker->fifo.second, must_be_written); // Fifo case
            } else {
                worker->event_loop.async_write(fd, must_be_written); // Client case
            }

        } else {
            return true;
        }
    }

    return true;

}

bool acceptor(const std::shared_ptr<Worker>& worker, int client_fh) {
    using namespace std::placeholders;

    Logger& logger = Logger::Instance();
    logger.write("Accept new client:", client_fh);

    auto new_parser = std::make_shared<Protocol::Parser>(Protocol::Parser());

    auto bind_reader = std::bind(reader, worker, new_parser, _1, _2);
    worker->event_loop.async_read(client_fh, std::move(bind_reader));

    return true;
}

void Worker::OnRun(int server_socket, int worker_number, int r_fifo = -1) {
    using namespace std::placeholders;

    Logger &logger = Logger::Instance();

    std::stringstream ss;
    ss << "WORKER_" << worker_number;
    logger.i_am(ss.str());

    logger.write("Hello");

    event_loop.Start(EPOLL_SIZE);

    if (worker_number == 0 && r_fifo != -1) { // Add fifo listener
        auto new_parser = std::make_shared<Protocol::Parser>();

        auto bind_reader = std::bind(reader, shared_from_this(), new_parser, _1, _2);
        event_loop.async_read(r_fifo, std::move(bind_reader), EPOLLIN | EPOLLEXCLUSIVE | EPOLLHUP);
    }

    auto bind_acceptor = std::bind(acceptor, shared_from_this(), _1);
    event_loop.async_accept(server_socket, std::move(bind_acceptor));
    event_loop.loop();
}

// See Worker.h
void Worker::Start(int server_socket, int worker_number) {
    Logger& logger = Logger::Instance();

    try {
        std::thread thread(&Worker::OnRun, this, server_socket, worker_number, fifo.first);
        thread.detach();
    } catch (std::runtime_error& error) {
        logger.write(
            "Error while creating", worker_number, "worker",
            "Error:", error.what()
        );
    }
}

// See Worker.h
void Worker::Stop() {
    event_loop.Stop();
}

// See Worker.h
void Worker::Join() {
    event_loop.Join();
}

Worker::~Worker() {
    Worker::Join();
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
