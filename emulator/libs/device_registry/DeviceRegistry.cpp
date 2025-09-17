
#include "goldfish/device_registry/DeviceRegistry.h"

namespace goldfish {

DeviceRegistry& DeviceRegistry::get() {
    static DeviceRegistry instance;
    return instance;
}

std::unique_ptr<DeviceRegistry> DeviceRegistry::testRegistry() {
    return std::unique_ptr<DeviceRegistry>(new DeviceRegistry());
}

}  // namespace goldfish
