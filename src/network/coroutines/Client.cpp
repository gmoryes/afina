#include "Client.h"

namespace Afina {
namespace Network {
namespace Coroutines {

void Client::Read() {
    Logger &logger = Logger::Instance();

    ssize_t has_read = 0;
    char buffer[BUFFER_SIZE];

    while ((has_read = read(_fd, buffer, BUFFER_SIZE)) > 0) {
        logger.write("Has read", has_read, "bytes from socket:", _fd);

        read_buffer.Put(buffer, has_read);
    }

    if (has_read == -1) {
        if (errno == EAGAIN) {
            logger.write("No data anymore, wait...");
        } else {
            logger.write("Error during read(), errno =", errno);
            _connected = false;
        }
    } else if (!has_read) {
        logger.write("Client diconnected, goodbye");
        _connected = false;
    }
}

void Client::Write() {
    if (write_buffer.empty())
        return;

    Logger &logger = Logger::Instance();

    ssize_t has_write;
    size_t need_write = std::min(size_t(BUFFER_SIZE), write_buffer.size());
    while ((has_write = write(_fd, write_buffer.data(), need_write)) > 0) {
        logger.write("Has write", has_write, "bytes to", _fd);

        write_buffer.Erase(has_write);
        need_write = std::min(size_t(BUFFER_SIZE), write_buffer.size());

        if (need_write == 0)
            break;
    }

    if (has_write < 0) {
        if (errno == EAGAIN) {
            logger.write("Socket(", _fd, " is overloaded now, wait...");
        } else {
            logger.write("Error during write(), errno =", errno);
            _connected = false;
        }
    } else if (!has_write) {
        logger.write("Client diconnected, goodbye");
        _connected = false;
    }
}

bool Client::Process() {
    Logger &logger = Logger::Instance();
    size_t cur_parsed;
    bool find_command;
    while (not read_buffer.empty()) {
        try {
            find_command = parser.Parse(read_buffer, read_buffer.size(), cur_parsed);
        } catch (std::runtime_error &error) {
            logger.write("Error during parser.Parse(), desc:", error.what());
            return false;
        }

        read_buffer.Erase(cur_parsed);

        if (find_command) { // Get command

            uint32_t body_size = parser.GetBodySize();

            if (read_buffer.size() < body_size)
                return true; // Read not enough, try again

            Protocol::Parser::Command command;
            try {
                command = parser.Build();
            } catch (std::runtime_error &error) {
                logger.write("Error during Build(), desc:", error.what());
                return false;
            }

            std::string must_be_written;
            command->Execute(*storage, read_buffer.Copy(body_size), must_be_written);
            must_be_written += "\r\n";

            body_size = (body_size == 0) ? body_size : body_size + 2; // \r\n
            read_buffer.Erase(body_size); // Delete body from read buffer
            parser.Reset(); // Delete data, that we has parsed

            write_buffer.Put(must_be_written.data(), must_be_written.size());

        } else {
            return true;
        }
    }

    return true;
}

}
}
}
