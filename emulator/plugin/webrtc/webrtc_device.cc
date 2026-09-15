#include "webrtc_device.h"

#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "hw/core/qdev.h"
#include "qom/object.h"
}
// IWYU pragma: end_keep
// clang-format on

#include <vector>

#include "android/emulation/control/in_process_rtc_service.h"
#include "goldfish/avd_info/avd_info.h"

#define TYPE_WEBRTC "webrtc"
OBJECT_DECLARE_SIMPLE_TYPE(WebrtcDev, WEBRTC)
struct WebrtcDev {
    DeviceState parent_obj;
    std::vector<std::shared_ptr<::grpc::Service>> rtc_services;
};

namespace goldfish::grpc {
static std::mutex webrtc_service_mutex;
static std::vector<std::shared_ptr<::grpc::Service>> webrtc_services;

std::vector<std::shared_ptr<::grpc::Service>> WebrtcGetServices() {
    std::lock_guard<std::mutex> lock(webrtc_service_mutex);
    return webrtc_services;
}

// NOLINTBEGIN(readability-identifier-naming)
static void webrtc_realize(DeviceState* dev, Error** errp) {
    WebrtcDev* webrtc = WEBRTC(dev);
    webrtc->rtc_services =
            android::emulation::control::CreateInProcessRtcServices(goldfish::avd_info::GetAvd());
    std::lock_guard<std::mutex> lock(webrtc_service_mutex);
    webrtc_services = webrtc->rtc_services;
}

static void webrtc_unrealize(DeviceState* dev) {
    WebrtcDev* webrtc = WEBRTC(dev);
    webrtc->rtc_services.clear();
    std::lock_guard<std::mutex> lock(webrtc_service_mutex);
    webrtc_services.clear();
}

static void webrtc_class_init(ObjectClass* oc, const void* data) {
    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = webrtc_realize;
    dc->unrealize = webrtc_unrealize;
}

static const TypeInfo webrtc_type_info = {
    .name = TYPE_WEBRTC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(WebrtcDev),
    .class_init = webrtc_class_init,
};
// NOLINTEND(readability-identifier-naming)

void webrtc_register_types() {
    type_register_static(&webrtc_type_info);
}

}  // namespace goldfish::grpc
