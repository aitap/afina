#define _POSIX_C_SOURCE 200809L
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdlib.h>    // realpath
#include <sys/types.h> // getpid
#include <unistd.h>    // getopt, unlink
#include <uv.h>

#include <afina/Storage.h>
#include <afina/Version.h>

#include "network/Server.h"
#include "storage/MapBasedGlobalLockImpl.h"

typedef struct {
    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
    std::string pidfile;
} Application;

// Handle all signals caught
void signal_handler(uv_signal_t *handle, int signum) {
    std::cout << "Received stop signal" << std::endl;
    Application *pApp = static_cast<Application *>(handle->data);
    // don't leave pidfile: some other process may get our PID later
    if (pApp->pidfile.size())
        unlink(pApp->pidfile.c_str());
    uv_stop(handle->loop);
}

// Called when it is time to collect passive metrics from services
void timer_handler(uv_timer_t *handle) {
    Application *pApp = static_cast<Application *>(handle->data);
    std::cout << "Start passive metrics collection" << std::endl;
}

int main(int argc, char **argv) {
    std::cout << "Starting Afina " << Afina::Version_Major << "." << Afina::Version_Minor << "."
              << Afina::Version_Patch;
    if (Afina::Version_SHA.size() > 0) {
        std::cout << "-" << Afina::Version_SHA;
    }
    std::cout << std::endl;

    // Build new storage instance
    Application app;

    // At this point we'd said our welcome but still haven't run any important code
    // Time to daemonize and/or create pidfiles
    {
        bool daemonize = false;
        std::ofstream pfs;
        {
            int c;
            std::unique_ptr<char, decltype(&std::free)> rpath{nullptr, &std::free};
            while ((c = getopt(argc, argv, ":p:d")) != -1) {
                switch (c) {
                case 'd':
                    daemonize = true;
                    break;
                case 'p':
                    // create if needed
                    pfs.open(optarg);
                    // store the real path because we may chdir later
                    rpath.reset(realpath(optarg, nullptr));
                    if (!rpath) {
                        perror("Creating pidfile:");
                        return 1;
                    }
                    app.pidfile.assign(rpath.get());
                    break;
                case ':':
                    std::cerr << "Option -" << (char)optopt << " requires an argument" << std::endl;
                    return 1;
                case '?':
                    std::cerr << "Option -" << (char)optopt << " unrecognized" << std::endl;
                    return 1;
                }
            }
        }
        if (daemonize) {
            // 1. fork() off to
            //  a. return the control to the shell
            //  b. *not* to remain a process group leader
            pid_t pid = fork();
            if (pid < 0) {
                std::cerr << "Failed to fork()" << std::endl;
                return 1;
            } else if (pid > 0) // parent should exit
                return 0;

            // 2. setsid() to become a process+session group leader
            //  (we won't have a controlling terminal this way)
            if (setsid() == -1)
                return 1; // Probably shouldn't print anything at this point

            // 3. fork() off again, leaving the session group without a leader
            //  (this makes sure we stay without a controlling terminal forever)
            if ((pid = fork()) < 0)
                return 1;
            else if (pid > 0)
                return 0;

            // 4. Don't occupy directory handles if we can help it
            chdir("/");
            // 5. Whatever umask we inherited, drop it
            umask(0);

            // 6. Daemons don't cry
            // (cin & cout are tied to stdin & stdout)
            freopen("/dev/null", "r", stdin);
            freopen("/dev/null", "w", stdout);
        }
        if (pfs) { // we were asked to create a pidfile - now is time
            pfs << getpid() << std::endl;
        }
    }

    app.storage = std::make_shared<Afina::Backend::MapBasedGlobalLockImpl>();

    // Build  & start network layer
    app.server = std::make_shared<Afina::Network::Server>(app.storage);

    // Init local loop. It will react to signals and performs some metrics collections. Each
    // subsystem is able to push metrics actively, but some metrics could be collected only
    // by polling, so loop here will does that work
    uv_loop_t loop;
    uv_loop_init(&loop);

    uv_signal_t sig_term, sig_int;
    uv_signal_init(&loop, &sig_term);
    uv_signal_start(&sig_term, signal_handler, SIGTERM);
    uv_signal_init(&loop, &sig_int);
    uv_signal_start(&sig_int, signal_handler, SIGINT);
    sig_term.data = &app;
    sig_int.data = &app;

    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    timer.data = &app;
    uv_timer_start(&timer, timer_handler, 0, 5000);

    // Start services
    try {
        app.storage->Start();
        app.server->Start(8080);

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
