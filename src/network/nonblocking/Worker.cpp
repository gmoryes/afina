#include "Worker.h"

#define EPOLL_SIZE 10000

namespace Afina {
namespace Network {
namespace NonBlocking {

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps,
               const std::pair<int, int>& fifo):
    stop(false),
    storage(std::move(ps)),
    fifo(fifo) {}

// See Worker.h
Worker::~Worker() {
    stop = false;
}

bool reader(Worker& worker,
            std::shared_ptr<Protocol::Parser>& parser,
            int fd,
            SmartString& buffer) {

    Logger& logger = Logger::Instance();

    size_t cur_parsed;
    bool find_command;
    try {
        find_command = parser->Parse(buffer, buffer.size(), cur_parsed);
    } catch (std::runtime_error& error) {
        logger.write("Error during parser.Parse(), desc:", error.what());
        return false;
    }

    buffer.Erase(cur_parsed);

    if (find_command) { // Get command

        uint32_t body_size = parser->GetBodySize();
        body_size = (body_size == 0) ? body_size : body_size + 2; // \r\n

        if (buffer.size() < body_size)
            return true; // Read not enough, try again

        Protocol::Parser::Command command;
        try {
            command = parser->Build(body_size);
        } catch (std::runtime_error& error) {
            logger.write("Error during Build(), desc:", error.what());
            return false;
        }

        buffer.Erase(body_size); // Delete body from read buffer
        parser->Reset(); // Delete data, that we has parsed

        std::string must_be_written;
        command->Execute(*(worker.storage), buffer.Copy(body_size), must_be_written);

        // Передаем event_loop строку для записи
        if (fd == worker.fifo.first) {// В случае fifo пишем в fd для записи
            //worker.event_loop.async_write(worker.fifo.second, must_be_written);
        } else {// В случае обычного клиента, пишем ему
            //worker.event_loop.async_write(fd, must_be_written);
        }
        return true;
    }
}

bool acceptor(Worker& worker, int client_fh) {
    using namespace std::placeholders;

    Logger& logger = Logger::Instance();
    logger.write("Accept new client:", client_fh);

    auto new_parser = std::make_shared<Protocol::Parser>();
    worker.parsers.push_back(new_parser);

    //auto bind_reader = std::bind(reader, worker, new_parser, _1, _2);
    //worker.event_loop.async_read(client_fh, std::move(bind_reader));

    return true;
}

void test(Worker& self, int a, int b) {
    std::cout << a << ' ' << b << std::endl;
}

void Worker::OnRun(int server_socket, int worker_number, int r_fifo = -1) {
    using namespace std::placeholders;

    Logger &logger = Logger::Instance();

    std::stringstream ss;
    ss << "WORKER_" << worker_number;
    logger.i_am(ss.str());

    logger.write("Hello");

    event_loop.Start(EPOLL_SIZE);

    SharedParsers parsers;

    if (r_fifo != -1) { // Add fifo listener
        auto new_parser = std::make_shared<Protocol::Parser>();
        parsers.push_back(new_parser);

        //auto bind_reader = std::bind(reader, *this, new_parser, _1, _2);
        //event_loop.async_read(r_fifo, std::move(bind_reader));
    }

    //auto bind_acceptor = std::bind(acceptor, *this, _1);
    auto f = std::bind(test, *this, 100, _1);
    //event_loop.async_accept(server_socket, std::move(bind_acceptor));
    event_loop.loop();
}

// See Worker.h
void Worker::Start(int server_socket, int worker_number) {
    Logger& logger = Logger::Instance();

    try {
        std::thread thread(&Worker::OnRun, this, server_socket, fifo.first, worker_number);
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
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    stop = true;
}

// See Worker.h
void Worker::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // TODO: Nothing here lol
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
