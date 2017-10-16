#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <pthread.h>
#include <signal.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <afina/Storage.h>

namespace Afina {
namespace Network {
namespace Blocking {

template <void (ServerImpl::*method)()> static void *PthreadProxy(void *p) {
    ServerImpl *srv = reinterpret_cast<ServerImpl *>(p);
    try {
        (srv->*method)();
        return (void *)0;
    } catch (std::runtime_error &ex) {
        std::cerr << "Exception caught" << ex.what() << std::endl;
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

    // Setup server parameters BEFORE thread is created, that will guarantee
    // variable value visibility
    max_workers = n_workers;
    listen_port = port;
    // NOTE: Actually, this doesn't guarantee the visibility because of per-core cache.
    // Stricty speaking, we should issue a memory barrier before creating a new thread.
    // Waiting on a mutex does that for us.

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
        throw std::runtime_error("server thread encountered an error");
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
        std::cout << "network debug: waiting for connection..." << std::endl;

        // When an incoming connection arrives, accept it. The call to accept() blocks until
        // the incoming connection arrives
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
            close(server_socket);
            throw std::runtime_error("Socket accept() failed");
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
    }

    // Cleanup on exit...
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
}

} // namespace Blocking
} // namespace Network
} // namespace Afina
