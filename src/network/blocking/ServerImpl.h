#ifndef AFINA_NETWORK_BLOCKING_SERVER_H
#define AFINA_NETWORK_BLOCKING_SERVER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <pthread.h>
#include <unordered_set>

#include <afina/network/Server.h>

namespace Afina {
namespace Network {
namespace Blocking {

/**
 * # Network resource manager implementation
 * Server that is spawning a separate thread for each connection
 */
class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<Afina::Storage> ps);
    ~ServerImpl();

    // See Server.h
    void Start(uint32_t port, uint16_t workers) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

protected:
    /**
     * Method is running in the connection acceptor thread
     */
    void RunAcceptor();

    /**
     * Methos is running for each connection
     */
    void RunConnection();

private:
    static void *RunAcceptorProxy(void *p);

    // Atomic flag to notify threads when it is time to stop. Note that
    // flag must be atomic in order to safely publish changes cross thread
    // bounds
    std::atomic<bool> running;

    // Thread that is accepting new connections
    pthread_t accept_thread;
    // The socket for the next client thread to work with
    int client_socket;
    // And the required synchronization to pass it from accept to client thread
    pthread_mutex_t client_socket_lock;
    pthread_cond_t client_socket_cv;
    bool client_okay;

    // Threads that are talking to clients
    // NOTE: access is permitted only from inside of accept_thread
    std::unordered_set<pthread_t> connections;

    // Maximum number of client allowed to exist concurrently
    // on the server
    // NOTE: access is permitted only from inside of accept_thread
    uint16_t max_workers;

    // Port to listen for new connections
    // NOTE: access is permitted only from inside of accept_thread
    uint32_t listen_port;
};

} // namespace Blocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_BLOCKING_SERVER_H
