
#include "configure_drives.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"

#include "android/base/testing/TestSystem.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/mock_avd.h"
#include "disk_drive.h"

namespace android::goldfish {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

using android::base::System;

class MockDeviceContainer {
  public:
    MOCK_METHOD(const Avd&, avd, (), (const));
    MOCK_METHOD(const AndroidOptions&, opts, (), (const));

    MOCK_METHOD(void, addRwDrive,
                (const std::string& id, const std::string& pci_address,
                 const std::optional<fs::path>& system_image_path_ro,
                 const fs::path& user_image_path, const fs::path& qcow2, uint64_t size_bytes,
                 bool wipe_existing));

    MOCK_METHOD(void, addRoDrive,
                (const std::string& id, const std::string& pci_address, const fs::path& image));

    template <class T, class... Args>
    void addDevice(Args&&... args) {
        if constexpr (std::is_same_v<T, RwDrive>) {
            addRwDrive(std::forward<Args>(args)...);
        } else if constexpr (std::is_same_v<T, RoDrive>) {
            addRoDrive(std::forward<Args>(args)...);
        }
    }
};

}  // namespace

TEST(ConfigureDrivesTest, AddDrives) {
    auto launcher_path = std::filesystem::temp_directory_path();
    base::TestSystem sys(launcher_path);
    auto user_dir = launcher_path / "user";
    auto system_dir = launcher_path / "system";
    fs::create_directory(user_dir);
    fs::create_directory(system_dir);
    fs::create_directory(system_dir / "data");

    System::get()->envSet("ANDROID_EMULATOR_HOME", launcher_path.string());

    MockDeviceContainer mock_container;
    MockAvd mock_avd;
    //     auto avd = std::make_unique<MockAvd>();
    //     MockAvd* avd_ptr = avd.get();
    AndroidOptions opts{};
    HardwareConfig hw;

    EXPECT_CALL(mock_container, avd()).WillRepeatedly(ReturnRef(mock_avd));
    EXPECT_CALL(mock_container, opts()).WillRepeatedly(ReturnRef(opts));
    EXPECT_CALL(mock_avd, hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(mock_avd, getSystemImageFilePath(Avd::ImageType::INITSYSTEM))
            .WillRepeatedly(Return(system_dir / "system.img"));
    EXPECT_CALL(mock_avd, getSystemImageFilePath(Avd::ImageType::INITVENDOR))
            .WillRepeatedly(Return(system_dir / "vendor.img"));
    EXPECT_CALL(mock_avd, getSystemImageFilePath(Avd::ImageType::ENCRYPTIONKEY))
            .WillRepeatedly(Return(system_dir / "encryption_key.img"));
    EXPECT_CALL(mock_avd, getSystemImageFilePath(Avd::ImageType::INITZIP))
            .WillRepeatedly(Return(system_dir / "data"));
    //     EXPECT_CALL(mock_avd, getSystemImageFilePath(Avd::ImageType::INITZIP))
    //             .WillRepeatedly(Return(absl::NotFoundError("")));
    EXPECT_CALL(mock_avd, getContentPath()).WillRepeatedly(Return(user_dir));

    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir / "system.img")).Times(1);
    EXPECT_CALL(mock_container, addRwDrive("encrypt", "06.0", _, _, _, _, _)).Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addRwDrive("userdata", "05.0", _, _, _, _, _)).Times(1);
    EXPECT_CALL(mock_container, addRwDrive("cache", "04.0", _, _, _, _, _)).Times(1);
#ifdef __x86_64__
    EXPECT_CALL(mock_container, addRwDrive("sdcard", "08.0", _, _, _, _, _)).Times(1);
#endif
    ASSERT_THAT(addDrives(mock_container), IsOk());
}

}  // namespace android::goldfish
