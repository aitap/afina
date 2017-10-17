#include "ServerImpl.h"

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
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps) : Server(ps) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0)
        throw std::runtime_error("Unable to mask SIGPIPE");

    // Setup server parameters BEFORE thread is created, that will guarantee
    // variable value visibility
    if ((uint16_t)port == port)
        listen_port = (uint16_t)port;
    else
        throw std::overflow_error("port wouldn't fit in a 16-bit value");

    running.store(true);
    if (pthread_create(&epoll_thread, NULL, &PthreadProxy<&ServerImpl::RunEpoll>, this) < 0)
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
    if (pthread_join(epoll_thread, &retval))
        throw std::runtime_error("pthread_join failed");
    if (retval) // better late than never
        throw std::runtime_error("epoll thread had encountered an error");
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
    ep_fd(int fd_) : fd(fd_) {}
    virtual void advance() = 0;
    virtual ~ep_fd() {}
};

static int epoll_modify(int epoll_fd, int how, uint32_t events, ep_fd &target) {
    struct epoll_event new_ev {
        .events = events, .data.ptr = (void *)&target
    };
    return epoll_ctl(epoll_fd, how, target.fd, &new_ev);
}

struct client_fd : ep_fd {
    int epoll_fd;
    std::shared_ptr<Afina::Storage> ps;
    std::vector<char> buf;
    std::list<client_fd> &list;
    std::list<client_fd>::iterator self;
    client_fd(int fd_, int epoll_fd_, std::shared_ptr<Afina::Storage> ps_, std::list<client_fd> &list_,
              std::list<client_fd>::iterator self_)
        : ep_fd(fd_), epoll_fd(epoll_fd_), ps(ps_), list(list_), self(self_) {}
    void advance() override { throw std::runtime_error("unimplemented"); }
};

struct listen_fd : ep_fd {
    int epoll_fd;
    std::shared_ptr<Afina::Storage> ps;
    std::list<client_fd> &client_list;
    listen_fd(int fd_, int epoll_fd_, std::shared_ptr<Afina::Storage> ps_, std::list<client_fd> &client_list_)
        : ep_fd(fd_), epoll_fd(epoll_fd_), ps(ps_), client_list(client_list_) {}
    void advance() override {
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
            if (epoll_modify(epoll_fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT, *cl_it))
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
const int epoll_timeout = 1000; // ms, to check every now and then that we still need to be running

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

    int reuseaddr = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) == -1) {
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
    std::list<client_fd> client_list;
    listen_fd listening_object{server_socket, epoll_sock, pStorage, client_list};
    // add the listen socket to the epoll set
    if (epoll_modify(epoll_sock, EPOLL_CTL_ADD, EPOLLIN, listening_object))
        throw std::runtime_error("epoll_ctl failed to add the listen socket");

    // main loop
    while (running.load()) {
        std::cout << "network debug: waiting for connection..." << std::endl;
        epoll_event events[num_events];
        int events_now = epoll_wait(epoll_sock, events, num_events, epoll_timeout);
        if (events_now < 0)
            throw std::runtime_error("epoll failed");
        for (int i = 0; i < events_now; i++)
            ((ep_fd *)events[i].data.ptr)->advance();
    }

    // clean up all sockets involved
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);
    for (client_fd &cl : client_list) {
        shutdown(cl.fd, SHUT_RDWR);
        close(cl.fd);
    }
    close(epoll_sock);
}

} // namespace Blocking
} // namespace Network
} // namespace Afina
