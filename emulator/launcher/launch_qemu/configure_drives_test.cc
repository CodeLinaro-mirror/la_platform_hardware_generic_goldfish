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
#include "goldfish/file/storage_capacity.h"
#include "goldfish/fsimage/exfat.h"
#include "goldfish/fsimage/qcow2.h"
#include "mock_avd.h"

namespace android::goldfish {

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using base::operator""_MiB;
using ::testing::_;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Return;
using ::testing::ReturnRef;

class MockDeviceContainer {
  public:
    MOCK_METHOD(const Avd&, avd, (), (const));
    MOCK_METHOD(const AndroidOptions&, opts, (), (const));

    MOCK_METHOD(void, addNullDrive, (std::string_view id, std::string_view pci_address));
    MOCK_METHOD(void, addRoDrive,
                (std::string_view id, std::string_view pci_address, const fs::path& image_path));
    MOCK_METHOD(void, addWritableImgDrive,
                (std::string_view id, std::string_view pci_address, const fs::path& backing_image,
                 const fs::path& qcow2_image));
    MOCK_METHOD(void, addBlankWritableDrive,
                (std::string_view id, std::string_view pci_address, const fs::path& backing_image,
                 const fs::path& qcow2_image));
    MOCK_METHOD(void, addWritableDirectoryDrive,
                (std::string_view id, std::string_view pci_address, const fs::path& backing_image,
                 const fs::path& qcow2_image, const fs::path& src_directory));

    void addDevice(std::unique_ptr<PciDevice> disk) {
        const PciDevice& d = *disk;
        if (typeid(d) == typeid(NullDrive)) {
            const auto& null_drive = static_cast<const NullDrive&>(d);
            addNullDrive(null_drive.id(), null_drive.addr());
        } else if (typeid(d) == typeid(RoDrive)) {
            const auto& ro = static_cast<const RoDrive&>(d);
            addRoDrive(ro.id(), ro.addr(), ro.image_path());
        } else if (typeid(d) == typeid(WritableImgDrive)) {
            const auto& rw = static_cast<const WritableImgDrive&>(d);
            addWritableImgDrive(rw.id(), rw.addr(), rw.backing_image(), rw.qcow2_image());
        } else if (typeid(d) == typeid(BlankWritableDrive)) {
            const auto& blank = static_cast<const BlankWritableDrive&>(d);
            addBlankWritableDrive(blank.id(), blank.addr(), blank.backing_image(),
                                  blank.qcow2_image());
        } else if (typeid(d) == typeid(WritableDirectoryDrive)) {
            const auto& dir = static_cast<const WritableDirectoryDrive&>(d);
            addWritableDirectoryDrive(dir.id(), dir.addr(), dir.backing_image(), dir.qcow2_image(),
                                      dir.src_directory());
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
    EXPECT_CALL(mock_container, addNullDrive("sdcard", "08.0")).Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir_ / "system.img")).Times(1);
    EXPECT_CALL(mock_container,
                addBlankWritableDrive("userdata", "05.0", user_dir_ / "userdata.img",
                                      user_dir_ / "userdata.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir_ / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);
    EXPECT_FALSE(hw_.hw_sdCard);
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
}

TEST_F(ConfigureDrivesTest, AddDrivesWritableSystem) {
    AndroidOptions opts{.writable_system = true};
    FakeEmulator emu(std::move(opts));
    ConfigureMockAvd(emu.mock_avd());

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addNullDrive("sdcard", "08.0")).Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("system", "03.0", system_dir_ / "system.img",
                                                    user_dir_ / "system.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container,
                addBlankWritableDrive("userdata", "05.0", user_dir_ / "userdata.img",
                                      user_dir_ / "userdata.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("vendor", "07.0", system_dir_ / "vendor.img",
                                                    user_dir_ / "vendor.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);
    EXPECT_FALSE(hw_.hw_sdCard);
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
}

TEST_F(ConfigureDrivesTest, AddDrivesExistingUserdataWithMatchingSize) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());

    const fs::path userdata_path = user_dir_ / "userdata.img";
    base::file::touch(userdata_path).IgnoreError();
    std::filesystem::resize_file(userdata_path, 6ULL * 1024 * 1024 * 1024);

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addNullDrive("sdcard", "08.0")).Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir_ / "system.img")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("userdata", "05.0", userdata_path,
                                                    user_dir_ / "userdata.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir_ / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);
    EXPECT_FALSE(hw_.hw_sdCard);
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
    EXPECT_TRUE(base::file::exists(userdata_path));
}

TEST_F(ConfigureDrivesTest, AddDrivesExistingOldUserdataWithMatchingSize) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());

    const fs::path userdata_path = user_dir_ / "userdata-qemu.img";
    base::file::touch(userdata_path).IgnoreError();
    std::filesystem::resize_file(userdata_path, 6ULL * 1024 * 1024 * 1024);

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addNullDrive("sdcard", "08.0")).Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir_ / "system.img")).Times(1);
    EXPECT_CALL(mock_container, addWritableImgDrive("userdata", "05.0", userdata_path,
                                                    user_dir_ / "userdata-qemu.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir_ / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);
    EXPECT_FALSE(hw_.hw_sdCard);
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
    EXPECT_TRUE(base::file::exists(userdata_path));
}

TEST_F(ConfigureDrivesTest, AddDrivesUserdataFromDirectory) {
    FakeEmulator emu;
    hw_.hw_sensor_hinge = true;
    ConfigureMockAvd(emu.mock_avd());

    base::file::mkdir_recursive(system_dir_ / "data" / "misc" / "pixel_9_pro_fold", 0755)
            .IgnoreError();

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addNullDrive("sdcard", "08.0")).Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir_ / "system.img")).Times(1);
    EXPECT_CALL(mock_container,
                addWritableDirectoryDrive("userdata", "05.0", user_dir_ / "userdata.img",
                                          user_dir_ / "userdata.img.qcow2", user_dir_ / "data"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir_ / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);
    EXPECT_FALSE(hw_.hw_sdCard);
    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
}

TEST_F(ConfigureDrivesTest, AddDrivesSdCardCreatesEmuImageWhenMissing) {
    FakeEmulator emu;
    hw_.hw_sdCard = true;
    hw_.sdcard_size = 64_MiB;
    ConfigureMockAvd(emu.mock_avd());

    const fs::path sdcard_emu_path = user_dir_ / "sdcard.emu.img";

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addWritableImgDrive("sdcard", "08.0", sdcard_emu_path,
                                                    user_dir_ / "sdcard.emu.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir_ / "system.img")).Times(1);
    EXPECT_CALL(mock_container,
                addBlankWritableDrive("userdata", "05.0", user_dir_ / "userdata.img",
                                      user_dir_ / "userdata.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir_ / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);

    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
    EXPECT_TRUE(base::file::is_file(sdcard_emu_path));
    EXPECT_EQ(std::filesystem::file_size(sdcard_emu_path), (64_MiB).Bytes());
    EXPECT_TRUE(::goldfish::fsimage::IsExFatFileImage(sdcard_emu_path));
}

TEST_F(ConfigureDrivesTest, AddDrivesSdCardUsesExistingEmuImage) {
    FakeEmulator emu;
    hw_.hw_sdCard = true;
    ConfigureMockAvd(emu.mock_avd());

    const fs::path sdcard_emu_path = user_dir_ / "sdcard.emu.img";
    base::file::touch(sdcard_emu_path).IgnoreError();
    std::filesystem::resize_file(sdcard_emu_path, 1024);

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addWritableImgDrive("sdcard", "08.0", sdcard_emu_path,
                                                    user_dir_ / "sdcard.emu.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir_ / "system.img")).Times(1);
    EXPECT_CALL(mock_container,
                addBlankWritableDrive("userdata", "05.0", user_dir_ / "userdata.img",
                                      user_dir_ / "userdata.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir_ / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);

    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
    EXPECT_EQ(std::filesystem::file_size(sdcard_emu_path), 1024U);
}

TEST_F(ConfigureDrivesTest, AddDrivesSdCardPrefersExistingSdcardImgOverEmuImage) {
    FakeEmulator emu;
    hw_.hw_sdCard = true;
    ConfigureMockAvd(emu.mock_avd());

    const fs::path sdcard_path = user_dir_ / "sdcard.img";
    base::file::touch(sdcard_path).IgnoreError();

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container,
                addWritableImgDrive("sdcard", "08.0", sdcard_path, user_dir_ / "sdcard.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir_ / "system.img")).Times(1);
    EXPECT_CALL(mock_container,
                addBlankWritableDrive("userdata", "05.0", user_dir_ / "userdata.img",
                                      user_dir_ / "userdata.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir_ / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);

    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
    EXPECT_FALSE(base::file::exists(user_dir_ / "sdcard.emu.img"));
}

TEST_F(ConfigureDrivesTest, AddDrivesSdCardExternalPath) {
    const fs::path external_sdcard_path = launcher_path_ / "external_sdcard.img";
    base::file::touch(external_sdcard_path).IgnoreError();

    FakeEmulator emu;
    hw_.hw_sdCard = true;
    hw_.sdcard_path = external_sdcard_path.string();
    ConfigureMockAvd(emu.mock_avd());

    MockDeviceContainer mock_container;
    EXPECT_CALL(mock_container, addWritableImgDrive("sdcard", "08.0", external_sdcard_path,
                                                    user_dir_ / "sdcard.external.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container,
                addWritableImgDrive("encrypt", "06.0", system_dir_ / "encryption_key.img",
                                    user_dir_ / "encryptionkey.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("system", "03.0", system_dir_ / "system.img")).Times(1);
    EXPECT_CALL(mock_container,
                addBlankWritableDrive("userdata", "05.0", user_dir_ / "userdata.img",
                                      user_dir_ / "userdata.img.qcow2"))
            .Times(1);
    EXPECT_CALL(mock_container, addRoDrive("vendor", "07.0", system_dir_ / "vendor.img")).Times(1);
    EXPECT_CALL(mock_container, addNullDrive("cache", "04.0")).Times(1);

    ASSERT_THAT(addDrives(emu.config(), mock_container), IsOk());
}

TEST_F(ConfigureDrivesTest, AddDrivesSdCardExternalPathMissingReturnsNotFound) {
    FakeEmulator emu;
    hw_.hw_sdCard = true;
    hw_.sdcard_path = (launcher_path_ / "nonexistent_sdcard.img").string();
    ConfigureMockAvd(emu.mock_avd());

    MockDeviceContainer mock_container;
    EXPECT_THAT(addDrives(emu.config(), mock_container), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(ConfigureDrivesTest, UserdataIsNewOnFirstBoot) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());

    EXPECT_THAT(IsUserdataNew(emu.config().avd()), IsTrue());
}

TEST_F(ConfigureDrivesTest, UserdataIsNotNewWithValidQcow2Overlay) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());
    ASSERT_THAT(base::file::touch(user_dir_ / "userdata.img"), IsOk());
    ASSERT_THAT(::goldfish::fsimage::CreateQcow2Overlay(user_dir_ / "userdata.img.qcow2",
                                                        user_dir_ / "userdata.img"),
                IsOk());

    EXPECT_THAT(IsUserdataNew(emu.config().avd()), IsFalse());
}

// Android Studio's Wipe Data deletes the qcow2 overlay but keeps userdata.img.
TEST_F(ConfigureDrivesTest, UserdataIsNewWithoutQcow2Overlay) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());
    ASSERT_THAT(base::file::touch(user_dir_ / "userdata.img"), IsOk());

    EXPECT_THAT(IsUserdataNew(emu.config().avd()), IsTrue());
}

// The userdata drive replaces an invalid qcow2 overlay with a new one.
TEST_F(ConfigureDrivesTest, UserdataIsNewWithInvalidQcow2Overlay) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());
    ASSERT_THAT(base::file::touch(user_dir_ / "userdata.img"), IsOk());
    ASSERT_THAT(base::file::touch(user_dir_ / "userdata.img.qcow2"), IsOk());

    EXPECT_THAT(IsUserdataNew(emu.config().avd()), IsTrue());
}

// The qcow2 overlay of a shallow copy has the image of another AVD as its backing image.
TEST_F(ConfigureDrivesTest, UserdataIsNotNewWithValidQcow2OverlayOfAnotherImage) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());
    ASSERT_THAT(base::file::touch(launcher_path_ / "other.img"), IsOk());
    ASSERT_THAT(::goldfish::fsimage::CreateQcow2Overlay(user_dir_ / "userdata.img.qcow2",
                                                        launcher_path_ / "other.img"),
                IsOk());

    EXPECT_THAT(IsUserdataNew(emu.config().avd()), IsFalse());
}

TEST_F(ConfigureDrivesTest, OldUserdataImageIsNotNewWithValidQcow2Overlay) {
    FakeEmulator emu;
    ConfigureMockAvd(emu.mock_avd());
    ASSERT_THAT(base::file::touch(user_dir_ / "userdata-qemu.img"), IsOk());
    ASSERT_THAT(::goldfish::fsimage::CreateQcow2Overlay(user_dir_ / "userdata-qemu.img.qcow2",
                                                        user_dir_ / "userdata-qemu.img"),
                IsOk());

    EXPECT_THAT(IsUserdataNew(emu.config().avd()), IsFalse());
}

}  // namespace android::goldfish
