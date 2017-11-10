#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <pthread.h> // pthread_sigmask
#include <signal.h>  // sigset_t
#include <stdlib.h>  // realpath
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/stat.h> // umask
#include <sys/timerfd.h>
#include <sys/types.h> // getpid
#include <unistd.h>    // getopt, unlink

#include <cxxopts.hpp>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/network/Server.h>

#include "network/blocking/ServerImpl.h"
#include "network/epoll/ServerImpl.h"
#include "network/nonblocking/ServerImpl.h"

#include "storage/MapBasedGlobalLockImpl.h"
#include "storage/MapBasedRWLockImpl.h"
#include "storage/MapBasedStripedLockImpl.h"

#include <sched.h>

static unsigned int get_cpu_count() {
    cpu_set_t myprocessors;
    if (sched_getaffinity(0, sizeof(myprocessors), &myprocessors)) { // afinity, lol
        throw std::runtime_error("sched_getaffinity failed");
    }
    int ret = CPU_COUNT(&myprocessors);
    if (ret <= 0) { // why should they have made it signed?!
        throw std::runtime_error("CPU_COUNT returned WTF");
    }
    return ret;
}

typedef struct {
    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
} Application;

struct epoll_event_handler {
    int fd;
    Application &app;
    epoll_event_handler(int fd_, Application &app_) : fd(fd_), app(app_) {}
    virtual bool advance() = 0; // returns true when it's time to stop the app
    virtual ~epoll_event_handler() {}
};

struct epoll_signal : epoll_event_handler {
    epoll_signal(int fd, Application &app) : epoll_event_handler(fd, app) {}
    bool advance() override {
        struct signalfd_siginfo buf;
        ssize_t len;
        while ((len = read(fd, &buf, sizeof(buf))) != -1) {
            // The read(1) returns information for as many signals as are pending and will fit in
            // the supplied buffer.
            if (len != sizeof(buf))
                throw std::runtime_error("Short read from signalfd");
            if (buf.ssi_signo != SIGHUP) // ignore SIGHUP for now; maybe there'll be some kind of semantics here later
                return true;
        }
        if (errno != EAGAIN)
            throw std::runtime_error("Read from signalfd failed");
        // falling through means we've only caught non-terminating signals
        return false;
    }
};

struct epoll_timer : epoll_event_handler {
    epoll_timer(int fd, Application &app) : epoll_event_handler(fd, app) {}
    bool advance() override {
        uint64_t dummy;
        ssize_t len;
        // buffer given to read(2) returns an unsigned 8-byte integer (uint64_t) containing the number of
        // expirations that  have  occurred
        while ((len = read(fd, &dummy, sizeof(dummy))) != -1) {
            if (len != sizeof(dummy))
                throw std::runtime_error("Short read from timerfd");
        }
        if (errno != EAGAIN)
            throw std::runtime_error("Read from signalfd failed");
        std::cout << "Start passive metrics collection" << std::endl;
        return false;
    }
};

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
        options.add_options()("r,readfifo", "Path to a FIFO to read commands from", cxxopts::value<std::string>());
        options.add_options()("w,writefifo", "Path to a FIFO to write commands to", cxxopts::value<std::string>());
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
    } else if (storage_type == "map_rwlock") {
        app.storage = std::make_shared<Afina::Backend::MapBasedRWLockImpl>();
    } else if (storage_type == "map_striped") {
        app.storage = std::make_shared<Afina::Backend::MapBasedStripedLockImpl>(get_cpu_count());
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
    } else if (network_type == "nonblocking") {
        app.server = std::make_shared<Afina::Network::NonBlocking::ServerImpl>(app.storage);
    } else {
        throw std::runtime_error("Unknown network type");
    }

    // At this point we'd said our welcome and checked most parameters
    // but still haven't run any important code. Time to daemonize and/or create
    // pidfiles
    {
        std::ofstream pfs;
        if (options.count("pidfile")) {
            pidfile = options["pidfile"].as<std::string>();
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

        // add the signalfd to the epoll
        // (we'll reuse our struct sigevent inside the for loop)
        epoll_signal sig_object{sigfd, app};
        struct epoll_event event = {EPOLLIN | EPOLLET, {(void *)&sig_object}};
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sigfd, &event))
            throw std::runtime_error("failed to add signalfd to epoll set");

        // employ a timerfd to run my periodic tasks
        int periodic_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        struct itimerspec periodic_interval = {{10, 0}, {10, 0}};
        if (timerfd_settime(periodic_fd, 0, &periodic_interval, nullptr))
            throw std::runtime_error("failed to set periodic timer interval");
        // add the timerfd to the epoll, too
        epoll_timer tmr_object{periodic_fd, app};
        event.data.ptr = (void *)&tmr_object;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, periodic_fd, &event))
            throw std::runtime_error("failed to add signalfd to epoll set");

        if (options.count("readfifo") ^ options.count("writefifo")) {
            throw std::runtime_error("FIFO requires exactly 2 files (or none at all)");
        }
        if (options.count("readfifo")) {
            Afina::Network::Epoll::ServerImpl *s = dynamic_cast<Afina::Network::Epoll::ServerImpl *>(app.server.get());
            if (!s) {
                throw std::runtime_error("FIFO is only supported in epoll backend");
            }
            s->set_fifo(options["readfifo"].as<std::string>(), options["writefifo"].as<std::string>());
        }
        app.storage->Start();
        app.server->Start(8080, get_cpu_count());

        std::cout << "Application started" << std::endl;

        // Freeze current thread and process events
        for (;;) {
            int events = epoll_wait(epollfd, &event, 1, 10000);
            if (events == -1 && errno != EINTR) // timeout or signal
                throw std::runtime_error("epoll_wait failed");
            if (((epoll_event_handler *)event.data.ptr)->advance())
                break;
        }

        // time to clean up
        close(periodic_fd);
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
