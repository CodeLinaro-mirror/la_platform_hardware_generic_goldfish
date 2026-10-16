// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "goldfish/fsimage/exfat.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include "android/status/status_macros.h"
#include "goldfish/file/file.h"

namespace goldfish::fsimage {
namespace {

using android::base::StorageCapacity;

constexpr uint32_t kSectorSizeBits = 9U;
constexpr uint64_t kSectorSize = 1ULL << kSectorSizeBits;  // 512 bytes
constexpr uint64_t kBootRegionSectors = 12ULL;
constexpr uint64_t kTotalBootSectors = kBootRegionSectors * 2ULL;  // Main + Backup (24 sectors)
constexpr uint32_t kReservedClusters = 2U;
constexpr uint32_t kFirstDataCluster = 2U;
constexpr uint64_t kMaxExfatClusters = 0xFFFFFFF5ULL;

constexpr uint16_t kBootSignature = 0xAA55U;
constexpr uint32_t kExBootSignature = 0xAA550000U;
constexpr uint16_t kExfatRevision100 = 0x0100U;
constexpr uint32_t kDefaultVolumeSerial = 0x12345678U;
constexpr uint8_t kDriveSelect = 0x80U;

constexpr uint32_t kFatMediaDescriptor = 0xFFFFFFF8U;
constexpr uint32_t kFatEofCluster = 0xFFFFFFFFU;

constexpr uint8_t kDentryTypeVolumeLabel = 0x83U;
constexpr uint8_t kDentryTypeBitmap = 0x81U;
constexpr uint8_t kDentryTypeUpcase = 0x82U;
constexpr uint8_t kDentryTypeFile = 0x85U;
constexpr uint8_t kDentryTypeStreamExt = 0xC0U;
constexpr uint8_t kDentryTypeFileName = 0xC1U;

constexpr uint8_t kFlagAllocationPossible = 0x01U;
constexpr uint16_t kAttrDirectory = 0x0010U;
constexpr uint16_t kAttrArchive = 0x0020U;
constexpr uint32_t kDefaultDosTimestamp = (1U << 21U) | (1U << 16U);  // 1980-01-01 00:00:00

constexpr size_t kMaxVolumeLabelLength = 11U;
constexpr size_t kMaxFileNameLength = 255U;
constexpr size_t kCharsPerFileNameDentry = 15U;
constexpr uint64_t kMaxDirectorySizeBytes = 256ULL * 1024ULL * 1024ULL;

constexpr uint16_t HostToLe16(uint16_t v) {
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(v);
    }
    return v;
}

constexpr uint32_t HostToLe32(uint32_t v) {
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(v);
    }
    return v;
}

constexpr uint64_t HostToLe64(uint64_t v) {
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(v);
    }
    return v;
}

#pragma pack(push, 1)

struct ExfatBootSector {
    uint8_t jmp_boot[3];
    uint8_t fs_name[8];
    uint8_t must_be_zero[53];
    uint64_t partition_offset;
    uint64_t vol_length;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t clu_offset;
    uint32_t clu_count;
    uint32_t root_cluster;
    uint32_t vol_serial;
    uint16_t fs_revision;
    uint16_t vol_flags;
    uint8_t sect_size_bits;
    uint8_t sect_per_clus_bits;
    uint8_t num_fats;
    uint8_t drv_sel;
    uint8_t percent_in_use;
    uint8_t reserved[7];
    uint8_t boot_code[390];
    uint16_t signature;
};
static_assert(sizeof(ExfatBootSector) == kSectorSize);

struct ExfatVolumeLabelDentry {
    uint8_t type;
    uint8_t char_count;
    uint16_t volume_label[11];
    uint8_t reserved[8];
};
static_assert(sizeof(ExfatVolumeLabelDentry) == 32);

struct ExfatBitmapDentry {
    uint8_t type;
    uint8_t flags;
    uint8_t reserved[18];
    uint32_t start_clu;
    uint64_t size;
};
static_assert(sizeof(ExfatBitmapDentry) == 32);

struct ExfatUpcaseDentry {
    uint8_t type;
    uint8_t reserved1[3];
    uint32_t checksum;
    uint8_t reserved2[12];
    uint32_t start_clu;
    uint64_t size;
};
static_assert(sizeof(ExfatUpcaseDentry) == 32);

struct ExfatFileDentry {
    uint8_t type;
    uint8_t secondary_count;
    uint16_t set_checksum;
    uint16_t file_attributes;
    uint16_t reserved1;
    uint32_t create_timestamp;
    uint32_t last_modified_timestamp;
    uint32_t last_accessed_timestamp;
    uint8_t create_10ms_increment;
    uint8_t last_modified_10ms_increment;
    uint8_t create_utc_offset;
    uint8_t last_modified_utc_offset;
    uint8_t last_accessed_utc_offset;
    uint8_t reserved2[7];
};
static_assert(sizeof(ExfatFileDentry) == 32);

struct ExfatStreamExtDentry {
    uint8_t type;
    uint8_t general_secondary_flags;
    uint8_t reserved1;
    uint8_t name_length;
    uint16_t name_hash;
    uint16_t reserved2;
    uint64_t valid_data_length;
    uint32_t reserved3;
    uint32_t first_cluster;
    uint64_t data_length;
};
static_assert(sizeof(ExfatStreamExtDentry) == 32);

struct ExfatFileNameDentry {
    uint8_t type;
    uint8_t general_secondary_flags;
    uint16_t file_name[kCharsPerFileNameDentry];
};
static_assert(sizeof(ExfatFileNameDentry) == 32);

union ExfatDentry {
    ExfatVolumeLabelDentry volume_label;
    ExfatBitmapDentry bitmap;
    ExfatUpcaseDentry upcase;
    ExfatFileDentry file;
    ExfatStreamExtDentry stream_ext;
    ExfatFileNameDentry file_name;
};
static_assert(sizeof(ExfatDentry) == 32);

#pragma pack(pop)

// Recommended compressed Up-case Table from the exFAT specification (Section 7.2.5.1).
constexpr std::array<uint16_t, 2918> kDefaultUpcaseTable = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a, 0x000b,
    0x000c, 0x000d, 0x000e, 0x000f, 0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f, 0x0020, 0x0021, 0x0022, 0x0023,
    0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b,
    0x003c, 0x003d, 0x003e, 0x003f, 0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f, 0x0050, 0x0051, 0x0052, 0x0053,
    0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x0060, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b,
    0x004c, 0x004d, 0x004e, 0x004f, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005a, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f, 0x0080, 0x0081, 0x0082, 0x0083,
    0x0084, 0x0085, 0x0086, 0x0087, 0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097, 0x0098, 0x0099, 0x009a, 0x009b,
    0x009c, 0x009d, 0x009e, 0x009f, 0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
    0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af, 0x00b0, 0x00b1, 0x00b2, 0x00b3,
    0x00b4, 0x00b5, 0x00b6, 0x00b7, 0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
    0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7, 0x00c8, 0x00c9, 0x00ca, 0x00cb,
    0x00cc, 0x00cd, 0x00ce, 0x00cf, 0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
    0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df, 0x00c0, 0x00c1, 0x00c2, 0x00c3,
    0x00c4, 0x00c5, 0x00c6, 0x00c7, 0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00f7, 0x00d8, 0x00d9, 0x00da, 0x00db,
    0x00dc, 0x00dd, 0x00de, 0x0178, 0x0100, 0x0100, 0x0102, 0x0102, 0x0104, 0x0104, 0x0106, 0x0106,
    0x0108, 0x0108, 0x010a, 0x010a, 0x010c, 0x010c, 0x010e, 0x010e, 0x0110, 0x0110, 0x0112, 0x0112,
    0x0114, 0x0114, 0x0116, 0x0116, 0x0118, 0x0118, 0x011a, 0x011a, 0x011c, 0x011c, 0x011e, 0x011e,
    0x0120, 0x0120, 0x0122, 0x0122, 0x0124, 0x0124, 0x0126, 0x0126, 0x0128, 0x0128, 0x012a, 0x012a,
    0x012c, 0x012c, 0x012e, 0x012e, 0x0130, 0x0131, 0x0132, 0x0132, 0x0134, 0x0134, 0x0136, 0x0136,
    0x0138, 0x0139, 0x0139, 0x013b, 0x013b, 0x013d, 0x013d, 0x013f, 0x013f, 0x0141, 0x0141, 0x0143,
    0x0143, 0x0145, 0x0145, 0x0147, 0x0147, 0x0149, 0x014a, 0x014a, 0x014c, 0x014c, 0x014e, 0x014e,
    0x0150, 0x0150, 0x0152, 0x0152, 0x0154, 0x0154, 0x0156, 0x0156, 0x0158, 0x0158, 0x015a, 0x015a,
    0x015c, 0x015c, 0x015e, 0x015e, 0x0160, 0x0160, 0x0162, 0x0162, 0x0164, 0x0164, 0x0166, 0x0166,
    0x0168, 0x0168, 0x016a, 0x016a, 0x016c, 0x016c, 0x016e, 0x016e, 0x0170, 0x0170, 0x0172, 0x0172,
    0x0174, 0x0174, 0x0176, 0x0176, 0x0178, 0x0179, 0x0179, 0x017b, 0x017b, 0x017d, 0x017d, 0x017f,
    0x0243, 0x0181, 0x0182, 0x0182, 0x0184, 0x0184, 0x0186, 0x0187, 0x0187, 0x0189, 0x018a, 0x018b,
    0x018b, 0x018d, 0x018e, 0x018f, 0x0190, 0x0191, 0x0191, 0x0193, 0x0194, 0x01f6, 0x0196, 0x0197,
    0x0198, 0x0198, 0x023d, 0x019b, 0x019c, 0x019d, 0x0220, 0x019f, 0x01a0, 0x01a0, 0x01a2, 0x01a2,
    0x01a4, 0x01a4, 0x01a6, 0x01a7, 0x01a7, 0x01a9, 0x01aa, 0x01ab, 0x01ac, 0x01ac, 0x01ae, 0x01af,
    0x01af, 0x01b1, 0x01b2, 0x01b3, 0x01b3, 0x01b5, 0x01b5, 0x01b7, 0x01b8, 0x01b8, 0x01ba, 0x01bb,
    0x01bc, 0x01bc, 0x01be, 0x01f7, 0x01c0, 0x01c1, 0x01c2, 0x01c3, 0x01c4, 0x01c5, 0x01c4, 0x01c7,
    0x01c8, 0x01c7, 0x01ca, 0x01cb, 0x01ca, 0x01cd, 0x01cd, 0x01cf, 0x01cf, 0x01d1, 0x01d1, 0x01d3,
    0x01d3, 0x01d5, 0x01d5, 0x01d7, 0x01d7, 0x01d9, 0x01d9, 0x01db, 0x01db, 0x018e, 0x01de, 0x01de,
    0x01e0, 0x01e0, 0x01e2, 0x01e2, 0x01e4, 0x01e4, 0x01e6, 0x01e6, 0x01e8, 0x01e8, 0x01ea, 0x01ea,
    0x01ec, 0x01ec, 0x01ee, 0x01ee, 0x01f0, 0x01f1, 0x01f2, 0x01f1, 0x01f4, 0x01f4, 0x01f6, 0x01f7,
    0x01f8, 0x01f8, 0x01fa, 0x01fa, 0x01fc, 0x01fc, 0x01fe, 0x01fe, 0x0200, 0x0200, 0x0202, 0x0202,
    0x0204, 0x0204, 0x0206, 0x0206, 0x0208, 0x0208, 0x020a, 0x020a, 0x020c, 0x020c, 0x020e, 0x020e,
    0x0210, 0x0210, 0x0212, 0x0212, 0x0214, 0x0214, 0x0216, 0x0216, 0x0218, 0x0218, 0x021a, 0x021a,
    0x021c, 0x021c, 0x021e, 0x021e, 0x0220, 0x0221, 0x0222, 0x0222, 0x0224, 0x0224, 0x0226, 0x0226,
    0x0228, 0x0228, 0x022a, 0x022a, 0x022c, 0x022c, 0x022e, 0x022e, 0x0230, 0x0230, 0x0232, 0x0232,
    0x0234, 0x0235, 0x0236, 0x0237, 0x0238, 0x0239, 0x2c65, 0x023b, 0x023b, 0x023d, 0x2c66, 0x023f,
    0x0240, 0x0241, 0x0241, 0x0243, 0x0244, 0x0245, 0x0246, 0x0246, 0x0248, 0x0248, 0x024a, 0x024a,
    0x024c, 0x024c, 0x024e, 0x024e, 0x0250, 0x0251, 0x0252, 0x0181, 0x0186, 0x0255, 0x0189, 0x018a,
    0x0258, 0x018f, 0x025a, 0x0190, 0x025c, 0x025d, 0x025e, 0x025f, 0x0193, 0x0261, 0x0262, 0x0194,
    0x0264, 0x0265, 0x0266, 0x0267, 0x0197, 0x0196, 0x026a, 0x2c62, 0x026c, 0x026d, 0x026e, 0x019c,
    0x0270, 0x0271, 0x019d, 0x0273, 0x0274, 0x019f, 0x0276, 0x0277, 0x0278, 0x0279, 0x027a, 0x027b,
    0x027c, 0x2c64, 0x027e, 0x027f, 0x01a6, 0x0281, 0x0282, 0x01a9, 0x0284, 0x0285, 0x0286, 0x0287,
    0x01ae, 0x0244, 0x01b1, 0x01b2, 0x0245, 0x028d, 0x028e, 0x028f, 0x0290, 0x0291, 0x01b7, 0x0293,
    0x0294, 0x0295, 0x0296, 0x0297, 0x0298, 0x0299, 0x029a, 0x029b, 0x029c, 0x029d, 0x029e, 0x029f,
    0x02a0, 0x02a1, 0x02a2, 0x02a3, 0x02a4, 0x02a5, 0x02a6, 0x02a7, 0x02a8, 0x02a9, 0x02aa, 0x02ab,
    0x02ac, 0x02ad, 0x02ae, 0x02af, 0x02b0, 0x02b1, 0x02b2, 0x02b3, 0x02b4, 0x02b5, 0x02b6, 0x02b7,
    0x02b8, 0x02b9, 0x02ba, 0x02bb, 0x02bc, 0x02bd, 0x02be, 0x02bf, 0x02c0, 0x02c1, 0x02c2, 0x02c3,
    0x02c4, 0x02c5, 0x02c6, 0x02c7, 0x02c8, 0x02c9, 0x02ca, 0x02cb, 0x02cc, 0x02cd, 0x02ce, 0x02cf,
    0x02d0, 0x02d1, 0x02d2, 0x02d3, 0x02d4, 0x02d5, 0x02d6, 0x02d7, 0x02d8, 0x02d9, 0x02da, 0x02db,
    0x02dc, 0x02dd, 0x02de, 0x02df, 0x02e0, 0x02e1, 0x02e2, 0x02e3, 0x02e4, 0x02e5, 0x02e6, 0x02e7,
    0x02e8, 0x02e9, 0x02ea, 0x02eb, 0x02ec, 0x02ed, 0x02ee, 0x02ef, 0x02f0, 0x02f1, 0x02f2, 0x02f3,
    0x02f4, 0x02f5, 0x02f6, 0x02f7, 0x02f8, 0x02f9, 0x02fa, 0x02fb, 0x02fc, 0x02fd, 0x02fe, 0x02ff,
    0x0300, 0x0301, 0x0302, 0x0303, 0x0304, 0x0305, 0x0306, 0x0307, 0x0308, 0x0309, 0x030a, 0x030b,
    0x030c, 0x030d, 0x030e, 0x030f, 0x0310, 0x0311, 0x0312, 0x0313, 0x0314, 0x0315, 0x0316, 0x0317,
    0x0318, 0x0319, 0x031a, 0x031b, 0x031c, 0x031d, 0x031e, 0x031f, 0x0320, 0x0321, 0x0322, 0x0323,
    0x0324, 0x0325, 0x0326, 0x0327, 0x0328, 0x0329, 0x032a, 0x032b, 0x032c, 0x032d, 0x032e, 0x032f,
    0x0330, 0x0331, 0x0332, 0x0333, 0x0334, 0x0335, 0x0336, 0x0337, 0x0338, 0x0339, 0x033a, 0x033b,
    0x033c, 0x033d, 0x033e, 0x033f, 0x0340, 0x0341, 0x0342, 0x0343, 0x0344, 0x0345, 0x0346, 0x0347,
    0x0348, 0x0349, 0x034a, 0x034b, 0x034c, 0x034d, 0x034e, 0x034f, 0x0350, 0x0351, 0x0352, 0x0353,
    0x0354, 0x0355, 0x0356, 0x0357, 0x0358, 0x0359, 0x035a, 0x035b, 0x035c, 0x035d, 0x035e, 0x035f,
    0x0360, 0x0361, 0x0362, 0x0363, 0x0364, 0x0365, 0x0366, 0x0367, 0x0368, 0x0369, 0x036a, 0x036b,
    0x036c, 0x036d, 0x036e, 0x036f, 0x0370, 0x0371, 0x0372, 0x0373, 0x0374, 0x0375, 0x0376, 0x0377,
    0x0378, 0x0379, 0x037a, 0x03fd, 0x03fe, 0x03ff, 0x037e, 0x037f, 0x0380, 0x0381, 0x0382, 0x0383,
    0x0384, 0x0385, 0x0386, 0x0387, 0x0388, 0x0389, 0x038a, 0x038b, 0x038c, 0x038d, 0x038e, 0x038f,
    0x0390, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397, 0x0398, 0x0399, 0x039a, 0x039b,
    0x039c, 0x039d, 0x039e, 0x039f, 0x03a0, 0x03a1, 0x03a2, 0x03a3, 0x03a4, 0x03a5, 0x03a6, 0x03a7,
    0x03a8, 0x03a9, 0x03aa, 0x03ab, 0x0386, 0x0388, 0x0389, 0x038a, 0x03b0, 0x0391, 0x0392, 0x0393,
    0x0394, 0x0395, 0x0396, 0x0397, 0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x039e, 0x039f,
    0x03a0, 0x03a1, 0x03a3, 0x03a3, 0x03a4, 0x03a5, 0x03a6, 0x03a7, 0x03a8, 0x03a9, 0x03aa, 0x03ab,
    0x038c, 0x038e, 0x038f, 0x03cf, 0x03d0, 0x03d1, 0x03d2, 0x03d3, 0x03d4, 0x03d5, 0x03d6, 0x03d7,
    0x03d8, 0x03d8, 0x03da, 0x03da, 0x03dc, 0x03dc, 0x03de, 0x03de, 0x03e0, 0x03e0, 0x03e2, 0x03e2,
    0x03e4, 0x03e4, 0x03e6, 0x03e6, 0x03e8, 0x03e8, 0x03ea, 0x03ea, 0x03ec, 0x03ec, 0x03ee, 0x03ee,
    0x03f0, 0x03f1, 0x03f9, 0x03f3, 0x03f4, 0x03f5, 0x03f6, 0x03f7, 0x03f7, 0x03f9, 0x03fa, 0x03fa,
    0x03fc, 0x03fd, 0x03fe, 0x03ff, 0x0400, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405, 0x0406, 0x0407,
    0x0408, 0x0409, 0x040a, 0x040b, 0x040c, 0x040d, 0x040e, 0x040f, 0x0410, 0x0411, 0x0412, 0x0413,
    0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042a, 0x042b,
    0x042c, 0x042d, 0x042e, 0x042f, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f, 0x0420, 0x0421, 0x0422, 0x0423,
    0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042a, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f,
    0x0400, 0x0401, 0x0402, 0x0403, 0x0404, 0x0405, 0x0406, 0x0407, 0x0408, 0x0409, 0x040a, 0x040b,
    0x040c, 0x040d, 0x040e, 0x040f, 0x0460, 0x0460, 0x0462, 0x0462, 0x0464, 0x0464, 0x0466, 0x0466,
    0x0468, 0x0468, 0x046a, 0x046a, 0x046c, 0x046c, 0x046e, 0x046e, 0x0470, 0x0470, 0x0472, 0x0472,
    0x0474, 0x0474, 0x0476, 0x0476, 0x0478, 0x0478, 0x047a, 0x047a, 0x047c, 0x047c, 0x047e, 0x047e,
    0x0480, 0x0480, 0x0482, 0x0483, 0x0484, 0x0485, 0x0486, 0x0487, 0x0488, 0x0489, 0x048a, 0x048a,
    0x048c, 0x048c, 0x048e, 0x048e, 0x0490, 0x0490, 0x0492, 0x0492, 0x0494, 0x0494, 0x0496, 0x0496,
    0x0498, 0x0498, 0x049a, 0x049a, 0x049c, 0x049c, 0x049e, 0x049e, 0x04a0, 0x04a0, 0x04a2, 0x04a2,
    0x04a4, 0x04a4, 0x04a6, 0x04a6, 0x04a8, 0x04a8, 0x04aa, 0x04aa, 0x04ac, 0x04ac, 0x04ae, 0x04ae,
    0x04b0, 0x04b0, 0x04b2, 0x04b2, 0x04b4, 0x04b4, 0x04b6, 0x04b6, 0x04b8, 0x04b8, 0x04ba, 0x04ba,
    0x04bc, 0x04bc, 0x04be, 0x04be, 0x04c0, 0x04c1, 0x04c1, 0x04c3, 0x04c3, 0x04c5, 0x04c5, 0x04c7,
    0x04c7, 0x04c9, 0x04c9, 0x04cb, 0x04cb, 0x04cd, 0x04cd, 0x04c0, 0x04d0, 0x04d0, 0x04d2, 0x04d2,
    0x04d4, 0x04d4, 0x04d6, 0x04d6, 0x04d8, 0x04d8, 0x04da, 0x04da, 0x04dc, 0x04dc, 0x04de, 0x04de,
    0x04e0, 0x04e0, 0x04e2, 0x04e2, 0x04e4, 0x04e4, 0x04e6, 0x04e6, 0x04e8, 0x04e8, 0x04ea, 0x04ea,
    0x04ec, 0x04ec, 0x04ee, 0x04ee, 0x04f0, 0x04f0, 0x04f2, 0x04f2, 0x04f4, 0x04f4, 0x04f6, 0x04f6,
    0x04f8, 0x04f8, 0x04fa, 0x04fa, 0x04fc, 0x04fc, 0x04fe, 0x04fe, 0x0500, 0x0500, 0x0502, 0x0502,
    0x0504, 0x0504, 0x0506, 0x0506, 0x0508, 0x0508, 0x050a, 0x050a, 0x050c, 0x050c, 0x050e, 0x050e,
    0x0510, 0x0510, 0x0512, 0x0512, 0x0514, 0x0515, 0x0516, 0x0517, 0x0518, 0x0519, 0x051a, 0x051b,
    0x051c, 0x051d, 0x051e, 0x051f, 0x0520, 0x0521, 0x0522, 0x0523, 0x0524, 0x0525, 0x0526, 0x0527,
    0x0528, 0x0529, 0x052a, 0x052b, 0x052c, 0x052d, 0x052e, 0x052f, 0x0530, 0x0531, 0x0532, 0x0533,
    0x0534, 0x0535, 0x0536, 0x0537, 0x0538, 0x0539, 0x053a, 0x053b, 0x053c, 0x053d, 0x053e, 0x053f,
    0x0540, 0x0541, 0x0542, 0x0543, 0x0544, 0x0545, 0x0546, 0x0547, 0x0548, 0x0549, 0x054a, 0x054b,
    0x054c, 0x054d, 0x054e, 0x054f, 0x0550, 0x0551, 0x0552, 0x0553, 0x0554, 0x0555, 0x0556, 0x0557,
    0x0558, 0x0559, 0x055a, 0x055b, 0x055c, 0x055d, 0x055e, 0x055f, 0x0560, 0x0531, 0x0532, 0x0533,
    0x0534, 0x0535, 0x0536, 0x0537, 0x0538, 0x0539, 0x053a, 0x053b, 0x053c, 0x053d, 0x053e, 0x053f,
    0x0540, 0x0541, 0x0542, 0x0543, 0x0544, 0x0545, 0x0546, 0x0547, 0x0548, 0x0549, 0x054a, 0x054b,
    0x054c, 0x054d, 0x054e, 0x054f, 0x0550, 0x0551, 0x0552, 0x0553, 0x0554, 0x0555, 0x0556, 0xffff,
    0x17f6, 0x2c63, 0x1d7e, 0x1d7f, 0x1d80, 0x1d81, 0x1d82, 0x1d83, 0x1d84, 0x1d85, 0x1d86, 0x1d87,
    0x1d88, 0x1d89, 0x1d8a, 0x1d8b, 0x1d8c, 0x1d8d, 0x1d8e, 0x1d8f, 0x1d90, 0x1d91, 0x1d92, 0x1d93,
    0x1d94, 0x1d95, 0x1d96, 0x1d97, 0x1d98, 0x1d99, 0x1d9a, 0x1d9b, 0x1d9c, 0x1d9d, 0x1d9e, 0x1d9f,
    0x1da0, 0x1da1, 0x1da2, 0x1da3, 0x1da4, 0x1da5, 0x1da6, 0x1da7, 0x1da8, 0x1da9, 0x1daa, 0x1dab,
    0x1dac, 0x1dad, 0x1dae, 0x1daf, 0x1db0, 0x1db1, 0x1db2, 0x1db3, 0x1db4, 0x1db5, 0x1db6, 0x1db7,
    0x1db8, 0x1db9, 0x1dba, 0x1dbb, 0x1dbc, 0x1dbd, 0x1dbe, 0x1dbf, 0x1dc0, 0x1dc1, 0x1dc2, 0x1dc3,
    0x1dc4, 0x1dc5, 0x1dc6, 0x1dc7, 0x1dc8, 0x1dc9, 0x1dca, 0x1dcb, 0x1dcc, 0x1dcd, 0x1dce, 0x1dcf,
    0x1dd0, 0x1dd1, 0x1dd2, 0x1dd3, 0x1dd4, 0x1dd5, 0x1dd6, 0x1dd7, 0x1dd8, 0x1dd9, 0x1dda, 0x1ddb,
    0x1ddc, 0x1ddd, 0x1dde, 0x1ddf, 0x1de0, 0x1de1, 0x1de2, 0x1de3, 0x1de4, 0x1de5, 0x1de6, 0x1de7,
    0x1de8, 0x1de9, 0x1dea, 0x1deb, 0x1dec, 0x1ded, 0x1dee, 0x1def, 0x1df0, 0x1df1, 0x1df2, 0x1df3,
    0x1df4, 0x1df5, 0x1df6, 0x1df7, 0x1df8, 0x1df9, 0x1dfa, 0x1dfb, 0x1dfc, 0x1dfd, 0x1dfe, 0x1dff,
    0x1e00, 0x1e00, 0x1e02, 0x1e02, 0x1e04, 0x1e04, 0x1e06, 0x1e06, 0x1e08, 0x1e08, 0x1e0a, 0x1e0a,
    0x1e0c, 0x1e0c, 0x1e0e, 0x1e0e, 0x1e10, 0x1e10, 0x1e12, 0x1e12, 0x1e14, 0x1e14, 0x1e16, 0x1e16,
    0x1e18, 0x1e18, 0x1e1a, 0x1e1a, 0x1e1c, 0x1e1c, 0x1e1e, 0x1e1e, 0x1e20, 0x1e20, 0x1e22, 0x1e22,
    0x1e24, 0x1e24, 0x1e26, 0x1e26, 0x1e28, 0x1e28, 0x1e2a, 0x1e2a, 0x1e2c, 0x1e2c, 0x1e2e, 0x1e2e,
    0x1e30, 0x1e30, 0x1e32, 0x1e32, 0x1e34, 0x1e34, 0x1e36, 0x1e36, 0x1e38, 0x1e38, 0x1e3a, 0x1e3a,
    0x1e3c, 0x1e3c, 0x1e3e, 0x1e3e, 0x1e40, 0x1e40, 0x1e42, 0x1e42, 0x1e44, 0x1e44, 0x1e46, 0x1e46,
    0x1e48, 0x1e48, 0x1e4a, 0x1e4a, 0x1e4c, 0x1e4c, 0x1e4e, 0x1e4e, 0x1e50, 0x1e50, 0x1e52, 0x1e52,
    0x1e54, 0x1e54, 0x1e56, 0x1e56, 0x1e58, 0x1e58, 0x1e5a, 0x1e5a, 0x1e5c, 0x1e5c, 0x1e5e, 0x1e5e,
    0x1e60, 0x1e60, 0x1e62, 0x1e62, 0x1e64, 0x1e64, 0x1e66, 0x1e66, 0x1e68, 0x1e68, 0x1e6a, 0x1e6a,
    0x1e6c, 0x1e6c, 0x1e6e, 0x1e6e, 0x1e70, 0x1e70, 0x1e72, 0x1e72, 0x1e74, 0x1e74, 0x1e76, 0x1e76,
    0x1e78, 0x1e78, 0x1e7a, 0x1e7a, 0x1e7c, 0x1e7c, 0x1e7e, 0x1e7e, 0x1e80, 0x1e80, 0x1e82, 0x1e82,
    0x1e84, 0x1e84, 0x1e86, 0x1e86, 0x1e88, 0x1e88, 0x1e8a, 0x1e8a, 0x1e8c, 0x1e8c, 0x1e8e, 0x1e8e,
    0x1e90, 0x1e90, 0x1e92, 0x1e92, 0x1e94, 0x1e94, 0x1e96, 0x1e97, 0x1e98, 0x1e99, 0x1e9a, 0x1e9b,
    0x1e9c, 0x1e9d, 0x1e9e, 0x1e9f, 0x1ea0, 0x1ea0, 0x1ea2, 0x1ea2, 0x1ea4, 0x1ea4, 0x1ea6, 0x1ea6,
    0x1ea8, 0x1ea8, 0x1eaa, 0x1eaa, 0x1eac, 0x1eac, 0x1eae, 0x1eae, 0x1eb0, 0x1eb0, 0x1eb2, 0x1eb2,
    0x1eb4, 0x1eb4, 0x1eb6, 0x1eb6, 0x1eb8, 0x1eb8, 0x1eba, 0x1eba, 0x1ebc, 0x1ebc, 0x1ebe, 0x1ebe,
    0x1ec0, 0x1ec0, 0x1ec2, 0x1ec2, 0x1ec4, 0x1ec4, 0x1ec6, 0x1ec6, 0x1ec8, 0x1ec8, 0x1eca, 0x1eca,
    0x1ecc, 0x1ecc, 0x1ece, 0x1ece, 0x1ed0, 0x1ed0, 0x1ed2, 0x1ed2, 0x1ed4, 0x1ed4, 0x1ed6, 0x1ed6,
    0x1ed8, 0x1ed8, 0x1eda, 0x1eda, 0x1edc, 0x1edc, 0x1ede, 0x1ede, 0x1ee0, 0x1ee0, 0x1ee2, 0x1ee2,
    0x1ee4, 0x1ee4, 0x1ee6, 0x1ee6, 0x1ee8, 0x1ee8, 0x1eea, 0x1eea, 0x1eec, 0x1eec, 0x1eee, 0x1eee,
    0x1ef0, 0x1ef0, 0x1ef2, 0x1ef2, 0x1ef4, 0x1ef4, 0x1ef6, 0x1ef6, 0x1ef8, 0x1ef8, 0x1efa, 0x1efb,
    0x1efc, 0x1efd, 0x1efe, 0x1eff, 0x1f08, 0x1f09, 0x1f0a, 0x1f0b, 0x1f0c, 0x1f0d, 0x1f0e, 0x1f0f,
    0x1f08, 0x1f09, 0x1f0a, 0x1f0b, 0x1f0c, 0x1f0d, 0x1f0e, 0x1f0f, 0x1f18, 0x1f19, 0x1f1a, 0x1f1b,
    0x1f1c, 0x1f1d, 0x1f16, 0x1f17, 0x1f18, 0x1f19, 0x1f1a, 0x1f1b, 0x1f1c, 0x1f1d, 0x1f1e, 0x1f1f,
    0x1f28, 0x1f29, 0x1f2a, 0x1f2b, 0x1f2c, 0x1f2d, 0x1f2e, 0x1f2f, 0x1f28, 0x1f29, 0x1f2a, 0x1f2b,
    0x1f2c, 0x1f2d, 0x1f2e, 0x1f2f, 0x1f38, 0x1f39, 0x1f3a, 0x1f3b, 0x1f3c, 0x1f3d, 0x1f3e, 0x1f3f,
    0x1f38, 0x1f39, 0x1f3a, 0x1f3b, 0x1f3c, 0x1f3d, 0x1f3e, 0x1f3f, 0x1f48, 0x1f49, 0x1f4a, 0x1f4b,
    0x1f4c, 0x1f4d, 0x1f46, 0x1f47, 0x1f48, 0x1f49, 0x1f4a, 0x1f4b, 0x1f4c, 0x1f4d, 0x1f4e, 0x1f4f,
    0x1f50, 0x1f59, 0x1f52, 0x1f5b, 0x1f54, 0x1f5d, 0x1f56, 0x1f5f, 0x1f58, 0x1f59, 0x1f5a, 0x1f5b,
    0x1f5c, 0x1f5d, 0x1f5e, 0x1f5f, 0x1f68, 0x1f69, 0x1f6a, 0x1f6b, 0x1f6c, 0x1f6d, 0x1f6e, 0x1f6f,
    0x1f68, 0x1f69, 0x1f6a, 0x1f6b, 0x1f6c, 0x1f6d, 0x1f6e, 0x1f6f, 0x1fba, 0x1fbb, 0x1fc8, 0x1fc9,
    0x1fca, 0x1fcb, 0x1fda, 0x1fdb, 0x1ff8, 0x1ff9, 0x1fea, 0x1feb, 0x1ffa, 0x1ffb, 0x1f7e, 0x1f7f,
    0x1f88, 0x1f89, 0x1f8a, 0x1f8b, 0x1f8c, 0x1f8d, 0x1f8e, 0x1f8f, 0x1f88, 0x1f89, 0x1f8a, 0x1f8b,
    0x1f8c, 0x1f8d, 0x1f8e, 0x1f8f, 0x1f98, 0x1f99, 0x1f9a, 0x1f9b, 0x1f9c, 0x1f9d, 0x1f9e, 0x1f9f,
    0x1f98, 0x1f99, 0x1f9a, 0x1f9b, 0x1f9c, 0x1f9d, 0x1f9e, 0x1f9f, 0x1fa8, 0x1fa9, 0x1faa, 0x1fab,
    0x1fac, 0x1fad, 0x1fae, 0x1faf, 0x1fa8, 0x1fa9, 0x1faa, 0x1fab, 0x1fac, 0x1fad, 0x1fae, 0x1faf,
    0x1fb8, 0x1fb9, 0x1fb2, 0x1fbc, 0x1fb4, 0x1fb5, 0x1fb6, 0x1fb7, 0x1fb8, 0x1fb9, 0x1fba, 0x1fbb,
    0x1fbc, 0x1fbd, 0x1fbe, 0x1fbf, 0x1fc0, 0x1fc1, 0x1fc2, 0x1fc3, 0x1fc4, 0x1fc5, 0x1fc6, 0x1fc7,
    0x1fc8, 0x1fc9, 0x1fca, 0x1fcb, 0x1fc3, 0x1fcd, 0x1fce, 0x1fcf, 0x1fd8, 0x1fd9, 0x1fd2, 0x1fd3,
    0x1fd4, 0x1fd5, 0x1fd6, 0x1fd7, 0x1fd8, 0x1fd9, 0x1fda, 0x1fdb, 0x1fdc, 0x1fdd, 0x1fde, 0x1fdf,
    0x1fe8, 0x1fe9, 0x1fe2, 0x1fe3, 0x1fe4, 0x1fec, 0x1fe6, 0x1fe7, 0x1fe8, 0x1fe9, 0x1fea, 0x1feb,
    0x1fec, 0x1fed, 0x1fee, 0x1fef, 0x1ff0, 0x1ff1, 0x1ff2, 0x1ff3, 0x1ff4, 0x1ff5, 0x1ff6, 0x1ff7,
    0x1ff8, 0x1ff9, 0x1ffa, 0x1ffb, 0x1ff3, 0x1ffd, 0x1ffe, 0x1fff, 0x2000, 0x2001, 0x2002, 0x2003,
    0x2004, 0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200a, 0x200b, 0x200c, 0x200d, 0x200e, 0x200f,
    0x2010, 0x2011, 0x2012, 0x2013, 0x2014, 0x2015, 0x2016, 0x2017, 0x2018, 0x2019, 0x201a, 0x201b,
    0x201c, 0x201d, 0x201e, 0x201f, 0x2020, 0x2021, 0x2022, 0x2023, 0x2024, 0x2025, 0x2026, 0x2027,
    0x2028, 0x2029, 0x202a, 0x202b, 0x202c, 0x202d, 0x202e, 0x202f, 0x2030, 0x2031, 0x2032, 0x2033,
    0x2034, 0x2035, 0x2036, 0x2037, 0x2038, 0x2039, 0x203a, 0x203b, 0x203c, 0x203d, 0x203e, 0x203f,
    0x2040, 0x2041, 0x2042, 0x2043, 0x2044, 0x2045, 0x2046, 0x2047, 0x2048, 0x2049, 0x204a, 0x204b,
    0x204c, 0x204d, 0x204e, 0x204f, 0x2050, 0x2051, 0x2052, 0x2053, 0x2054, 0x2055, 0x2056, 0x2057,
    0x2058, 0x2059, 0x205a, 0x205b, 0x205c, 0x205d, 0x205e, 0x205f, 0x2060, 0x2061, 0x2062, 0x2063,
    0x2064, 0x2065, 0x2066, 0x2067, 0x2068, 0x2069, 0x206a, 0x206b, 0x206c, 0x206d, 0x206e, 0x206f,
    0x2070, 0x2071, 0x2072, 0x2073, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0x2079, 0x207a, 0x207b,
    0x207c, 0x207d, 0x207e, 0x207f, 0x2080, 0x2081, 0x2082, 0x2083, 0x2084, 0x2085, 0x2086, 0x2087,
    0x2088, 0x2089, 0x208a, 0x208b, 0x208c, 0x208d, 0x208e, 0x208f, 0x2090, 0x2091, 0x2092, 0x2093,
    0x2094, 0x2095, 0x2096, 0x2097, 0x2098, 0x2099, 0x209a, 0x209b, 0x209c, 0x209d, 0x209e, 0x209f,
    0x20a0, 0x20a1, 0x20a2, 0x20a3, 0x20a4, 0x20a5, 0x20a6, 0x20a7, 0x20a8, 0x20a9, 0x20aa, 0x20ab,
    0x20ac, 0x20ad, 0x20ae, 0x20af, 0x20b0, 0x20b1, 0x20b2, 0x20b3, 0x20b4, 0x20b5, 0x20b6, 0x20b7,
    0x20b8, 0x20b9, 0x20ba, 0x20bb, 0x20bc, 0x20bd, 0x20be, 0x20bf, 0x20c0, 0x20c1, 0x20c2, 0x20c3,
    0x20c4, 0x20c5, 0x20c6, 0x20c7, 0x20c8, 0x20c9, 0x20ca, 0x20cb, 0x20cc, 0x20cd, 0x20ce, 0x20cf,
    0x20d0, 0x20d1, 0x20d2, 0x20d3, 0x20d4, 0x20d5, 0x20d6, 0x20d7, 0x20d8, 0x20d9, 0x20da, 0x20db,
    0x20dc, 0x20dd, 0x20de, 0x20df, 0x20e0, 0x20e1, 0x20e2, 0x20e3, 0x20e4, 0x20e5, 0x20e6, 0x20e7,
    0x20e8, 0x20e9, 0x20ea, 0x20eb, 0x20ec, 0x20ed, 0x20ee, 0x20ef, 0x20f0, 0x20f1, 0x20f2, 0x20f3,
    0x20f4, 0x20f5, 0x20f6, 0x20f7, 0x20f8, 0x20f9, 0x20fa, 0x20fb, 0x20fc, 0x20fd, 0x20fe, 0x20ff,
    0x2100, 0x2101, 0x2102, 0x2103, 0x2104, 0x2105, 0x2106, 0x2107, 0x2108, 0x2109, 0x210a, 0x210b,
    0x210c, 0x210d, 0x210e, 0x210f, 0x2110, 0x2111, 0x2112, 0x2113, 0x2114, 0x2115, 0x2116, 0x2117,
    0x2118, 0x2119, 0x211a, 0x211b, 0x211c, 0x211d, 0x211e, 0x211f, 0x2120, 0x2121, 0x2122, 0x2123,
    0x2124, 0x2125, 0x2126, 0x2127, 0x2128, 0x2129, 0x212a, 0x212b, 0x212c, 0x212d, 0x212e, 0x212f,
    0x2130, 0x2131, 0x2132, 0x2133, 0x2134, 0x2135, 0x2136, 0x2137, 0x2138, 0x2139, 0x213a, 0x213b,
    0x213c, 0x213d, 0x213e, 0x213f, 0x2140, 0x2141, 0x2142, 0x2143, 0x2144, 0x2145, 0x2146, 0x2147,
    0x2148, 0x2149, 0x214a, 0x214b, 0x214c, 0x214d, 0x2132, 0x214f, 0x2150, 0x2151, 0x2152, 0x2153,
    0x2154, 0x2155, 0x2156, 0x2157, 0x2158, 0x2159, 0x215a, 0x215b, 0x215c, 0x215d, 0x215e, 0x215f,
    0x2160, 0x2161, 0x2162, 0x2163, 0x2164, 0x2165, 0x2166, 0x2167, 0x2168, 0x2169, 0x216a, 0x216b,
    0x216c, 0x216d, 0x216e, 0x216f, 0x2160, 0x2161, 0x2162, 0x2163, 0x2164, 0x2165, 0x2166, 0x2167,
    0x2168, 0x2169, 0x216a, 0x216b, 0x216c, 0x216d, 0x216e, 0x216f, 0x2180, 0x2181, 0x2182, 0x2183,
    0x2183, 0xffff, 0x034b, 0x24b6, 0x24b7, 0x24b8, 0x24b9, 0x24ba, 0x24bb, 0x24bc, 0x24bd, 0x24be,
    0x24bf, 0x24c0, 0x24c1, 0x24c2, 0x24c3, 0x24c4, 0x24c5, 0x24c6, 0x24c7, 0x24c8, 0x24c9, 0x24ca,
    0x24cb, 0x24cc, 0x24cd, 0x24ce, 0x24cf, 0xffff, 0x0746, 0x2c00, 0x2c01, 0x2c02, 0x2c03, 0x2c04,
    0x2c05, 0x2c06, 0x2c07, 0x2c08, 0x2c09, 0x2c0a, 0x2c0b, 0x2c0c, 0x2c0d, 0x2c0e, 0x2c0f, 0x2c10,
    0x2c11, 0x2c12, 0x2c13, 0x2c14, 0x2c15, 0x2c16, 0x2c17, 0x2c18, 0x2c19, 0x2c1a, 0x2c1b, 0x2c1c,
    0x2c1d, 0x2c1e, 0x2c1f, 0x2c20, 0x2c21, 0x2c22, 0x2c23, 0x2c24, 0x2c25, 0x2c26, 0x2c27, 0x2c28,
    0x2c29, 0x2c2a, 0x2c2b, 0x2c2c, 0x2c2d, 0x2c2e, 0x2c5f, 0x2c60, 0x2c60, 0x2c62, 0x2c63, 0x2c64,
    0x2c65, 0x2c66, 0x2c67, 0x2c67, 0x2c69, 0x2c69, 0x2c6b, 0x2c6b, 0x2c6d, 0x2c6e, 0x2c6f, 0x2c70,
    0x2c71, 0x2c72, 0x2c73, 0x2c74, 0x2c75, 0x2c75, 0x2c77, 0x2c78, 0x2c79, 0x2c7a, 0x2c7b, 0x2c7c,
    0x2c7d, 0x2c7e, 0x2c7f, 0x2c80, 0x2c80, 0x2c82, 0x2c82, 0x2c84, 0x2c84, 0x2c86, 0x2c86, 0x2c88,
    0x2c88, 0x2c8a, 0x2c8a, 0x2c8c, 0x2c8c, 0x2c8e, 0x2c8e, 0x2c90, 0x2c90, 0x2c92, 0x2c92, 0x2c94,
    0x2c94, 0x2c96, 0x2c96, 0x2c98, 0x2c98, 0x2c9a, 0x2c9a, 0x2c9c, 0x2c9c, 0x2c9e, 0x2c9e, 0x2ca0,
    0x2ca0, 0x2ca2, 0x2ca2, 0x2ca4, 0x2ca4, 0x2ca6, 0x2ca6, 0x2ca8, 0x2ca8, 0x2caa, 0x2caa, 0x2cac,
    0x2cac, 0x2cae, 0x2cae, 0x2cb0, 0x2cb0, 0x2cb2, 0x2cb2, 0x2cb4, 0x2cb4, 0x2cb6, 0x2cb6, 0x2cb8,
    0x2cb8, 0x2cba, 0x2cba, 0x2cbc, 0x2cbc, 0x2cbe, 0x2cbe, 0x2cc0, 0x2cc0, 0x2cc2, 0x2cc2, 0x2cc4,
    0x2cc4, 0x2cc6, 0x2cc6, 0x2cc8, 0x2cc8, 0x2cca, 0x2cca, 0x2ccc, 0x2ccc, 0x2cce, 0x2cce, 0x2cd0,
    0x2cd0, 0x2cd2, 0x2cd2, 0x2cd4, 0x2cd4, 0x2cd6, 0x2cd6, 0x2cd8, 0x2cd8, 0x2cda, 0x2cda, 0x2cdc,
    0x2cdc, 0x2cde, 0x2cde, 0x2ce0, 0x2ce0, 0x2ce2, 0x2ce2, 0x2ce4, 0x2ce5, 0x2ce6, 0x2ce7, 0x2ce8,
    0x2ce9, 0x2cea, 0x2ceb, 0x2cec, 0x2ced, 0x2cee, 0x2cef, 0x2cf0, 0x2cf1, 0x2cf2, 0x2cf3, 0x2cf4,
    0x2cf5, 0x2cf6, 0x2cf7, 0x2cf8, 0x2cf9, 0x2cfa, 0x2cfb, 0x2cfc, 0x2cfd, 0x2cfe, 0x2cff, 0x10a0,
    0x10a1, 0x10a2, 0x10a3, 0x10a4, 0x10a5, 0x10a6, 0x10a7, 0x10a8, 0x10a9, 0x10aa, 0x10ab, 0x10ac,
    0x10ad, 0x10ae, 0x10af, 0x10b0, 0x10b1, 0x10b2, 0x10b3, 0x10b4, 0x10b5, 0x10b6, 0x10b7, 0x10b8,
    0x10b9, 0x10ba, 0x10bb, 0x10bc, 0x10bd, 0x10be, 0x10bf, 0x10c0, 0x10c1, 0x10c2, 0x10c3, 0x10c4,
    0x10c5, 0xffff, 0xd21b, 0xff21, 0xff22, 0xff23, 0xff24, 0xff25, 0xff26, 0xff27, 0xff28, 0xff29,
    0xff2a, 0xff2b, 0xff2c, 0xff2d, 0xff2e, 0xff2f, 0xff30, 0xff31, 0xff32, 0xff33, 0xff34, 0xff35,
    0xff36, 0xff37, 0xff38, 0xff39, 0xff3a, 0xff5b, 0xff5c, 0xff5d, 0xff5e, 0xff5f, 0xff60, 0xff61,
    0xff62, 0xff63, 0xff64, 0xff65, 0xff66, 0xff67, 0xff68, 0xff69, 0xff6a, 0xff6b, 0xff6c, 0xff6d,
    0xff6e, 0xff6f, 0xff70, 0xff71, 0xff72, 0xff73, 0xff74, 0xff75, 0xff76, 0xff77, 0xff78, 0xff79,
    0xff7a, 0xff7b, 0xff7c, 0xff7d, 0xff7e, 0xff7f, 0xff80, 0xff81, 0xff82, 0xff83, 0xff84, 0xff85,
    0xff86, 0xff87, 0xff88, 0xff89, 0xff8a, 0xff8b, 0xff8c, 0xff8d, 0xff8e, 0xff8f, 0xff90, 0xff91,
    0xff92, 0xff93, 0xff94, 0xff95, 0xff96, 0xff97, 0xff98, 0xff99, 0xff9a, 0xff9b, 0xff9c, 0xff9d,
    0xff9e, 0xff9f, 0xffa0, 0xffa1, 0xffa2, 0xffa3, 0xffa4, 0xffa5, 0xffa6, 0xffa7, 0xffa8, 0xffa9,
    0xffaa, 0xffab, 0xffac, 0xffad, 0xffae, 0xffaf, 0xffb0, 0xffb1, 0xffb2, 0xffb3, 0xffb4, 0xffb5,
    0xffb6, 0xffb7, 0xffb8, 0xffb9, 0xffba, 0xffbb, 0xffbc, 0xffbd, 0xffbe, 0xffbf, 0xffc0, 0xffc1,
    0xffc2, 0xffc3, 0xffc4, 0xffc5, 0xffc6, 0xffc7, 0xffc8, 0xffc9, 0xffca, 0xffcb, 0xffcc, 0xffcd,
    0xffce, 0xffcf, 0xffd0, 0xffd1, 0xffd2, 0xffd3, 0xffd4, 0xffd5, 0xffd6, 0xffd7, 0xffd8, 0xffd9,
    0xffda, 0xffdb, 0xffdc, 0xffdd, 0xffde, 0xffdf, 0xffe0, 0xffe1, 0xffe2, 0xffe3, 0xffe4, 0xffe5,
    0xffe6, 0xffe7, 0xffe8, 0xffe9, 0xffea, 0xffeb, 0xffec, 0xffed, 0xffee, 0xffef, 0xfff0, 0xfff1,
    0xfff2, 0xfff3, 0xfff4, 0xfff5, 0xfff6, 0xfff7, 0xfff8, 0xfff9, 0xfffa, 0xfffb, 0xfffc, 0xfffd,
    0xfffe, 0xffff,
};

constexpr std::array<uint16_t, 65536> BuildUncompressedUpcaseTable() {
    std::array<uint16_t, 65536> table = {};
    uint32_t cp = 0;
    for (size_t i = 0; i < kDefaultUpcaseTable.size() && cp < table.size();) {
        const uint16_t entry = kDefaultUpcaseTable[i];
        if (entry == 0xFFFFU && i + 1 < kDefaultUpcaseTable.size()) {
            const uint16_t count = kDefaultUpcaseTable[i + 1];
            for (uint32_t j = 0; j < count && cp < table.size(); ++j) {
                table[cp] = static_cast<uint16_t>(cp);
                ++cp;
            }
            i += 2;
        } else {
            table[cp] = entry;
            ++cp;
            ++i;
        }
    }
    while (cp < table.size()) {
        table[cp] = static_cast<uint16_t>(cp);
        ++cp;
    }
    return table;
}

uint16_t UpcaseCodeUnit(uint16_t ch) {
    static constexpr std::array<uint16_t, 65536> kUncompressedUpcaseTable =
            BuildUncompressedUpcaseTable();
    return kUncompressedUpcaseTable[ch];
}

uint32_t ComputeChecksum32(const uint8_t* data, size_t len, uint32_t chksum = 0,
                           bool is_boot_sector = false) {
    for (size_t i = 0; i < len; ++i) {
        if (is_boot_sector && (i == 106 || i == 107 || i == 112)) {
            continue;
        }
        chksum = std::rotr(chksum, 1) + data[i];
    }
    return chksum;
}

uint16_t ComputeSetChecksum16(const uint8_t* data, size_t len) {
    uint16_t chksum = 0;
    for (size_t i = 0; i < len; ++i) {
        if (i == 2 || i == 3) {
            continue;
        }
        chksum = static_cast<uint16_t>(std::rotr(chksum, 1) + data[i]);
    }
    return chksum;
}

uint16_t ComputeNameHash(const std::vector<uint16_t>& utf16_name) {
    uint16_t hash = 0;
    for (const uint16_t ch : utf16_name) {
        const uint16_t up_le = HostToLe16(UpcaseCodeUnit(ch));
        const auto* bytes = reinterpret_cast<const uint8_t*>(&up_le);
        hash = static_cast<uint16_t>(std::rotr(hash, 1) + bytes[0]);
        hash = static_cast<uint16_t>(std::rotr(hash, 1) + bytes[1]);
    }
    return hash;
}

uint8_t ChooseSectorsPerClusterShift(uint64_t volume_bytes) {
    if (volume_bytes <= 256ULL * 1024 * 1024) {
        return 3U;  // 4 KiB clusters (8 sectors)
    } else if (volume_bytes <= 32ULL * 1024 * 1024 * 1024) {
        return 6U;  // 32 KiB clusters (64 sectors)
    } else {
        return 8U;  // 128 KiB clusters (256 sectors)
    }
}

uint64_t RoundUp(uint64_t value, uint64_t alignment) {
    return ((value + alignment - 1ULL) / alignment) * alignment;
}

bool IsValidExfatNameCodePoint(uint32_t cp) {
    if (cp < 0x20U) {
        return false;
    }
    switch (cp) {
    case '"':
    case '*':
    case '/':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
        return false;
    default:
        return true;
    }
}

bool IsValidVolumeLabelChar(char ch) {
    const auto c = static_cast<uint8_t>(ch);
    return c <= 0x7EU && IsValidExfatNameCodePoint(c);
}

absl::StatusOr<std::vector<uint16_t>> Utf8ToExfatUtf16(std::string_view utf8_name) {
    if (utf8_name.empty() || utf8_name == "." || utf8_name == "..") {
        return absl::InvalidArgumentError(
                absl::StrFormat("Invalid exFAT filename '%s'", utf8_name));
    }

    std::vector<uint16_t> utf16;
    utf16.reserve(utf8_name.size());

    size_t i = 0;
    while (i < utf8_name.size()) {
        const auto b0 = static_cast<uint8_t>(utf8_name[i]);
        uint32_t cp = 0;
        size_t len = 0;
        if ((b0 & 0x80U) == 0U) {
            cp = b0;
            len = 1;
        } else if ((b0 & 0xE0U) == 0xC0U) {
            cp = b0 & 0x1FU;
            len = 2;
        } else if ((b0 & 0xF0U) == 0xE0U) {
            cp = b0 & 0x0FU;
            len = 3;
        } else if ((b0 & 0xF8U) == 0xF0U) {
            cp = b0 & 0x07U;
            len = 4;
        } else {
            return absl::InvalidArgumentError(
                    absl::StrFormat("Invalid UTF-8 in exFAT filename '%s'", utf8_name));
        }

        if (i + len > utf8_name.size()) {
            return absl::InvalidArgumentError(
                    absl::StrFormat("Truncated UTF-8 sequence in exFAT filename '%s'", utf8_name));
        }
        for (size_t j = 1; j < len; ++j) {
            const auto cont = static_cast<uint8_t>(utf8_name[i + j]);
            if ((cont & 0xC0U) != 0x80U) {
                return absl::InvalidArgumentError(
                        absl::StrFormat("Invalid UTF-8 continuation byte in '%s'", utf8_name));
            }
            cp = (cp << 6U) | (cont & 0x3FU);
        }

        if ((len == 2 && cp < 0x80U) || (len == 3 && cp < 0x800U) || (len == 4 && cp < 0x10000U) ||
            (cp >= 0xD800U && cp <= 0xDFFFU) || cp > 0x10FFFFU) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Invalid Unicode code point in exFAT filename '%s'", utf8_name));
        }
        if (!IsValidExfatNameCodePoint(cp)) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "exFAT filename '%s' contains disallowed character", utf8_name));
        }

        if (cp <= 0xFFFFU) {
            utf16.push_back(static_cast<uint16_t>(cp));
        } else {
            const uint32_t sub = cp - 0x10000U;
            utf16.push_back(static_cast<uint16_t>(0xD800U + (sub >> 10U)));
            utf16.push_back(static_cast<uint16_t>(0xDC00U + (sub & 0x3FFU)));
        }
        i += len;
    }

    if (utf16.empty() || utf16.size() > kMaxFileNameLength) {
        return absl::InvalidArgumentError(
                absl::StrFormat("exFAT filename '%s' length (%u) exceeds maximum (%u)", utf8_name,
                                utf16.size(), kMaxFileNameLength));
    }
    return utf16;
}

struct ExfatTreeNode {
    fs::path host_path;
    std::string utf8_name;
    std::vector<uint16_t> utf16_name;
    std::vector<ExfatTreeNode> children;
    std::vector<ExfatDentry> dir_entries;
    uint64_t file_size = 0;
    uint32_t first_cluster = 0;
    uint32_t cluster_count = 0;
    uint16_t name_hash = 0;
    bool is_directory = false;
};

absl::Status ScanDirectoryRecursive(const fs::path& dir_path, ExfatTreeNode* dir_node) {
    std::error_code ec;
    std::vector<std::pair<fs::directory_entry, std::u8string>> entries;
    for (fs::directory_iterator it(dir_path, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        entries.emplace_back(*it, it->path().filename().u8string());
    }
    if (ec) {
        return absl::InternalError(absl::StrFormat("Failed to iterate directory '%s': %s",
                                                   dir_path.string(), ec.message()));
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    for (auto& entry : entries) {
        entry.second.clear();
        entry.second.shrink_to_fit();
    }

    absl::flat_hash_set<std::vector<uint16_t>> seen_upcase_names;
    dir_node->children.reserve(entries.size());

    for (const auto& entry : entries) {
        const fs::path& child_path = entry.first.path();
        const fs::file_status st = fs::symlink_status(child_path, ec);
        if (ec) {
            return absl::InternalError(
                    absl::StrFormat("Failed to stat '%s': %s", child_path.string(), ec.message()));
        }

        ExfatTreeNode child;
        child.host_path = child_path;
        child.utf8_name = child_path.filename().string();
        ASSIGN_OR_RETURN(child.utf16_name, Utf8ToExfatUtf16(child.utf8_name));
        child.name_hash = ComputeNameHash(child.utf16_name);

        std::vector<uint16_t> upcase_name(child.utf16_name.size());
        for (size_t i = 0; i < child.utf16_name.size(); ++i) {
            upcase_name[i] = UpcaseCodeUnit(child.utf16_name[i]);
        }
        if (!seen_upcase_names.insert(std::move(upcase_name)).second) {
            return absl::InvalidArgumentError(
                    absl::StrFormat("Duplicate case-insensitive filename '%s' in '%s'",
                                    child.utf8_name, dir_path.string()));
        }

        if (st.type() == fs::file_type::directory) {
            child.is_directory = true;
            RETURN_IF_ERROR(ScanDirectoryRecursive(child_path, &child));
        } else if (st.type() == fs::file_type::regular) {
            child.is_directory = false;
            child.file_size = fs::file_size(child_path, ec);
            if (ec) {
                return absl::InternalError(absl::StrFormat("Failed to get file size of '%s': %s",
                                                           child_path.string(), ec.message()));
            }
        } else {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Unsupported file type for '%s' (exFAT supports only regular files and "
                    "directories)",
                    child_path.string()));
        }

        dir_node->children.push_back(std::move(child));
    }

    return absl::OkStatus();
}

size_t EntrySetByteSize(const ExfatTreeNode& child) {
    const size_t name_entries =
            (child.utf16_name.size() + kCharsPerFileNameDentry - 1U) / kCharsPerFileNameDentry;
    return (2U + name_entries) * sizeof(ExfatDentry);
}

absl::Status AssignNodeClusters(ExfatTreeNode* node, uint64_t cluster_count,
                                uint32_t* next_cluster) {
    if (cluster_count == 0) {
        node->cluster_count = 0;
        node->first_cluster = 0;
        return absl::OkStatus();
    }
    node->first_cluster = *next_cluster;
    if (__builtin_add_overflow(*next_cluster, cluster_count, next_cluster)) {
        return absl::OutOfRangeError(
                absl::StrFormat("Cluster index overflow while allocating clusters for '%s'",
                                node->host_path.string()));
    }
    node->cluster_count = static_cast<uint32_t>(cluster_count);
    return absl::OkStatus();
}

absl::Status AssignClustersRecursive(ExfatTreeNode* node, bool is_root, uint64_t cluster_size,
                                     uint32_t* next_cluster) {
    if (node->is_directory) {
        uint64_t dir_bytes = is_root ? (3ULL * sizeof(ExfatDentry)) : 0ULL;
        for (const ExfatTreeNode& child : node->children) {
            dir_bytes += EntrySetByteSize(child);
        }
        if (dir_bytes > kMaxDirectorySizeBytes) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "Directory '%s' exceeds maximum exFAT directory size (%u bytes)",
                    node->host_path.string(), kMaxDirectorySizeBytes));
        }
        const uint64_t cluster_count =
                std::max<uint64_t>(1ULL, (dir_bytes + cluster_size - 1ULL) / cluster_size);
        RETURN_IF_ERROR(AssignNodeClusters(node, cluster_count, next_cluster));

        for (ExfatTreeNode& child : node->children) {
            RETURN_IF_ERROR(
                    AssignClustersRecursive(&child, /*is_root=*/false, cluster_size, next_cluster));
        }
    } else {
        const uint64_t cluster_count = (node->file_size + cluster_size - 1ULL) / cluster_size;
        RETURN_IF_ERROR(AssignNodeClusters(node, cluster_count, next_cluster));
    }
    return absl::OkStatus();
}

void AppendChildEntrySet(const ExfatTreeNode& child, uint64_t cluster_size,
                         std::vector<ExfatDentry>* out) {
    const size_t name_entries =
            (child.utf16_name.size() + kCharsPerFileNameDentry - 1U) / kCharsPerFileNameDentry;
    const size_t set_entries = 2U + name_entries;
    const size_t start_index = out->size();

    out->push_back(ExfatDentry{
        .file = {
            .type = kDentryTypeFile,
            .secondary_count = static_cast<uint8_t>(1U + name_entries),
            .set_checksum = 0U,
            .file_attributes = HostToLe16(child.is_directory ? kAttrDirectory : kAttrArchive),
            .reserved1 = 0U,
            .create_timestamp = HostToLe32(kDefaultDosTimestamp),
            .last_modified_timestamp = HostToLe32(kDefaultDosTimestamp),
            .last_accessed_timestamp = HostToLe32(kDefaultDosTimestamp),
            .create_10ms_increment = 0U,
            .last_modified_10ms_increment = 0U,
            .create_utc_offset = 0U,
            .last_modified_utc_offset = 0U,
            .last_accessed_utc_offset = 0U,
            .reserved2 = {0},
        }});

    const uint64_t stream_size =
            child.is_directory ? (static_cast<uint64_t>(child.cluster_count) * cluster_size)
                               : child.file_size;
    out->push_back(ExfatDentry{.stream_ext = {
                                   .type = kDentryTypeStreamExt,
                                   .general_secondary_flags = kFlagAllocationPossible,
                                   .reserved1 = 0U,
                                   .name_length = static_cast<uint8_t>(child.utf16_name.size()),
                                   .name_hash = HostToLe16(child.name_hash),
                                   .reserved2 = 0U,
                                   .valid_data_length = HostToLe64(stream_size),
                                   .reserved3 = 0U,
                                   .first_cluster = HostToLe32(child.first_cluster),
                                   .data_length = HostToLe64(stream_size),
                               }});

    for (size_t i = 0; i < name_entries; ++i) {
        ExfatFileNameDentry name_dentry = {
            .type = kDentryTypeFileName,
            .general_secondary_flags = 0U,
            .file_name = {0},
        };
        const size_t char_base = i * kCharsPerFileNameDentry;
        const size_t count = std::min(kCharsPerFileNameDentry, child.utf16_name.size() - char_base);
        for (size_t c = 0; c < count; ++c) {
            name_dentry.file_name[c] = HostToLe16(child.utf16_name[char_base + c]);
        }
        out->push_back(ExfatDentry{.file_name = name_dentry});
    }

    const uint16_t set_chksum =
            ComputeSetChecksum16(reinterpret_cast<const uint8_t*>(&(*out)[start_index]),
                                 set_entries * sizeof(ExfatDentry));
    (*out)[start_index].file.set_checksum = HostToLe16(set_chksum);
}

void BuildDirectoryTablesRecursive(ExfatTreeNode* node, uint64_t cluster_size) {
    if (!node->is_directory) {
        return;
    }
    for (const ExfatTreeNode& child : node->children) {
        AppendChildEntrySet(child, cluster_size, &node->dir_entries);
    }
    for (ExfatTreeNode& child : node->children) {
        BuildDirectoryTablesRecursive(&child, cluster_size);
    }
}

void PopulateTreeFatChainsRecursive(const ExfatTreeNode& node, std::vector<uint32_t>* fat_entries) {
    if (node.cluster_count > 0) {
        for (uint32_t i = 0; i + 1 < node.cluster_count; ++i) {
            (*fat_entries)[node.first_cluster + i] = HostToLe32(node.first_cluster + i + 1U);
        }
        (*fat_entries)[node.first_cluster + node.cluster_count - 1U] = HostToLe32(kFatEofCluster);
    }
    for (const ExfatTreeNode& child : node.children) {
        PopulateTreeFatChainsRecursive(child, fat_entries);
    }
}

absl::Status WriteTreeDataRecursive(const ExfatTreeNode& node, uint64_t clu_offset,
                                    uint64_t cluster_size, std::ofstream* ofs) {
    const auto cluster_to_offset = [clu_offset, cluster_size](uint32_t cluster) -> uint64_t {
        return clu_offset * kSectorSize + (cluster - kFirstDataCluster) * cluster_size;
    };

    if (node.is_directory) {
        const uint64_t alloc_bytes = static_cast<uint64_t>(node.cluster_count) * cluster_size;
        const uint64_t dir_table_bytes = node.dir_entries.size() * sizeof(ExfatDentry);
        ofs->seekp(static_cast<std::streamoff>(cluster_to_offset(node.first_cluster)));
        if (!node.dir_entries.empty()) {
            ofs->write(reinterpret_cast<const char*>(node.dir_entries.data()),
                       static_cast<std::streamsize>(dir_table_bytes));
        }
        if (alloc_bytes > dir_table_bytes) {
            const std::vector<char> zeros(static_cast<size_t>(alloc_bytes - dir_table_bytes), 0);
            ofs->write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
        }
        if (!ofs->good()) {
            return absl::InternalError("Failed to write directory entries to exFAT image");
        }
        for (const ExfatTreeNode& child : node.children) {
            RETURN_IF_ERROR(WriteTreeDataRecursive(child, clu_offset, cluster_size, ofs));
        }
    } else if (node.cluster_count > 0) {
        std::ifstream ifs(node.host_path, std::ios_base::binary);
        if (!ifs.good()) {
            return absl::InternalError(
                    absl::StrFormat("Failed to open source file '%s'", node.host_path.string()));
        }
        ofs->seekp(static_cast<std::streamoff>(cluster_to_offset(node.first_cluster)));

        std::vector<char> buf(64U * 1024U, 0);  // NOLINT
        uint64_t remaining = node.file_size;
        while (remaining > 0) {
            const auto to_read =
                    static_cast<std::streamsize>(std::min<uint64_t>(remaining, buf.size()));
            if (!ifs.read(buf.data(), to_read)) {
                return absl::InternalError(absl::StrFormat("Failed to read source file '%s'",
                                                           node.host_path.string()));
            }
            ofs->write(buf.data(), to_read);
            if (!ofs->good()) {
                return absl::InternalError(absl::StrFormat(
                        "Failed to write file '%s' to exFAT image", node.host_path.string()));
            }
            remaining -= static_cast<uint64_t>(to_read);
        }
    }
    return absl::OkStatus();
}

absl::Status CreateExFATImageImpl(const fs::path& image, const fs::path* source_directory,
                                  StorageCapacity size, std::string_view volume_label) {
    if (volume_label.size() > kMaxVolumeLabelLength) {
        return absl::InvalidArgumentError(
                absl::StrFormat("exFAT volume label '%s' exceeds maximum length (%u characters)",
                                volume_label, kMaxVolumeLabelLength));
    }
    for (const char ch : volume_label) {
        if (!IsValidVolumeLabelChar(ch)) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "exFAT volume label '%s' contains invalid character", volume_label));
        }
    }

    const uint64_t total_bytes = size.Bytes();
    if (total_bytes == 0 || (total_bytes % kSectorSize) != 0) {
        return absl::InvalidArgumentError(
                absl::StrFormat("exFAT image size (%u bytes) must be a non-zero multiple of %u",
                                total_bytes, kSectorSize));
    }

    ExfatTreeNode root_node;
    root_node.is_directory = true;
    if (source_directory != nullptr) {
        if (!android::base::file::exists(*source_directory)) {
            return absl::NotFoundError(absl::StrFormat("Source directory '%s' does not exist",
                                                       source_directory->string()));
        }
        if (!android::base::file::is_dir(*source_directory)) {
            return absl::InvalidArgumentError(absl::StrFormat("Source path '%s' is not a directory",
                                                              source_directory->string()));
        }
        root_node.host_path = *source_directory;
        RETURN_IF_ERROR(ScanDirectoryRecursive(*source_directory, &root_node));
    }

    const uint64_t vol_length = total_bytes / kSectorSize;
    const uint8_t sect_per_clus_bits = ChooseSectorsPerClusterShift(total_bytes);
    const uint64_t sect_per_clus = 1ULL << sect_per_clus_bits;
    const uint64_t cluster_size = sect_per_clus * kSectorSize;

    // Place FAT right after the 24-sector Main + Backup Boot Regions (aligned to cluster size).
    const uint64_t fat_offset = RoundUp(kTotalBootSectors, sect_per_clus);
    if (vol_length <= fat_offset + 1ULL + sect_per_clus) {
        return absl::InvalidArgumentError(
                absl::StrFormat("exFAT image size (%u bytes) is too small", total_bytes));
    }

    const uint64_t avail_bytes = (vol_length - fat_offset) * kSectorSize;
    if (avail_bytes <= 8ULL + cluster_size) {
        return absl::InvalidArgumentError(
                absl::StrFormat("exFAT image size (%u bytes) is too small", total_bytes));
    }

    uint64_t max_clusters = (avail_bytes - 8ULL) / (cluster_size + sizeof(uint32_t));
    uint64_t fat_length =
            ((max_clusters + kReservedClusters) * sizeof(uint32_t) + kSectorSize - 1ULL) /
            kSectorSize;
    uint64_t clu_offset = RoundUp(fat_offset + fat_length, sect_per_clus);

    if (vol_length <= clu_offset) {
        return absl::InvalidArgumentError(
                absl::StrFormat("exFAT image size (%u bytes) is too small", total_bytes));
    }

    const uint64_t clu_count = (vol_length - clu_offset) / sect_per_clus;
    if (clu_count > kMaxExfatClusters) {
        return absl::OutOfRangeError(absl::StrFormat(
                "exFAT cluster count (%u) exceeds maximum (%u)", clu_count, kMaxExfatClusters));
    }

    // Recompute exact FAT length needed for clu_count.
    fat_length =
            ((clu_count + kReservedClusters) * sizeof(uint32_t) + kSectorSize - 1ULL) / kSectorSize;

    // Calculate cluster allocation for Allocation Bitmap, Up-case Table, and directory tree.
    const uint64_t bitmap_bytes = (clu_count + 7ULL) / 8ULL;
    const uint32_t bitmap_clusters =
            static_cast<uint32_t>((bitmap_bytes + cluster_size - 1ULL) / cluster_size);
    const uint64_t upcase_bytes = kDefaultUpcaseTable.size() * sizeof(uint16_t);
    const uint32_t upcase_clusters =
            static_cast<uint32_t>((upcase_bytes + cluster_size - 1ULL) / cluster_size);

    const uint32_t bitmap_first_cluster = kFirstDataCluster;
    const uint32_t upcase_first_cluster = bitmap_first_cluster + bitmap_clusters;
    uint32_t next_cluster = upcase_first_cluster + upcase_clusters;

    RETURN_IF_ERROR(
            AssignClustersRecursive(&root_node, /*is_root=*/true, cluster_size, &next_cluster));
    const uint32_t root_first_cluster = root_node.first_cluster;
    const uint32_t used_clusters = next_cluster - kFirstDataCluster;

    if (clu_count < used_clusters) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "exFAT image size (%u bytes) is too small (%u clusters required, %u available)",
                total_bytes, used_clusters, clu_count));
    }

    // 1. Build 12-sector Boot Region (Main and Backup are identical).
    std::vector<uint8_t> boot_region(kBootRegionSectors * kSectorSize, 0U);
    const ExfatBootSector boot_sector = {
        .jmp_boot = {0xEB, 0x76, 0x90},
        .fs_name = {'E', 'X', 'F', 'A', 'T', ' ', ' ', ' '},
        .must_be_zero = {0},
        .partition_offset = HostToLe64(0ULL),
        .vol_length = HostToLe64(vol_length),
        .fat_offset = HostToLe32(static_cast<uint32_t>(fat_offset)),
        .fat_length = HostToLe32(static_cast<uint32_t>(fat_length)),
        .clu_offset = HostToLe32(static_cast<uint32_t>(clu_offset)),
        .clu_count = HostToLe32(static_cast<uint32_t>(clu_count)),
        .root_cluster = HostToLe32(root_first_cluster),
        .vol_serial = HostToLe32(kDefaultVolumeSerial),
        .fs_revision = HostToLe16(kExfatRevision100),
        .vol_flags = HostToLe16(0U),
        .sect_size_bits = static_cast<uint8_t>(kSectorSizeBits),
        .sect_per_clus_bits = sect_per_clus_bits,
        .num_fats = 1U,
        .drv_sel = kDriveSelect,
        .percent_in_use = static_cast<uint8_t>((used_clusters * 100ULL) / clu_count),
        .reserved = {0},
        .boot_code = {0},
        .signature = HostToLe16(kBootSignature),
    };
    ::memcpy(boot_region.data(), &boot_sector, sizeof(boot_sector));

    // Sectors 1..8: Extended Boot Sectors with 0xAA550000 signature at the end of each sector.
    const uint32_t exboot_sig_le = HostToLe32(kExBootSignature);
    for (size_t s = 1; s <= 8; ++s) {
        ::memcpy(boot_region.data() + (s + 1) * kSectorSize - sizeof(exboot_sig_le), &exboot_sig_le,
                 sizeof(exboot_sig_le));
    }

    // Sector 11: Boot Checksum over Sectors 0..10.
    uint32_t boot_chksum = ComputeChecksum32(boot_region.data(), kSectorSize, 0U,
                                             /*is_boot_sector=*/true);
    boot_chksum = ComputeChecksum32(boot_region.data() + kSectorSize, 10ULL * kSectorSize,
                                    boot_chksum, /*is_boot_sector=*/false);
    const uint32_t boot_chksum_le = HostToLe32(boot_chksum);
    uint8_t* chksum_sector = boot_region.data() + 11ULL * kSectorSize;
    for (size_t offset = 0; offset < kSectorSize; offset += sizeof(boot_chksum_le)) {
        ::memcpy(chksum_sector + offset, &boot_chksum_le, sizeof(boot_chksum_le));
    }

    // 2. Build populated prefix of FAT.
    std::vector<uint32_t> fat_entries(kReservedClusters + used_clusters, 0U);
    fat_entries[0] = HostToLe32(kFatMediaDescriptor);
    fat_entries[1] = HostToLe32(kFatEofCluster);
    auto write_fat_chain = [&fat_entries](uint32_t start_cluster, uint32_t count) {
        for (uint32_t i = 0; i + 1 < count; ++i) {
            fat_entries[start_cluster + i] = HostToLe32(start_cluster + i + 1U);
        }
        fat_entries[start_cluster + count - 1U] = HostToLe32(kFatEofCluster);
    };
    write_fat_chain(bitmap_first_cluster, bitmap_clusters);
    write_fat_chain(upcase_first_cluster, upcase_clusters);
    PopulateTreeFatChainsRecursive(root_node, &fat_entries);

    // 3. Build populated prefix of Allocation Bitmap.
    std::vector<uint8_t> bitmap_prefix((used_clusters + 7U) / 8U, 0U);
    for (uint32_t i = 0; i < used_clusters; ++i) {
        bitmap_prefix[i / 8U] |= static_cast<uint8_t>(1U << (i % 8U));
    }

    // 4. Build Up-case Table in little-endian byte order and compute its checksum.
    std::vector<uint8_t> upcase_buf(upcase_bytes, 0U);
    for (size_t i = 0; i < kDefaultUpcaseTable.size(); ++i) {
        const uint16_t val_le = HostToLe16(kDefaultUpcaseTable[i]);
        ::memcpy(upcase_buf.data() + i * sizeof(val_le), &val_le, sizeof(val_le));
    }
    const uint32_t upcase_chksum = ComputeChecksum32(upcase_buf.data(), upcase_buf.size());

    // 5. Build Root Directory entries (Volume Label, Allocation Bitmap, Up-case Table + children).
    ExfatVolumeLabelDentry label_dentry = {
        .type = kDentryTypeVolumeLabel,
        .char_count = static_cast<uint8_t>(volume_label.size()),
        .volume_label = {0},
        .reserved = {0},
    };
    for (size_t i = 0; i < volume_label.size(); ++i) {
        label_dentry.volume_label[i] = HostToLe16(static_cast<uint8_t>(volume_label[i]));
    }
    root_node.dir_entries.push_back(ExfatDentry{.volume_label = label_dentry});
    root_node.dir_entries.push_back(ExfatDentry{.bitmap = {
                                                    .type = kDentryTypeBitmap,
                                                    .flags = 0U,
                                                    .reserved = {0},
                                                    .start_clu = HostToLe32(bitmap_first_cluster),
                                                    .size = HostToLe64(bitmap_bytes),
                                                }});
    root_node.dir_entries.push_back(ExfatDentry{.upcase = {
                                                    .type = kDentryTypeUpcase,
                                                    .reserved1 = {0},
                                                    .checksum = HostToLe32(upcase_chksum),
                                                    .reserved2 = {0},
                                                    .start_clu = HostToLe32(upcase_first_cluster),
                                                    .size = HostToLe64(upcase_bytes),
                                                }});
    BuildDirectoryTablesRecursive(&root_node, cluster_size);

    // Write image atomically via a temporary file.
    fs::path tmp_image = image;
    tmp_image += ".tmp";

    absl::Cleanup rm_tmp_image = [&tmp_image] { android::base::file::rm(tmp_image).IgnoreError(); };

    std::ofstream ofs(tmp_image, std::ios_base::binary | std::ios_base::trunc);
    if (!ofs.good()) {
        std::move(rm_tmp_image).Cancel();
        return absl::InternalError(
                absl::StrFormat("Failed to open '%s' for writing exFAT image", tmp_image.string()));
    }

    // Write Main Boot Region (Sectors 0..11) and Backup Boot Region (Sectors 12..23).
    ofs.write(reinterpret_cast<const char*>(boot_region.data()),
              static_cast<std::streamsize>(boot_region.size()));
    ofs.write(reinterpret_cast<const char*>(boot_region.data()),
              static_cast<std::streamsize>(boot_region.size()));

    // Write FAT entries at fat_offset.
    ofs.seekp(static_cast<std::streamoff>(fat_offset * kSectorSize));
    ofs.write(reinterpret_cast<const char*>(fat_entries.data()),
              static_cast<std::streamsize>(fat_entries.size() * sizeof(uint32_t)));

    const auto cluster_to_offset = [clu_offset, cluster_size](uint32_t cluster) -> uint64_t {
        return clu_offset * kSectorSize + (cluster - kFirstDataCluster) * cluster_size;
    };

    // Write Allocation Bitmap at bitmap_first_cluster.
    ofs.seekp(static_cast<std::streamoff>(cluster_to_offset(bitmap_first_cluster)));
    ofs.write(reinterpret_cast<const char*>(bitmap_prefix.data()),
              static_cast<std::streamsize>(bitmap_prefix.size()));

    // Write Up-case Table at upcase_first_cluster.
    ofs.seekp(static_cast<std::streamoff>(cluster_to_offset(upcase_first_cluster)));
    ofs.write(reinterpret_cast<const char*>(upcase_buf.data()),
              static_cast<std::streamsize>(upcase_buf.size()));

    // Write Root Directory and all subdirectories/files.
    RETURN_IF_ERROR(WriteTreeDataRecursive(root_node, clu_offset, cluster_size, &ofs));

    ofs.close();
    if (!ofs.good()) {
        return absl::InternalError(
                absl::StrFormat("Failed to write exFAT metadata to '%s'", tmp_image.string()));
    }

    // Extend file to full requested capacity as a sparse file.
    std::error_code ec;
    fs::resize_file(tmp_image, total_bytes, ec);
    if (ec) {
        return absl::InternalError(
                absl::StrFormat("Failed to resize exFAT image '%s' to %u bytes: "
                                "%s",
                                tmp_image.string(), total_bytes, ec.message()));
    }

    return android::base::file::mv_file(tmp_image, image, /*fallback_to_copy_rm=*/false);
}

}  // namespace

bool IsExFatFileImage(const fs::path& path) {
    std::ifstream ifs(path, std::ios_base::binary);
    if (!ifs.good()) {
        return false;
    }

    ExfatBootSector bs = {};
    if (!ifs.read(reinterpret_cast<char*>(&bs), sizeof(bs))) {
        return false;
    }

    constexpr char kExfatFsName[] = {'E', 'X', 'F', 'A', 'T', ' ', ' ', ' '};
    static_assert(sizeof(kExfatFsName) == sizeof(bs.fs_name));

    return ::memcmp(bs.fs_name, kExfatFsName, sizeof(bs.fs_name)) == 0 &&
           bs.signature == HostToLe16(kBootSignature);
}

absl::Status CreateEmptyExFATImage(const fs::path& image, StorageCapacity size,
                                   std::string_view volume_label) {
    return CreateExFATImageImpl(image, /*source_directory=*/nullptr, size, volume_label);
}

absl::Status CreateExFATImageFromDirectory(const fs::path& image, const fs::path& source_directory,
                                           StorageCapacity size, std::string_view volume_label) {
    return CreateExFATImageImpl(image, &source_directory, size, volume_label);
}

}  // namespace goldfish::fsimage
