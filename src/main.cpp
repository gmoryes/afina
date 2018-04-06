#include <chrono>
#include <iostream>
#include <memory>
#include <uv.h>

#include <cxxopts.hpp>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/network/Server.h>

#include "network/nonblocking/ServerImpl.h"
//#include "network/uv/ServerImpl.h"
#include "storage/MapBasedGlobalLockImpl.h"

#include <logger/Logger.h>

typedef struct {
    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
} Application;

// Handle all signals catched
void signal_handler(uv_signal_t *handle, int signum) {
    Application *pApp = static_cast<Application *>(handle->data);
    std::cout << "Receive stop signal, wait threads" << std::endl;
    uv_stop(handle->loop);
}

// Called when it is time to collect passive metrics from services
void timer_handler(uv_timer_t *handle) {
    Application *pApp = static_cast<Application *>(handle->data);
}

int main(int argc, char **argv) {
    // Build version
    // TODO: move into Version.h as a function
    std::stringstream app_string;
    app_string << "Afina " << Afina::Version_Major << "." << Afina::Version_Minor << "." << Afina::Version_Patch;
    if (Afina::Version_SHA.size() > 0) {
        app_string << "-" << Afina::Version_SHA;
    }

    // Command line arguments parsing
    cxxopts::Options options("afina", "Simple memory caching server");
    try {
        // TODO: use custom cxxopts::value to print options possible values in help message
        // and simplify validation below
        options.add_options()("s,storage", "Type of storage service to use", cxxopts::value<std::string>());
        options.add_options()("n,network", "Type of network service to use", cxxopts::value<std::string>());
        options.add_options()("min_workers", "Min workers number (default = 1) ", cxxopts::value<int>());
        options.add_options()("max_workers", "Max workers number (default = 1) ", cxxopts::value<int>());
        options.add_options()("max_queue_size", "Max threadpool queue size (default = 1) ", cxxopts::value<int>());
        options.add_options()("idle_time", "Worker idle time(ms) (default = 100) ", cxxopts::value<int>());
        options.add_options()("w,workers", "Workers number (default = 1) ", cxxopts::value<int>());
        options.add_options()("r-fifo", "Fifo file for read (not set by default)", cxxopts::value<std::string>());
        options.add_options()("w-fifo", "File for answers from fifo (set to /dev/null by default)", cxxopts::value<std::string>());
        options.add_options()("h,help", "Print usage info");
        options.parse(argc, argv);

        if (options.count("help") > 0) {
            std::cerr << options.help() << std::endl;
            return 0;
        }
    } catch (cxxopts::OptionParseException &ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    // Start boot sequence
    Application app;
    std::cout << "Starting " << app_string.str() << std::endl;

    // Build new storage instance
    std::string storage_type = "map_global";
    if (options.count("storage") > 0) {
        storage_type = options["storage"].as<std::string>();
    }

    if (storage_type == "map_global") {
        app.storage = std::make_shared<Afina::Backend::MapBasedGlobalLockImpl>();
    } else {
        throw std::runtime_error("Unknown storage type");
    }

    // Build  & start network layer
    std::string network_type = "uv";
    if (options.count("network") > 0) {
        network_type = options["network"].as<std::string>();
    }

    if (network_type == "uv") {
        //app.server = std::make_shared<Afina::Network::UV::ServerImpl>(app.storage);
    } else if (network_type == "blocking") {
        //app.server = std::make_shared<Afina::Network::Blocking::ServerImpl>(app.storage);
    } else if (network_type == "nonblocking") {
        app.server = std::make_shared<Afina::Network::NonBlocking::ServerImpl>(app.storage);
    } else {
        throw std::runtime_error("Unknown network type");
    }

    // Init local loop. It will react to signals and performs some metrics collections. Each
    // subsystem is able to push metrics actively, but some metrics could be collected only
    // by polling, so loop here will does that work
    uv_loop_t loop;
    uv_loop_init(&loop);

    uv_signal_t sig;
    uv_signal_init(&loop, &sig);
    uv_signal_start(&sig, signal_handler, SIGINT);
    sig.data = &app;

    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    timer.data = &app;
    uv_timer_start(&timer, timer_handler, 0, 5000);

    // Start services
    int min_w = 1;
    if (options.count("min_workers") > 0) {
        min_w = options["min_workers"].as<int>();
    }
    int max_w = 1;
    if (options.count("max_workers") > 0) {
        max_w = options["max_workers"].as<int>();
    }
    int max_queue_size = 1;
    if (options.count("max_queue_size") > 0) {
        max_queue_size = options["max_queue_size"].as<int>();
    }
    int idle_time = 100;
    if (options.count("idle_time") > 0) {
        idle_time = options["idle_time"].as<int>();
    }

    int workers_cnt = 1;
    if (options.count("workers") > 0) {
        workers_cnt = options["workers"].as<int>();
    }

    std::string r_fifo;
    if (options.count("r-fifo") > 0) {
        r_fifo = options["r-fifo"].as<std::string>();
    }

    std::string w_fifo = "/dev/null";
    if (options.count("w-fifo") > 0) {
        if (!r_fifo.size()) {
            std::cerr << "--r-fifo must be initialized, see -h for help" << std::endl;
            return 1;
        }
        w_fifo = options["w-fifo"].as<std::string>();
    }

    try {
        if (r_fifo.size()) {
            if (!app.server->StartFIFO(r_fifo, w_fifo)) {
                std::cerr << "Can not create fifo file" << std::endl;
                return 1;
            }
        }

        app.storage->Start();

        app.server->Start(8080, workers_cnt);
        if (network_type == "blocking") {
            app.server->StartThreadPool(min_w, max_w, max_queue_size, idle_time);
        }
        // Freeze current thread and process events
        std::cout << "Application started" << std::endl;
        uv_run(&loop, UV_RUN_DEFAULT);

        // Stop services
        app.server->Stop();
        app.server->Join();
        app.storage->Stop();

        std::cout << "Application stopped" << std::endl;
    } catch (std::exception &e) {
        std::cerr << "Fatal error" << e.what() << std::endl;
    }

    return 0;
}
