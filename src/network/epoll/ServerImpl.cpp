#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <pthread.h>
#include <signal.h>

#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>

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
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps) : Server(ps) {
    pthread_mutex_init(&client_socket_lock, nullptr);
    pthread_cond_init(&client_socket_cv, nullptr);
}

// See Server.h
ServerImpl::~ServerImpl() {
    pthread_cond_destroy(&client_socket_cv);
    pthread_mutex_destroy(&client_socket_lock);
}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that would kill the process. We want to ignore this signal, so that
    // send() just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    if ((uint16_t)port != port)
        throw std::overflow_error("port wouldn't fit in a 16-bit value");
    // Setup server parameters BEFORE thread is created, that will guarantee
    // variable value visibility
    max_workers = n_workers;
    listen_port = port;

    // The pthread_create function creates a new thread.
    //
    // The first parameter is a pointer to a pthread_t variable, which we can use
    // in the remainder of the program to manage this thread.
    //
    // The second parameter is used to specify the attributes of this new thread
    // (e.g., its stack size). We can leave it NULL here.
    //
    // The third parameter is the function this thread will run. This function *must*
    // have the following prototype:
    //    void *f(void *args);
    //
    // Note how the function expects a single parameter of type void*. We are using it to
    // pass this pointer in order to proxy call to the class member function. The fourth
    // parameter to pthread_create is used to specify this parameter value.
    //
    // The thread we are creating here is the "server thread", which will be
    // responsible for listening on port 23300 for incoming connections. This thread,
    // in turn, will spawn threads to service each incoming connection, allowing
    // multiple clients to connect simultaneously.
    // Note that, in this particular example, creating a "server thread" is redundant,
    // since there will only be one server thread, and the program's main thread (the
    // one running main()) could fulfill this purpose.
    running.store(true);
    if (pthread_create(&accept_thread, NULL, &PthreadProxy<&ServerImpl::RunAcceptor>, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
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
    if (pthread_join(accept_thread, &retval))
        throw std::runtime_error("pthread_join failed");
    if (retval) // better late than never
        throw std::runtime_error("server thread had encountered an error");
}

// See Server.h
void ServerImpl::RunAcceptor() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // For IPv4 we use struct sockaddr_in:
    // struct sockaddr_in {
    //     short int          sin_family;  // Address family, AF_INET
    //     unsigned short int sin_port;    // Port number
    //     struct in_addr     sin_addr;    // Internet address
    //     unsigned char      sin_zero[8]; // Same size as struct sockaddr
    // };
    //
    // Note we need to convert the port to network order

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(listen_port); // TCP port number, downcasted from 32-bit to 16-bit type
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    // Arguments are:
    // - Family: IPv4
    // - Type: Full-duplex stream (reliable)
    // - Protocol: TCP
    int server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    // when the server closes the socket,the connection must stay in the TIME_WAIT state to
    // make sure the client received the acknowledgement that the connection has been terminated.
    // During this time, this port is unavailable to other processes, unless we specify this option
    //
    // This option let kernel knows that we are OK that another process may listen on the same
    // port. In a such case kernel will balance input traffic between all listeners (except those which
    // are closed already)
    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    // Bind the socket to the address. In other words let kernel know data for what address we'd
    // like to see in the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    // Start listening. The second parameter is the "backlog", or the maximum number of
    // connections that we'll allow to queue up (while the application is *not* doing accept()
    // for them). Note that listen() doesn't block until incoming connections arrive. It
    // just makesthe OS aware that this process is willing to accept connections on this
    // socket (which is bound to a specific IP and port)
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    struct sockaddr_in client_addr;
    socklen_t sinSize = sizeof(struct sockaddr_in);
    if (pthread_mutex_lock(&client_socket_lock))
        throw std::runtime_error("couldn't lock client socket mutex");
    while (running.load()) {
        std::cout << "network debug: waiting for connection..." << std::endl;

        // When an incoming connection arrives, accept it. The call to accept() blocks until
        // the incoming connection arrives
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
            close(server_socket);
            throw std::runtime_error("Socket accept() failed");
        }

        // the requirement is: access the worker threads only from the accept_thread
        // so I do
        // this reaps the dead children to make space for more client connections
        // (I think I could implement it with detached threads and a simple shared atomic counter, though)
        for (auto it = connections.begin(); it != connections.end();) {
            if (pthread_kill(*it, 0)) { // doesn't kill, does check
                void *retval;
                if (pthread_join(*it, &retval))
                    throw std::runtime_error("failed to join a dead client thread");
                if (retval)
                    throw std::runtime_error("client thread had encountered an error");
                // we're still alive?
                it = connections.erase(it);
            } else
                ++it;
        }

        if (connections.size() >= max_workers) {
            send(client_socket, "SERVER_ERROR Вас много -- я одна\r\n", strlen("SERVER_ERROR Вас много -- я одна\r\n"),
                 MSG_DONTWAIT);
            // we don't care, we should close as soon as possible and get on with it
            // but this should fit in a single TCP packet even if MTU is as small as 800, shouldn't it?
            shutdown(client_socket, SHUT_RDWR);
            close(client_socket);
            continue;
        }

        auto it = connections.emplace(connections.end());
        if (pthread_create(&*it /* yeah, it's a pointer to a dereferenced iterator */, NULL,
                           PthreadProxy<&ServerImpl::RunConnection>, this) < 0) {
            throw std::runtime_error("couldn't create client socket thread failed");
        }
        if (pthread_cond_wait(&client_socket_cv,
                              &client_socket_lock)) // make sure that the client thread gets the socket
            throw std::runtime_error("couldn't wait on client socket condvar");
        // NOTE: this may still fail because of spurious wakeups.
        // The best way to be sure would be to add a boolean flag which this method would set to false
        // and wait for it to become true in a while loop
    }

    // Cleanup on exit...
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);

    if (pthread_mutex_unlock(&client_socket_lock))
        throw std::runtime_error("couldn't unlock client socket mutex");

    // at this point we have to clean up all client threads
    for (pthread_t &thread : connections) {
        void *retval;
        if (pthread_join(thread, &retval))
            throw std::runtime_error("failed to join a dead client thread");
        if (retval)
            throw std::runtime_error("client thread had encountered an error");
    }
}

// better test with a really small buffer to catch possible errors
static const size_t read_buffer_size = 256;

// See Server.h
void ServerImpl::RunConnection() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // first and foremost, retrieve the socket and send the accept thread "all OK"
    if (pthread_mutex_lock(&client_socket_lock))
        throw std::runtime_error("couldn't lock client socket mutex");
    int client = client_socket;
    if (pthread_cond_signal(&client_socket_cv))
        throw std::runtime_error("couldn't signal client socket condvar");
    if (pthread_mutex_unlock(&client_socket_lock))
        throw std::runtime_error("couldn't unlock client socket mutex");

#if defined(SO_KEEPALIVE) && defined(TCP_KEEPCNT) && defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL)
    // this ensures fast shutdown. some production applications won't like that
    {
        int enabled = 1, ka_cnt = 3, ka_idle = 3, ka_intl = 1;
        if (setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled)) ||
            setsockopt(client, IPPROTO_TCP, TCP_KEEPCNT, &ka_cnt, sizeof(ka_cnt)) ||
            setsockopt(client, IPPROTO_TCP, TCP_KEEPIDLE, &ka_cnt, sizeof(ka_idle)) ||
            setsockopt(client, IPPROTO_TCP, TCP_KEEPINTVL, &ka_cnt, sizeof(ka_intl)))
            throw std::runtime_error("couldn't enable keep-alive on the client socket");
    }
#endif

    Afina::Protocol::Parser parser;

    for (;;) { // the loop ends when recv()/send() fails
        std::vector<char> buf;
        buf.reserve(read_buffer_size);

        // both parser and command may throw exceptions
        std::string out;
        try {
            // first, read & parse the command
            ssize_t received = 0;
            size_t parsed = 0;
            do {
                // move excess data to the beginning of the buffer
                memmove(buf.data(), buf.data() + parsed, received - parsed);
                // append whatever the client may have sent
                received = recv(client, buf.data(), buf.capacity() - received + parsed, 0);
                if (received <= 0) { // client bails out, no command to execute
                    shutdown(client, SHUT_RDWR);
                    close(client);
                    return;
                }
            } while (!parser.Parse(buf.data(), received, parsed));

            // parser.Parse returned true -- can build a command now
            uint32_t arg_size;
            auto cmd = parser.Build(arg_size);

            std::string arg;
            // was there an argument?
            if (arg_size) {
                arg_size += 2; // data is followed by \r\n
                buf.reserve(arg_size);

                size_t offset = 0;
                if (received - parsed) { // was there any excess?
                    // as usual, move it to the beginning
                    memmove(buf.data(), buf.data() + parsed, received - parsed);
                    offset += received - parsed; // and account for it
                }

                while (offset < arg_size) { // append the body we know the size of
                    received = recv(client, buf.data() + offset, buf.capacity() - offset, 0);
                    if (received <= 0) { // client bails out, no data to store
                        shutdown(client, SHUT_RDWR);
                        close(client);
                        return;
                    }
                    offset += received;
                }
                // prepare the body
                arg.assign(buf.data(), offset - 2 /* account for extra \r\n */);
            }

            // time to do the deed
            cmd->Execute(*pStorage, arg, out);
        } catch (std::runtime_error &e) {
            // if anything fails we just report the error to the user
            out = std::string("CLIENT_ERROR ") + e.what();
        }

        if (out.size()) {
            out += std::string("\r\n");
            size_t offset = 0;
            ssize_t sent;
            while (offset < out.size()) { // classical "send until nothing left or error" loop
                sent = send(client, out.data() + offset, out.size() - offset, 0);
                if (sent <= 0) { // client bails out, reply not sent
                    shutdown(client, SHUT_RDWR);
                    close(client);
                    return;
                }
                offset += sent;
            }
        }
    }
}

} // namespace Blocking
} // namespace Network
} // namespace Afina
