# HAL Devices (`//emulator/plugin/hal/...`) — Agent Playbook

**Namespace:** `goldfish::devices`

Host-side HAL devices. Each HAL is a `HalPlug` that talks to a guest driver over a QEMU pipe; most bridge to a host state channel in `//emulator/plugin/avd_universe`. There is **no** `emulator/plugin/hal/BUILD.bazel`: each HAL is its own package, `emulator/plugin/hal/<device>/`. Transport internals (`cable`, BQL serialization) are in [plug/DESIGN.md](plug/DESIGN.md); read it only when debugging the transport.

---

## 1. Registration Call & Exemplars

The guest's pipe name selects the registry call: `pipe:qemud:<name>[:args]` → `RegisterHalQemuDevice("<name>", …)`; `pipe:<name>[:args]` → `RegisterHalDevice("<name>", …)`. The optional `:args` suffix reaches the factory as its `std::string_view args`. Neither call adds or strips framing, so a QEMUD HAL runs `qemud::Parser` / `qemud::EncodeQemudPacket` itself.

| HAL style | Exemplar (header / implementation) | What to copy |
| :--- | :--- | :--- |
| Binary, 4-byte length prefix | [vehicle_device.h](vehicle/include/goldfish/devices/vehicle/vehicle_device.h) / [vehicle_device.cc](vehicle/vehicle_device.cc); [clipboard_device.h](clipboard/include/goldfish/devices/clipboard/clipboard_device.h) / [clipboard_device.cc](clipboard/clipboard_device.cc) | `RegisterHalDevice`. The byte order follows the guest driver: network order in vehicle (`ntohl` / `htonl`), little-endian in clipboard (`absl::little_endian`). Vehicle sends header and payload in one `Send()`; don't copy clipboard's two `Send()` calls. |
| QEMUD text commands | [boot_properties_device.h](bootproperties/include/goldfish/devices/boot/boot_properties_device.h) (`devices/boot/`, not `devices/bootproperties/`) / [boot_properties_device.cc](bootproperties/boot_properties_device.cc); [fingerprint_device.h](fingerprint/include/goldfish/devices/fingerprint/fingerprint_device.h) / [fingerprint_device.cc](fingerprint/fingerprint_device.cc) | `RegisterHalQemuDevice` + `qemud::Parser` / `qemud::EncodeQemudPacket`. |
| QEMU reset / shutdown hooks | [guest_status_device.h](guest-status/include/goldfish/devices/guest_status/guest_status_device.h) (`devices/guest_status/`, not `devices/guest-status/`) / [guest_status_device.cc](guest-status/guest_status_device.cc) | `EmulatorResetCallbacks`; posts `do_unregister` and shutdown requests to the QEMU loop. Uses `RegisterHalDevice` with little-endian framing. |
| Periodic timer | [gps_device.h](gps/include/goldfish/devices/gps/gps_device.h) / [gps_device.cc](gps/gps_device.cc); [sensor_device.h](sensors/include/goldfish/devices/sensor/sensor_device.h) (`devices/sensor/`, not `devices/sensors/`) / [sensor_device.cc](sensors/sensor_device.cc) | GPS: `ScheduleRepeating` + `weak_from_this()`, registered with `qemu_loop` as both loops, so all its callbacks run on `qemu_loop`. Sensors: `CreateTimer` + `Timer::Schedule` on `client_loop`, kept alive by `self_ = shared_from_this()` until `OnClose()` posts `self_.reset()`. |
| Host socket proxy | [unix_pipe.h](unix_pipe/include/goldfish/devices/unix_pipe/unix_pipe.h) / [unix_pipe.cc](unix_pipe/unix_pipe.cc) | `RegisterHalDevice` + `LibuvAsyncSocketFactory`; the pipe `args` is the host socket path. |

---

## 2. Core Headers, Targets & Exact Signatures

You should not need to open these headers unless you are debugging an existing HAL.

| Component | Bazel Target & `#include` | Exact C++ |
| :--- | :--- | :--- |
| `IConnectorRegistry` | `//emulator/plugin/hal/connector:connector_registry`<br>`"goldfish/devices/connector_registry.h"` ([file](connector/include/goldfish/devices/connector_registry.h)) | `using HalDeviceFactory = std::function<std::shared_ptr<HalPlug>(std::string_view args)>;`<br>`void RegisterHalDevice(std::string name, async::EventLoop* client_loop, async::EventLoop* qemu_loop, HalDeviceFactory factory);`<br>`void RegisterHalQemuDevice(std::string name, async::EventLoop* client_loop, async::EventLoop* qemu_loop, HalDeviceFactory factory);`<br>Never call the deprecated `IConnectorRegistry::RegisterDevice` / `RegisterQemuDevice` (legacy `cable::IPlug` + `DeviceFactory`). |
| `HalPlug` & `HalSocket` | `//emulator/plugin/hal/plug:hal_plug`<br>`"goldfish/devices/internal/hal_plug.h"` ([file](plug/include/goldfish/devices/internal/hal_plug.h)) | `class HalPlug` (no `std::enable_shared_from_this` base; a device that needs `shared_from_this()` / `weak_from_this()` inherits `public std::enable_shared_from_this<MyDevice>` alongside its interface)<br>`virtual void OnConnect() = 0;`<br>`virtual void OnReceive(std::string_view data) = 0;` (`data` is valid only during the call)<br>`virtual void OnClose() = 0;`<br>`protected: std::shared_ptr<HalSocket> Socket() const;` → `Send(std::string_view)`, `Close()` |
| `qemud` | `//emulator/plugin/hal/connector:qemud`<br>`"goldfish/devices/qemud/qemud.h"` ([file](connector/include/goldfish/devices/qemud/qemud.h)) | Namespace `goldfish::devices::qemud`:<br>`struct Parser { using Sink = std::function<bool(const void* data, size_t size)>; explicit Parser(Sink sink); bool OnReceive(const void* data, size_t size); };`<br>`std::string EncodeQemudPacket(std::string_view packet);` (free function) |
| `EmulatorResetCallbacks` | `//emulator/plugin/hal/common:emulator_reset`<br>`"goldfish/devices/emulator_reset.h"` ([file](common/include/goldfish/devices/emulator_reset.h)) | `struct EmulatorResetCallbacks {`<br>`  using QEMUResetHandler = void(void* opaque);`<br>`  using RegisterEmulatorReset = void (*)(QEMUResetHandler* func, void* opaque);`<br>`  using UnregisterEmulatorReset = void (*)(QEMUResetHandler* func, void* opaque);`<br>`  RegisterEmulatorReset do_register;`<br>`  UnregisterEmulatorReset do_unregister;`<br>`};`<br>The aliases are nested raw function pointers: write `EmulatorResetCallbacks::UnregisterEmulatorReset`, and pass only free functions or captureless lambdas. |
| `ObservableValue` & `MakeScopedCallback` | `//emulator/libs/eventing:eventing`<br>`"goldfish/eventing/observable_value.h"`, `"goldfish/eventing/with_callbacks.h"` | `using ObservableData = goldfish::eventing::ObservableValue<Data, goldfish::eventing::ObservableValueTriggerAlways>;`<br>`using Subscription = std::unique_ptr<android::base::eventing::ScopedEventCallback<ObservableData, Data>>;`<br>`Subscription sub = android::base::eventing::MakeScopedCallback(observable, [weak_dev](const Data& d) { … });` |
| `TestConnectorRegistry` | `//emulator/plugin/hal/connector:test-helpers`<br>`"goldfish/devices/test_connector_registry.h"` (the file is under [connector/test/](connector/test/goldfish/devices/test_connector_registry.h), not `connector/include/`) | `template <typename T> T* ConstructHalDevice(std::string_view args = {});` (runs the factory; never calls `OnConnect()`)<br>`TestHalSocket* HalSocket();` (`std::string storage`, `std::function<void(std::string_view)> on_send`, `bool closed`)<br>`void Close();` (runs `OnClose()` synchronously on the calling thread) |

---

## 3. Threading & Lifecycle

1. **The factory runs on the QEMU main thread (under the BQL)**, once per guest connection. Create the device and its subscriptions there, but keep it fast; start timers and heavier work in `OnConnect()`.
2. **`OnConnect()`, `OnReceive()`, and `OnClose()` are serialized on `client_loop`.** `Socket()->Send()` is thread-safe and marshals each call to `qemu_loop` as one unit, so send a whole frame (header + payload) per call.
3. **`Socket()` validity:** inside the factory and constructor it is a no-op `NullHalSocket`. Treat it as usable only between `OnConnect()` and `OnClose()`; after a guest close, `Send()` is silently dropped. The transport may deliver `OnReceive()` (handshake or argument bytes) *before* `OnConnect()`, so buffer early bytes if replying needs `Socket()`.
4. **`OnClose()` fires when the guest or the vsock transport ends the connection** (guest close/reset, virtio reset, snapshot load, `vsock::clear()` at shutdown), never after the HAL's own `Socket()->Close()`. If the HAL closes the socket itself, do the same cleanup first (cancel timers, drop subscriptions, post `do_unregister` to `qemu_loop`), or a reset handler registered with `this` dangles.
5. **QEMU-thread-only calls** (`do_unregister`, shutdown requests) are posted to the QEMU loop (see [guest_status_device.cc](guest-status/guest_status_device.cc)):
   ```cpp
   qemu_loop_->Post([self = shared_from_this()] {
       self->unregister_reset_(&MyDevice::OnQemuReset, self.get());
   }).IgnoreError();
   ```
6. **Host→guest subscriptions:** call `MakeScopedCallback` inside the `HalDeviceFactory` lambda that your `RegisterDevice` passes to `RegisterHal[Qemu]Device`, capture a `std::weak_ptr<MyDevice>` (never a `shared_ptr` or raw `this`), and store the returned handle on the device (see `IClipboardDevice::RegisterDevice` in [clipboard_device.cc](clipboard/clipboard_device.cc)). If the callback touches device state or timers, post to the device's loop *before* calling `weak_dev.lock()`, so `weak_dev.lock()` never runs while the observable holds its callback mutex (`ObservableValue::SetValue()` fires callbacks under its `std::mutex`; see `IGpsDevice::RegisterDevice` in [gps_device.cc](gps/gps_device.cc)).

---

## 4. Wire Framing & Skeletons

### A. Binary HAL with a 4-byte length prefix (`RegisterHalDevice`)
```cpp
// channel_ is MyChannel& (see §5.1); receive_data_ is std::vector<char>.
// Use the guest driver's byte order (vehicle uses ntohl/htonl).
void OnReceive(std::string_view data) override {
    receive_data_.insert(receive_data_.end(), data.begin(), data.end());
    while (receive_data_.size() >= sizeof(uint32_t)) {
        const uint32_t size = absl::little_endian::Load32(receive_data_.data());
        if (receive_data_.size() < sizeof(uint32_t) + size) return;
        std::string payload(receive_data_.data() + sizeof(uint32_t), size);
        receive_data_.erase(receive_data_.begin(), receive_data_.begin() + sizeof(uint32_t) + size);
        channel_.guest_to_host.SetValue(MyData{std::move(payload)});
    }
}
void SendToGuest(std::string_view payload) {
    std::string packet(sizeof(uint32_t) + payload.size(), '\0');
    absl::little_endian::Store32(packet.data(), static_cast<uint32_t>(payload.size()));
    if (!payload.empty()) packet.replace(sizeof(uint32_t), payload.size(), payload);
    Socket()->Send(std::move(packet));  // One Send() per frame: no header/payload interleaving.
}
```

### B. QEMUD text HAL with a reset hook (`RegisterHalQemuDevice`)
```cpp
class MyQemudDevice : public IMyQemudDevice, public std::enable_shared_from_this<MyQemudDevice> {
  public:
    MyQemudDevice(EmulatorResetCallbacks reset, async::EventLoop* qemu_loop)
            : qemu_loop_(qemu_loop),
              unregister_reset_(reset.do_unregister),
              qemud_parser_([this](const void* data, size_t size) {
                  return HandleCommand(std::string_view(static_cast<const char*>(data), size));
              }) {
        if (reset.do_register) reset.do_register(&MyQemudDevice::OnReset, this);
    }
    void OnConnect() override {}
    void OnReceive(std::string_view data) override { qemud_parser_.OnReceive(data.data(), data.size()); }
    void OnClose() override {
        qemu_loop_->Post([self = shared_from_this()] {
            if (self->unregister_reset_) self->unregister_reset_(&MyQemudDevice::OnReset, self.get());
        }).IgnoreError();
    }
    bool HandleCommand(std::string_view cmd) {
        Socket()->Send(qemud::EncodeQemudPacket("OK"));
        return true;
    }
    static void OnReset(void* opaque) { /* handle QEMU reset */ }

  private:
    async::EventLoop* qemu_loop_;
    EmulatorResetCallbacks::UnregisterEmulatorReset unregister_reset_;
    qemud::Parser qemud_parser_;
};
```
- **Locale-independent text:** format floating-point values for the guest with `absl::StrFormat`, or replace `,` with `.` as [sensor_device.cc](sensors/sensor_device.cc) does, so the host locale never corrupts guest parsing.

---

## 5. Wiring a New HAL End-to-End

1. **Host state channel** (`//emulator/plugin/avd_universe`): reuse or add a channel in [avd_universe/BUILD.bazel](../avd_universe/BUILD.bazel) (e.g. `//emulator/plugin/avd_universe:clipboard`, `//emulator/plugin/avd_universe:vehicle`) and expose it on `AvdUniverse` ([avd_universe.h](../avd/include/goldfish/avd/avd_universe.h)). Two-way channels (clipboard, vehicle) are a struct of two `ObservableValue`s: `host_to_guest` (the HAL subscribes and sends to the guest) and `guest_to_host` (the HAL calls `SetValue`); see `ClipboardChannel` in [clipboard_data.h](../avd_universe/include/goldfish/avd_universe/clipboard/clipboard_data.h). One-way state is a single `ObservableValue` (e.g. `ObservableLocation`, `ObservableFingerprintSensor`).
2. **HAL package** `emulator/plugin/hal/<device>/`: the only public header, `include/goldfish/devices/<domain>/<name>_device.h`, declares `class I<Name>Device : public HalPlug` with `static constexpr std::string_view kServiceName` and `static void RegisterDevice(...)`, which calls `registry->RegisterHalDevice` or `RegisterHalQemuDevice`. The concrete class stays private in `<name>_device.cc`.
3. **Startup:** call `I<Name>Device::RegisterDevice(...)` in `AvdExtendedUniverse::Initialize()` ([avd_extended_universe.cc](../avd/avd_extended_universe.cc)) and add `//emulator/plugin/hal/<device>` to the deps of `//emulator/plugin/avd:avd_extended_universe` in [avd/BUILD.bazel](../avd/BUILD.bazel).

**`BUILD.bazel` pattern** (model: [clipboard/BUILD.bazel](clipboard/BUILD.bazel); load `cc_library` / `cc_test` from `@rules_cc//cc:defs.bzl` and `clang_tidy_test` from `@aemu//tools/clang_tidy:defs.bzl`):
- `cc_library`: `alwayslink = True`, `includes = ["include"]`, deps `//emulator/plugin/avd_universe:<channel>`, `//emulator/plugin/hal/connector:connector_registry`, plus `//emulator/plugin/hal/connector:qemud` (QEMUD HALs), `//emulator/plugin/hal/common:emulator_reset` (reset hooks), `//emulator/libs/async:async_api` (timers or posting), and the Abseil libraries you use.
- `cc_test`: deps `:<lib>`, `//emulator/plugin/hal/connector:test-helpers`, `//emulator/plugin/hal/plug:fake_vsock`, `//emulator/libs/test:googletest_main`, plus `//emulator/libs/async:async_testing` when the test uses `TestEventLoop`.
- `clang_tidy_test(name = "tidy", targets = [":<lib>"], tidy_config_file = "//:clang_tidy_config")`.

---

## 6. Unit Testing (`TestConnectorRegistry`)

Model on [clipboard_device_test.cc](clipboard/clipboard_device_test.cc):

```cpp
using ::goldfish::async::testing::TestEventLoop;
class MyHalDeviceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        IMyHalDevice::RegisterDevice(&channel_, &registry_, client_loop_.get(), qemu_loop_.get());
        device_ = registry_.ConstructHalDevice<IMyHalDevice>();
        test_socket_ = registry_.HalSocket();
        test_socket_->storage.clear();
    }
    void TearDown() override {
        registry_.Close();  // Required: ~TestConnectorRegistry() DCHECKs that the plug was closed.
        client_loop_->RunAll();
        qemu_loop_->RunAll();
    }
    MyChannel channel_;
    std::unique_ptr<TestEventLoop> client_loop_ = TestEventLoop::Create();
    std::unique_ptr<TestEventLoop> qemu_loop_ = TestEventLoop::Create();
    TestConnectorRegistry registry_;
    TestHalSocket* test_socket_ = nullptr;
    IMyHalDevice* device_ = nullptr;
};
```
- **Guest → host:** call `device_->OnReceive(chunk)`; cover split and partial 4-byte headers and several frames coalesced in one chunk.
- **Host → guest:** inspect `test_socket_->storage` or set `test_socket_->on_send`.
- **Loop-affine devices:** `ConstructHalDevice()` never calls `OnConnect()`, `registry_.Close()` runs `OnClose()` on the test thread, and each `TestEventLoop` runs its tasks on its own thread. If the device starts work in `OnConnect()` or DCHECKs `IsOnLoopThread()`, call `OnConnect()` / `OnReceive()` / `registry_.Close()` through `loop->Post([&] { … }).IgnoreError(); loop->RunAll();`, and fire timers with `loop->AdvanceClock(absl::Seconds(1))` (model: [gps_device_test.cc](gps/gps_device_test.cc)).

---

## 7. Build & Test Commands

```bash
bazel test --test_output=errors @goldfish//emulator/plugin/hal/clipboard:clipboard_unittests  # one HAL
bazel test --test_output=errors @goldfish//emulator/plugin/hal/...                            # all HALs
bazel test --config=tsan --test_output=errors @goldfish//emulator/plugin/hal/...              # concurrency or framing changes; also --config=asan
bazel test --config=tidy --@aemu//tools/clang_tidy:clang_tidy_check_files=plugin/hal/ @goldfish//emulator/plugin/hal/...
```
