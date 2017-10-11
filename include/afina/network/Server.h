#ifndef AFINA_NETWORK_SERVER_H
#define AFINA_NETWORK_SERVER_H

#include <memory>
#include <vector>

namespace Afina {
class Storage;
namespace Network {

/**
 * # Network processors coordinator
 * Configure resources for the network processors and coordinates all work
 */
class Server {
public:
    Server(std::shared_ptr<Afina::Storage> ps) : pStorage(ps) {}
    virtual ~Server() {}

    /**
     * Starts the network service. After method returns process should
     * listen on the given interface/port pair to process incoming
     * data in `workers` number of threads
     */
    virtual void Start(uint32_t port, uint16_t workers = 1) = 0;

    /**
     * Signal all worker threads that server is going to shutdown. After method returns
     * no more connections should be accepted, existing connections should stop receiving commands,
     * but must wait until currently run commands are executed.
     *
     * After existing connections drain each should be closed and once worker has no more connections
     * its thread should return
     */
    virtual void Stop() = 0;

    /**
     * Blocks calling thread until all workers are stopped and all resources allocated for the network
     * are released
     */
    virtual void Join() = 0;

protected:
    /**
     * Instance of backing storeage on which current server should execute commands
     */
    std::shared_ptr<Afina::Storage> pStorage;
};

} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_SERVER_H
