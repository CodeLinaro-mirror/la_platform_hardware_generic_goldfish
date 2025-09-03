#include "goldfish/avd/global-event-loop.h"

#include <memory>
#include <utility>

#include "absl/base/call_once.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

namespace goldfish::async {

namespace {

using ::goldfish::async::EventLoop;
// Use a unique_ptr to ensure the event loop is cleaned up on program exit.
// TODO(jansene) b/441087461, make sure we do a "nice" shutdown during
// qemu shutdown event.
static std::unique_ptr<EventLoop> sGlobalEventLoop;
static absl::once_flag sInitOnce;

}  // namespace

EventLoop* globalEventLoop() {
    // This function uses absl::call_once to ensure that the default
    // loop is only created and started once.
    absl::call_once(sInitOnce, [] {
        // Create the default ThreadedEventLoop and store it in the unique_ptr.
        sGlobalEventLoop = ::goldfish::async::ThreadedEventLoop::create(
                ::goldfish::async::LibuvEventLoop::create());
    });

    return sGlobalEventLoop.get();
}

namespace testing {
void setGlobalEventLoopForTesting(EventLoop* newLoop) {
    sGlobalEventLoop.reset(newLoop);
}
}  // namespace testing

}  // namespace goldfish::async
