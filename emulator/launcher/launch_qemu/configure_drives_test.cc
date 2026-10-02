#include "configure_drives.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <typeinfo>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"

#include "android/base/system.h"
#include "android/base/testing/test_system.h"
#include "android/goldfish/hardware_config.h"
#include "disk_drive.h"
#include "fake_emulator.h"
#include "goldfish/file/file.h"
#include "mock_avd.h"

namespace android::goldfish {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

class MockDeviceContainer {
  public:
    MOCK_METHOD(const Avd&, avd, (), (const));
    MOCK_METHOD(const AndroidOptions&, opts, (), (const));

    MOCK_METHOD(void, addRoDrive, (std::string_view id, std::string_view pci_address));
    MOCK_METHOD(void, addWritableImgDrive, (std::string_view id, std::string_view pci_address));
    MOCK_METHOD(void, addBlankWritableDrive, (std::string_view id, std::string_view pci_address));
    MOCK_METHOD(void, addWritableDirectoryDrive,
                (std::string_view id, std::string_view pci_address));

    void addDevice(std::unique_ptr<PciDevice> disk) {
        const PciDevice& d = *disk;
        if (typeid(d) == typeid(RoDrive)) {
            addRoDrive(d.id(), d.addr());
        } else if (typeid(d) == typeid(WritableImgDrive)) {
            addWritableImgDrive(d.id(), d.addr());
        } else if (typeid(d) == typeid(BlankWritableDrive)) {
            addBlankWritableDrive(d.id(), d.addr());
        } else if (typeid(d) == typeid(WritableDirectoryDrive)) {
            addWritableDirectoryDrive(d.id(), d.addr());
        } else {
            ADD_FAILURE() << "Unknown disk drive type for " << d.id();
        }
    }
};

class ConfigureDrivesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        launcher_path_ = sys_.GetTempRoot()->Path();
        user_dir_ = launcher_path_ / "user";
        system_dir_ = launcher_path_ / "system";
        base::file::mkdir(user_dir_, 0755).IgnoreError();
        base::file::mkdir(system_dir_, 0755).IgnoreError();
        base::file::mkdir(system_dir_ / "data", 0755).IgnoreError();
        base::file::touch(system_dir_ / "data" / "empty_data_disk").IgnoreError();

        sys_.EnvSet("ANDROID_EMULATOR_HOME", launcher_path_.string());

        system_image_paths_.system_image = system_dir_ / "system.img";
        system_image_paths_.vendor_image = system_dir_ / "vendor.img";
        system_image_paths_.encryption_key_image = system_dir_ / "encryption_key.img";
        system_image_paths_.data_dir = system_dir_ / "data";
    }

    void ConfigureMockAvd(MockAvd& mock_avd) {
        EXPECT_CALL(mock_avd, Hw()).WillRepeatedly(ReturnRef(hw_));
        EXPECT_CALL(mock_avd, GetSystemImagePaths()).WillRepeatedly(ReturnRef(system_image_paths_));
        EXPECT_CALL(mock_avd, GetContentPath()).WillRepeatedly(Return(user_dir_));
    }

    base::TestSystem sys_{""};
    fs::path launcher_path_;
    fs::path user_dir_;
    fs::path system_dir_;
    HardwareConfig hw_;
    SystemImagePaths system_image_paths_;
};

}  // namespace

TEST_F(ConfigureDrivesTest, AddDrives) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("encrypt", "06.0")).Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0")).Times(1);
    EXPECT_CALL(mock_container, addBlankWritableDrive("userdata", "05.0")).Times(1);
    EXPECT_CALL(mock_container, addBlankWritableDrive("cache", "04.0")).Times(1);
#ifdef __x86_64__
    if (hw_.hw_sdCard) {
        EXPECT_CALL(mock_container, addBlankWritableDrive("sdcard", "08.0")).Times(1);
    }
#endif
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
}

TEST_F(ConfigureDrivesTest, AddDrivesWritableSystem) {
    AndroidOptions opts{.writable_system = true};
    FakeEmulator emu(std::move(opts));
    ConfigureMockAvd(emu.mock_avd());

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addWritableImgDrive("system", "03.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("encrypt", "06.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("vendor", "07.0")).Times(1);
    EXPECT_CALL(mock_container, addBlankWritableDrive("userdata", "05.0")).Times(1);
    EXPECT_CALL(mock_container, addBlankWritableDrive("cache", "04.0")).Times(1);
#ifdef __x86_64__
    if (hw_.hw_sdCard) {
        EXPECT_CALL(mock_container, addBlankWritableDrive("sdcard", "08.0")).Times(1);
    }
#endif
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
}

TEST_F(ConfigureDrivesTest, AddDrivesExistingUserdataWithMatchingSize) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());

    const fs::path userdata_path = user_dir_ / "userdata-qemu.img";
    base::file::touch(userdata_path).IgnoreError();
    std::filesystem::resize_file(userdata_path, 6ULL * 1024 * 1024 * 1024);

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("encrypt", "06.0")).Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("userdata", "05.0")).Times(1);
    EXPECT_CALL(mock_container, addBlankWritableDrive("cache", "04.0")).Times(1);
#ifdef __x86_64__
    if (hw_.hw_sdCard) {
        EXPECT_CALL(mock_container, addBlankWritableDrive("sdcard", "08.0")).Times(1);
    }
#endif
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
    EXPECT_TRUE(base::file::exists(userdata_path));
}

TEST_F(ConfigureDrivesTest, AddDrivesCustomDataSmallerThanRequestedUsedAsIs) {
    const fs::path custom_data_path = user_dir_ / "custom_userdata.img";
    const fs::path custom_data_qcow2_path = user_dir_ / "custom_userdata.img.qcow2";
    base::file::touch(custom_data_path).IgnoreError();
    base::file::touch(custom_data_qcow2_path).IgnoreError();
    std::filesystem::resize_file(custom_data_path, 1024);

    std::string custom_data_str = custom_data_path.string();
    AndroidOptions opts{.data = custom_data_str.data()};
    FakeEmulator emu(std::move(opts));
    ConfigureMockAvd(emu.mock_avd());

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("encrypt", "06.0")).Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("userdata", "05.0")).Times(1);
    EXPECT_CALL(mock_container, addBlankWritableDrive("cache", "04.0")).Times(1);
#ifdef __x86_64__
    if (hw_.hw_sdCard) {
        EXPECT_CALL(mock_container, addBlankWritableDrive("sdcard", "08.0")).Times(1);
    }
#endif
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
    EXPECT_TRUE(base::file::exists(custom_data_path));
    EXPECT_TRUE(base::file::exists(custom_data_qcow2_path));
}

TEST_F(ConfigureDrivesTest, AddDrivesUserdataFromDirectory) {
    FakeEmulator emu;
    hw_.hw_sensor_hinge = true;
    ConfigureMockAvd(emu.mock_avd());

    base::file::mkdir_recursive(system_dir_ / "data" / "misc" / "pixel_9_pro_fold", 0755)
            .IgnoreError();

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("encrypt", "06.0")).Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0")).Times(1);
    EXPECT_CALL(mock_container, addWritableDirectoryDrive("userdata", "05.0")).Times(1);
    EXPECT_CALL(mock_container, addBlankWritableDrive("cache", "04.0")).Times(1);
#ifdef __x86_64__
    if (hw_.hw_sdCard) {
        EXPECT_CALL(mock_container, addBlankWritableDrive("sdcard", "08.0")).Times(1);
    }
#endif
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
}

}  // namespace android::goldfish
