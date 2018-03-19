#include "Executor.h"
#include <logger/Logger.h>
#include <thread>
#include <sstream>

namespace Afina {

Executor::Executor(size_t low_watermark = 1,
                   size_t hight_watermark = 1,
                   size_t max_queue_size = 1,
                   size_t idle_time = 100):

    low_watermark(low_watermark),
    hight_watermark(hight_watermark),
    max_queue_size(max_queue_size),
    idle_time(idle_time) {

    Logger& logger = Logger::Instance();
    logger.write("Start create ThreadPool");
    logger.write("ThredPool params");
    logger.write("number of thread: [", low_watermark, hight_watermark, "]");
    logger.write("max_queue size: ", max_queue_size);
    logger.write("idle_time: ", idle_time);

    buzy_workers.store(0);

    for (int i = 0; i < low_watermark; i++) {
        try {
            threads.emplace_back(&perform, this, i);
        } catch (std::runtime_error &ex) {
            logger.write("Error while thread creating: ", ex.what());
        }
    }

    logger.write("Threads for ThreadPool created!");
}

Executor::~Executor() {
    Stop(true);
}

void Executor::Stop(bool await) {
    Logger& logger = Logger::Instance();
    state = State::kStopping;
    if (await) {
        for (int i = 0; i < threads.size(); i++) {
            logger.write("Wait join of", threads[i].get_id());
            threads[i].join();
        }
        state = State::kStopped;
    }
}

bool Executor::can_add() {
    if (buzy_workers.load() < hight_watermark)
        return true;

    return tasks.size() < max_queue_size;

}

};
