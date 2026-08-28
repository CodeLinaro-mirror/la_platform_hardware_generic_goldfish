#include "goldfish/async/testing/global_event_loop.h"

#include <memory>
#include <utility>

#include "absl/base/call_once.h"
#include "absl/base/no_destructor.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

namespace goldfish::async {

namespace {

using ::goldfish::async::EventLoop;
// See b/539403339: Use absl::NoDestructor to prevent CRT static destructor execution
// during process exit. On Windows, ntdll!LdrShutdownProcess suspends background worker threads
// before running static destructors. Destroying a ThreadedEventLoop in a static destructor causes a
// deadlock and STATUS_FATAL_APP_EXIT.
static absl::NoDestructor<std::unique_ptr<EventLoop>> global_event_loop;
static absl::once_flag init_once;

}  // namespace

EventLoop* globalEventLoop() {
    // This function uses absl::call_once to ensure that the default
    // loop is only created and started once.
    absl::call_once(init_once, [] {
        *global_event_loop = ::goldfish::async::ThreadedEventLoop::Create(
                ::goldfish::async::LibuvEventLoop::Create("GlobalLoop"));
    });

    return global_event_loop->get();
}

namespace testing {
void setGlobalEventLoopForTesting(EventLoop* new_loop) {
    global_event_loop->reset(new_loop);
}
}  // namespace testing

}  // namespace goldfish::async
