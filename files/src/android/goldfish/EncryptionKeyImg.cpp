
#include "android/goldfish/avd/Avd.h"

namespace android::goldfish {

static bool createInitalEncryptionKeyPartition(const Avd *avd) {
  // avd->getSystemImagePath(AvdImageType::ENCRYPTIONKEY);
  // avd->
  // auto partition = "disk.dataPartition.path";
  // if (!userdata_dir) {
  //     derror("no userdata_dir");
  //     return false;
  // }
  // hw->disk_encryptionKeyPartition_path = path_join(userdata_dir.get(),
  // "encryptionkey.img"); if (path_exists(hw->disk_systemPartition_initPath)) {
  //     ScopedCPtr<char>
  //     sysimg_dir(path_dirname(hw->disk_systemPartition_initPath)); if
  //     (!sysimg_dir.get()) {
  //         derror("no sysimg_dir %s", hw->disk_systemPartition_initPath);
  //         return false;
  //     }
  //     ScopedCPtr<char> init_encryptionkey_img_path(
  //         path_join(sysimg_dir.get(), "encryptionkey.img"));
  //     if (path_exists(init_encryptionkey_img_path.get())) {
  //         if (path_copy_file(hw->disk_encryptionKeyPartition_path,
  //                            init_encryptionkey_img_path.get()) >= 0) {
  //             return true;
  //         }
  //     } else {
  //         derror("no init encryptionkey.img");
  //     }
  // } else {
  //     derror("no system partition %s", hw->disk_systemPartition_initPath);
  // }
  // return false;
}
} // namespace android::goldfish