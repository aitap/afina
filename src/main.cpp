#define _POSIX_C_SOURCE 200809L
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <pthread.h> // pthread_sigmask
#include <signal.h>  // sigset_t
#include <stdlib.h>  // realpath
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>  // umask
#include <sys/types.h> // getpid
#include <unistd.h>    // getopt, unlink

#include <cxxopts.hpp>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/network/Server.h>

#include "network/blocking/ServerImpl.h"
#include "network/epoll/ServerImpl.h"
#include "storage/MapBasedGlobalLockImpl.h"

typedef struct {
    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
} Application;

int signal_handler(Application &app, int fd, sigset_t &signals) {
    struct signalfd_siginfo buf;
    ssize_t len;
    while ((len = read(fd, &buf, sizeof(buf))) != -1) {
        // The read(1) returns information for as many signals as are pending and will fit in
        // the supplied buffer.
        if (len != sizeof(buf))
            throw std::runtime_error("Short read from signalfd");
        if (sigismember(&signals, buf.ssi_signo) == 1) {
            if (buf.ssi_signo != SIGHUP) // for now
                return 1;
        } else
            throw std::runtime_error("Caught a wrong signal" + std::to_string(buf.ssi_signo));
    }
    if (errno != EAGAIN)
        throw std::runtime_error("Read from signalfd failed");
    // somehow, all the signals we've caught were SIGHUP
    return 0;
}

void run_periodic(Application &app) { std::cout << "Start passive metrics collection" << std::endl; }

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
        options.add_options()("d,daemonize", "Daemonize to background"); // boolean by default
        options.add_options()("p,pidfile", "Path of pidfile", cxxopts::value<std::string>());
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
    std::string pidfile;

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
    std::string network_type = "epoll";
    if (options.count("network") > 0) {
        network_type = options["network"].as<std::string>();
    }

    if (network_type == "blocking") {
        app.server = std::make_shared<Afina::Network::Blocking::ServerImpl>(app.storage);
    } else if (network_type == "epoll") {
        app.server = std::make_shared<Afina::Network::Epoll::ServerImpl>(app.storage);
    } else {
        throw std::runtime_error("Unknown network type");
    }

    // At this point we'd said our welcome and checked most parameters
    // but still haven't run any important code. Time to daemonize and/or create
    // pidfiles
    {
        std::ofstream pfs;
        if (options.count("pidfile")) {
            std::string pidfile = options["pidfile"].as<std::string>();
            // real path storage
            std::unique_ptr<char, decltype(&std::free)> rpath{nullptr, &std::free};
            // create the pidfile and prepare to write
            pfs.open(pidfile.c_str());
            // store the *real* path because we may chdir later
            rpath.reset(realpath(pidfile.c_str(), nullptr));
            if (!rpath)
                throw std::runtime_error("Failed to get real path of PID file");

            pidfile.assign(rpath.get());
        }
        if (options.count("daemonize")) {
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
            // (cin/cout/cerr are tied to stdin/stdout/stderr)
            freopen("/dev/null", "r", stdin);
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
        }
        if (pfs) { // we were asked to create a pidfile - now is time to fill it
            pfs << getpid() << std::endl;
        }
    }

    // Start services
    try {
        app.storage->Start();
        app.server->Start(8080, 10);

        // Freeze current thread and process events
        std::cout << "Application started" << std::endl;

        // prepare to handle signals and run periodic tasks
        // create sigfd to watch it via epoll
        sigset_t signals;
        // the following operations shouldn't fail because I'm using constants from the same headers
        sigemptyset(&signals);
        sigaddset(&signals, SIGTERM);
        sigaddset(&signals, SIGINT);
        sigaddset(&signals, SIGHUP);
        // we should block signals we want to read from signalfd
        if (pthread_sigmask(SIG_BLOCK, &signals, nullptr)) // child threads should inherit parent signal masks
            throw std::runtime_error("failed to block TERM & INT");
        int sigfd = signalfd(-1, &signals, SFD_NONBLOCK);
        if (sigfd == -1)
            throw std::runtime_error("signalfd failed");
        // create a epoll instance and use it to watch signalfd above
        int epollfd = epoll_create1(0);
        if (epollfd == -1)
            throw std::runtime_error("epoll_create1 failed");
        // two-stange initialization of the object because C++ doesn't allow filling second field of a union
        struct epoll_event sigevent = {EPOLLIN | EPOLLET, {nullptr}};
        sigevent.data.fd = sigfd;
        // we'll reuse the struct sigevent inside the for loop
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sigfd, &sigevent))
            throw std::runtime_error("failed to add signalfd to epoll set");

        for (;;) {
            int events = epoll_wait(epollfd, &sigevent, 1, 10000);
            if (events == -1 && errno != EINTR) // timeout or signal
                throw std::runtime_error("epoll_wait failed");
            if (events && signal_handler(app, sigevent.data.fd, signals))
                break;
            run_periodic(app);
        }
        close(sigfd);
        close(epollfd);

        // Stop services
        app.server->Stop();
        app.server->Join();
        app.storage->Stop();

        std::cout << "Application stopped" << std::endl;
    } catch (std::exception &e) {
        std::cerr << "Fatal error" << e.what() << std::endl;
    }

    if (pidfile.size())
        unlink(pidfile.c_str());

    return 0;
}
