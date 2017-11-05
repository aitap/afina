#include "ServerImpl.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <pthread.h>
#include <signal.h>

#include <errno.h>
#include <sys/epoll.h>

#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>

#include <list>

namespace Afina {
namespace Network {
namespace Epoll {

template <void (ServerImpl::*method)()> static void *PthreadProxy(void *p) {
    ServerImpl *srv = reinterpret_cast<ServerImpl *>(p);
    try {
        (srv->*method)();
        return (void *)0;
    } catch (std::runtime_error &ex) {
        std::cerr << "Exception caught: " << ex.what() << std::endl;
        return (void *)-1;
    }
    // different return values may be used to motify whoever calls pthread_join
    // of error conditions
}

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps) : Server(ps), fifo_read_fd(-1), fifo_write_fd(-1) {}

// See Server.h
ServerImpl::~ServerImpl() {
    for (const std::string &p : {fifo_read_path, fifo_write_path}) {
        if (p.size()) {
            unlink(p.c_str());
        }
    }
}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0)
        throw std::runtime_error("Unable to mask signals");

    // Setup server parameters BEFORE thread is created, that will guarantee
    // variable value visibility
    if ((uint16_t)port == port)
        listen_port = (uint16_t)port;
    else
        throw std::overflow_error("port wouldn't fit in a 16-bit value");

    workers.resize(n_workers);
    running.store(true);
    for (uint16_t i = 0; i < n_workers; i++)
        if (pthread_create(&workers[i], NULL, &PthreadProxy<&ServerImpl::RunEpoll>, this) < 0)
            throw std::runtime_error("Could not create epoll thread");
}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    running.store(false);
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    void *retval;
    for (pthread_t &epoll_thread : workers) {
        if (pthread_join(epoll_thread, &retval))
            throw std::runtime_error("pthread_join failed");
        if (retval) // better late than never
            throw std::runtime_error("epoll thread had encountered an error");
    }
    workers.clear();
}

static int setsocknonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, NULL);
    if (flags == -1)
        return flags;
    flags |= O_NONBLOCK;
    return fcntl(sock, F_SETFL, flags);
}

struct ep_fd {
    int fd;
    int epoll_fd;
    std::shared_ptr<Afina::Storage> ps;
    ep_fd(int fd_, int epoll_fd_, std::shared_ptr<Afina::Storage> ps_) : fd(fd_), epoll_fd(epoll_fd_), ps(ps_) {}
    virtual void advance(uint32_t) = 0;
    virtual ~ep_fd() {}
};

static int epoll_modify(int epoll_fd, int how, uint32_t events, ep_fd &target, int target_fd) {
    struct epoll_event new_ev {
        events, { (void *)&target }
    };
    return epoll_ctl(epoll_fd, how, target_fd, &new_ev);
}

static int epoll_modify(int epoll_fd, int how, uint32_t events, ep_fd &target) {
    return epoll_modify(epoll_fd, how, events, target, target.fd);
}

struct client_fd : ep_fd {
    // test with small buffer first
    const size_t buffer_size = 16;

    std::vector<char> buf;
    size_t offset;
    std::string out;
    bool bailout;
    Protocol::Parser parser;
    client_fd(int fd_, int epoll_fd_, std::shared_ptr<Afina::Storage> ps_)
        : ep_fd(fd_, epoll_fd_, ps_), offset(0), bailout(false) {}

    bool read_in(int read_fd) {
        // try to exhaust whatever the client has sent us
        ssize_t len = 0;
        do {
            offset += len;
            if (buf.size() == offset)
                buf.resize(std::max(buf.size() * 2, buffer_size));
            len = read(read_fd, buf.data() + offset, buf.size() - offset);
        } while (len > 0);
        if (!len)
            bailout = true;
        return (len >= 0 || errno == EWOULDBLOCK || errno == EAGAIN);
    }

    bool read_in() { return read_in(fd); }

    void parse_run() {
        try {
            for (;;) { // loop until we can't create more commands
                uint32_t body_size;
                // check if there's a pending command in the parser
                auto cmd = parser.Build(body_size);
                if (!cmd) { // maybe we can parse more and get it?
                    size_t parsed = 0;
                    parser.Parse(buf.data(), offset, parsed);
                    buf.erase(buf.begin(), buf.begin() + parsed);
                    offset -= parsed;
                    cmd = parser.Build(body_size);
                }
                if (cmd && (!body_size || offset >= body_size + 2)) {
                    // got a command and a body if required, will execute now
                    std::string body;
                    parser.Reset();
                    if (body_size) {
                        // supply the body
                        body.assign(buf.data(), body_size);
                        buf.erase(buf.begin(), buf.begin() + body_size + 2);
                        offset -= (body_size + 2);
                    }
                    std::string local_out;
                    cmd->Execute(*ps, body, local_out);
                    out += local_out;
                    out += std::string("\r\n");
                } else {
                    // we did our best, but we can't execute a command yet
                    // no chance of other commands until more data comes
                    break;
                }
            }
        } catch (std::runtime_error &e) {
            // if anything fails we just report the error to the user
            out += std::string("CLIENT_ERROR ") + e.what() + std::string("\r\n");
            bailout = true;
        }
    }

    bool write_out(int write_fd) {
        ssize_t len = 0;
        do {
            len = write(write_fd, out.data(), out.size());
            if (len > 0)
                out.erase(0, len);
        } while (out.size() && len > 0);
        return (!out.size() || errno == EWOULDBLOCK || errno == EAGAIN);
    }

    bool write_out() { return write_out(fd); }
};

struct client_socket : client_fd {
    std::list<client_socket> &list;
    std::list<client_socket>::iterator self;
    client_socket(int fd_, int epoll_fd_, std::shared_ptr<Afina::Storage> ps_, std::list<client_socket> &list_,
                  std::list<client_socket>::iterator self_)
        : client_fd(fd_, epoll_fd_, ps_), list(list_), self(self_) {
        buf.resize(buffer_size);
    }
    void cleanup() {
        epoll_modify(epoll_fd, EPOLL_CTL_DEL, 0, *this);
        shutdown(fd, SHUT_RDWR);
        close(fd);
        // time to commit sudoku
        list.erase(self);
        // ISO C++ faq does allow even `delete this`, subject to it being done carefully
    }
    void advance(uint32_t events) override {
        if (events & (EPOLLHUP | EPOLLERR))
            return cleanup();

        if (!read_in()) {
            return cleanup();
        }

        // try to parse new commands
        // XXX: why did I forbid parsing while having pending output?
        parse_run();

        // we have created pending output just now or have it from previous iteration
        if (out.size()) {
            if (!write_out()) {
                return cleanup();
            }
        }

        if (bailout) {
            return cleanup();
        }
    }
};

static int open_fifo(const std::string &path, int flags) {
    // tedious checking that everything is okay
    if (mkfifo(path.c_str(), 0660) && errno != EEXIST) {
        throw std::runtime_error("FIFO doesn't exist and I couldn't create one");
    }
    int fd = open(path.c_str(), O_NONBLOCK | flags);
    if (fd < 0) {
        throw std::runtime_error("Couldn't open FIFO fd");
    }

    // but is it a FIFO?
    struct stat fifo_stat;
    if (fstat(fd, &fifo_stat)) {
        close(fd);
        throw std::runtime_error("Couldn't perform stat() on an opened FIFO! WTF?!");
    }

    if (!S_ISFIFO(fifo_stat.st_mode)) {
        close(fd);
        throw std::runtime_error("File is not a FIFO");
    }

    return fd;
}

struct client_fifo : client_fd {
    std::string fifo_path;
    int read_fd, write_fd;
    std::string read_path, write_path;
    client_fifo(int epoll_, std::shared_ptr<Afina::Storage> ps_)
        : client_fd(-1, epoll_, ps_), read_fd(-1), write_fd(-1) {}

    void die(const char *what) {
        close(read_fd);
        close(write_fd);
        throw std::runtime_error(what);
    }

    void enable(int read_fd_, int write_fd_) {
        read_fd = read_fd_;
        write_fd = write_fd_;
        if (epoll_modify(epoll_fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLET, *this, read_fd)) {
            die("Couldn't watch read FIFO for events");
        }
        if (epoll_modify(epoll_fd, EPOLL_CTL_ADD, EPOLLOUT | EPOLLET, *this, write_fd)) {
            die("Couldn't watch write FIFO for events");
        }
    }

    void cleanup() { // this client is busted, but we can try again
        buf.clear();
        buf.resize(buffer_size);
        offset = 0;
        out.clear();
        bailout = false;
        parser.Reset();
        std::cout << "trying to reopen write FIFO" << std::endl;
        if (close(write_fd)) {
            throw std::runtime_error("Couldn't close FIFO");
        }
        write_fd = -1;
        try {
            write_fd = open_fifo(write_path, O_RDWR);
            if (epoll_modify(epoll_fd, EPOLL_CTL_ADD, EPOLLOUT | EPOLLET, *this, write_fd)) {
                die("Couldn't watch write FIFO for events");
            }
        } catch (...) {
            // don't leak fds
            close(read_fd);
            close(write_fd);
            read_fd = write_fd = -1;
            throw;
        }
    }

    void advance(uint32_t events) override {
        if (events & EPOLLERR) {
            die("Caught error state on a FIFO fd");
        }

        if (events & EPOLLIN) {
            if (!read_in(read_fd)) {
                return cleanup();
            }
        }

        parse_run();

        if (out.size()) {
            if (!write_out(write_fd)) {
                return cleanup();
            }
        }

        if (bailout) { // client closed read fd; we should reopen ours, too
            return cleanup();
        }
    }
};

struct listen_fd : ep_fd {
    std::list<client_socket> &client_list;
    listen_fd(int fd_, int epoll_fd_, std::shared_ptr<Afina::Storage> ps_, std::list<client_socket> &client_list_)
        : ep_fd(fd_, epoll_fd_, ps_), client_list(client_list_) {}
    void advance(uint32_t events) override {
        if (events & (EPOLLHUP | EPOLLERR)) {
            close(fd);
            throw std::runtime_error("Caught error state on listen socket");
        }
        // prepare to accept a connection
        struct sockaddr_in client_addr;
        socklen_t sinSize = sizeof(struct sockaddr_in);
        int client_socket;
        while ((client_socket = accept(fd, (struct sockaddr *)&client_addr, &sinSize)) !=
               -1) { // got a pending connection
            if (setsocknonblocking(client_socket))
                throw std::runtime_error("Couldn't set client socket to non-blocking");
            // create a client object
            auto cl_it = client_list.emplace(client_list.end(), client_socket, epoll_fd, ps, client_list,
                                             client_list.end() /* see below */);
            cl_it->self = cl_it; // sets the self field so it would be able to suicide later
            // register the object in epoll fd
            if (epoll_modify(epoll_fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT | EPOLLET, *cl_it))
                throw std::runtime_error("epollctl failed to add client socket");
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return; // don't crash if we're just waiting for more clients
        // oh well
        close(fd);
        throw std::runtime_error("Socket accept() failed");
    }
};

const size_t num_events = 10;   // events at a time
const int epoll_timeout = 5000; // ms, to check every now and then that we still need to be running

// See Server.h
void ServerImpl::RunEpoll() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // first, create the epoll instance
    int epoll_sock = epoll_create1(0);

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(listen_port); // TCP port number, downcasted from 32-bit to 16-bit type
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    int server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    int reuseaddr = 1, reuseport = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &reuseport, sizeof(reuseport)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    if (setsocknonblocking(server_socket))
        throw std::runtime_error("Couldn't set O_NONBLOCK to server socket");

    // prepare the necessary objects to handle clients
    std::list<client_socket> client_list;
    listen_fd listening_object{server_socket, epoll_sock, pStorage, client_list};
    // add the listen socket to the epoll set
    if (epoll_modify(epoll_sock, EPOLL_CTL_ADD, EPOLLIN | EPOLLET, listening_object))
        throw std::runtime_error("epoll_ctl failed to add the listen socket");

    client_fifo fifo_handler{epoll_sock, pStorage};
    {
        // maybe there's a FIFO waiting for us?
        std::lock_guard<std::mutex> lock{fifo_lock};
        // it's set_fifo's job to make sure that either 2 FIFOs are open or none at all
        if (fifo_read_fd >= 0 && fifo_write_fd >= 0) {
            fifo_handler.enable(fifo_read_fd, fifo_write_fd);
            fifo_handler.read_path = fifo_read_path;
            fifo_handler.write_path = fifo_write_path;
            fifo_write_fd = fifo_read_fd = -1;
        }
    }

    // main loop
    while (running.load()) {
        epoll_event events[num_events];
        int events_now = epoll_wait(epoll_sock, events, num_events, epoll_timeout);
        if (events_now < 0) {
            if (errno == EINTR)
                continue; // it happens, we'll probably get stopped by setting `running` to false soon
            else
                throw std::runtime_error("networking epoll returned error");
        }
        for (int i = 0; i < events_now; i++)
            ((ep_fd *)events[i].data.ptr)->advance(events[i].events);
    }

    // clean up all sockets involved
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);
    for (client_socket &cl : client_list) {
        // don't call .cleanup() because it invalidates cl
        shutdown(cl.fd, SHUT_RDWR);
        close(cl.fd);
    }
    // at this point it probably doesn't matter if some of them were -1
    close(fifo_handler.read_fd);
    close(fifo_handler.write_fd);
    close(epoll_sock);
}

void ServerImpl::set_fifo(const std::string &read, const std::string &write) {
    try {
        fifo_read_fd = open_fifo(read, O_RDONLY);
        fifo_write_fd = open_fifo(write, O_RDWR);
        fifo_read_path = read;
        fifo_write_path = write;
    } catch (...) {
        // make sure no dangling file descriptors are left
        for (int *fd : {&fifo_read_fd, &fifo_write_fd}) {
            if (*fd != -1) {
                close(*fd);
                *fd = -1;
            }
        }
        throw;
    }
}

} // namespace Blocking
} // namespace Network
} // namespace Afina
