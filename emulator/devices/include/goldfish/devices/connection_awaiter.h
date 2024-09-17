#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

#include "aemu/base/async/RecurrentTask.h"
#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace devices {

using android::base::Looper;
using android::base::RecurrentTask;
using cable::IPlug;
using cable::PlugPtr;
using cable::SocketPtr;

/**
 * @brief A class that facilitates asynchronous connection establishment with retry logic.
 *
 * `ConnectionAwaiter` simplifies the process of connecting to a remote endpoint by
 * repeatedly attempting to create a connection until it succeeds. It provides a
 * callback mechanism to notify the user when the connection is successfully established.
 */
class ConnectionAwaiter : public IPlug, public std::enable_shared_from_this<ConnectionAwaiter> {
  public:
    /**
     * @brief A function type representing a connection creation attempt.
     *
     * This function takes a `PlugPtr` as input and returns a `SocketPtr` representing
     * the established connection or nullptr if the connection attempt fails.
     */
    using CreateConnection = std::function<SocketPtr(PlugPtr)>;

    /**
     * @brief A function type representing a callback invoked upon successful connection.
     *
     * This function takes a `SocketPtr` as input, representing the established connection.
     */
    using ConnectionCallback = std::function<void(SocketPtr)>;

    /**
     * @brief Destructor. Stops the connection retry task.
     */
    ~ConnectionAwaiter();

    /**
     * @brief Handles successful connection establishment.
     *
     * This method is called when the connection is successfully established.
     */
    void onConnect() override;

    /**
     * This method is not expected to be called in the normal operation of this class,
     * as the socket is typically handed off to another component upon successful connection.
     */
    bool onReceive(const void* data, size_t size) override;

    /**
     * This method is not expected to be called under normal circumstances, as the socket is
     * typically handed off after a successful connection.
     */
    SocketPtr onUnplug() override;

    /**
     * @brief Continuously attempts to establish a connection until successful.
     *
     * This method initiates a repeated process of attempting to create a connection
     * using the provided `createConnection` factory function. It will continue to
     * invoke this function at regular intervals specified by `interval` until the
     * connection is successfully established. Once the connection is established,
     * the `onConnected` callback will be invoked.
     *
     * @param looper The looper instance responsible for scheduling connection retry tasks.
     * @param createConnection A factory function that attempts to create a connection.
     *                         This will be called repeatedly until a connection is made.
     * @param onConnected A callback function that will be triggered once the connection
     *                    is successfully established.
     * @param interval The time interval between successive connection attempts.
     *
     * @return std::shared_ptr<ConnectionAwaiter> A shared pointer to the `ConnectionAwaiter`
     *                                            object that manages the retry process.
     */
    static std::shared_ptr<ConnectionAwaiter> retryUntilConnected(
            Looper* looper, CreateConnection createConnection, ConnectionCallback onConnected,
            std::chrono::milliseconds interval);

  private:
    /**
     * @brief Constructor. Initializes the `ConnectionAwaiter` and starts the retry task.
     *
     * @param looper The looper instance for scheduling tasks.
     * @param createConnection The function to create a connection.
     * @param onConnected The callback to invoke upon successful connection.
     * @param interval The retry interval.
     */
    ConnectionAwaiter(Looper* looper, CreateConnection createConnection,
                      ConnectionCallback onConnected, std::chrono::milliseconds interval);

    /**
     * @brief Attempts to establish a connection.
     *
     * This method attempts to create a connection using the `mCreateConnection` function.
     * It returns true if a connection attempt was made, and false if the connection
     * is already established.
     *
     * @return True if a connection attempt was made, false otherwise.
     */
    bool attemptConnection();

    /// Flag indicating whether a connection is established.
    bool mIsConnected{false};

    /// The established socket connection (if any).
    SocketPtr mSocket{nullptr};

    /// Mutex to protect connection-related operations
    std::mutex mConnectionMutex;

    /// Function to create a connection
    CreateConnection mCreateConnection;

    /// Callback invoked upon successful connection
    ConnectionCallback mOnConnected;

    /// Task responsible for retrying connection attempts
    RecurrentTask mConnectionRetryTask;
};
}  // namespace devices
}  // namespace goldfish