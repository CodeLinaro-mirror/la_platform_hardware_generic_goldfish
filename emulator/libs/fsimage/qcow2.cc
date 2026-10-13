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

#include "goldfish/fsimage/qcow2.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "qcow2_internal.h"

namespace goldfish::fsimage {
namespace {

static_assert(std::endian::native == std::endian::little,
              "CreateQcow2Overlay assumes a little-endian host");

constexpr uint32_t kQcowMagic = 0x514649FBU;  // 'Q', 'F', 'I', 0xFB
constexpr uint32_t kQcowVersion2 = 2U;
constexpr uint32_t kQcowVersion3 = 3U;
constexpr uint32_t kClusterBits = 16U;
constexpr uint64_t kClusterSize = 1ULL << kClusterBits;  // 64 KiB
constexpr uint32_t kRefcountOrder = 4U;                  // 16-bit refcounts
constexpr uint64_t kSectorSize = 512ULL;
constexpr size_t kMaxBackingFileLength = 1023U;
constexpr uint64_t kL2EntriesPerCluster = kClusterSize / sizeof(uint64_t);
constexpr uint64_t kBytesPerL1Entry = kClusterSize * kL2EntriesPerCluster;  // 512 MiB
constexpr uint64_t kMaxL1SizeBytes = 32ULL * 1024ULL * 1024ULL;             // QCOW_MAX_L1_SIZE
constexpr uint64_t kMaxVirtualSize =
        (kMaxL1SizeBytes / sizeof(uint64_t)) * kBytesPerL1Entry;  // 2 PiB
constexpr uint64_t kRefcountBlockEntries = kClusterSize / sizeof(uint16_t);
static_assert(3ULL + (kMaxL1SizeBytes / kClusterSize) <= kRefcountBlockEntries);

constexpr uint32_t kMaxSnapshots = 65536U;         // QCOW_MAX_SNAPSHOTS
constexpr uint32_t kMaxSnapshotExtraData = 1024U;  // QCOW_MAX_SNAPSHOT_EXTRA_DATA

constexpr uint32_t kExtMagicEnd = 0x00000000U;
constexpr uint32_t kExtMagicBackingFormat = 0xE2792ACAU;

constexpr uint16_t HostToBe16(uint16_t v) {
    return __builtin_bswap16(v);
}

constexpr uint32_t HostToBe32(uint32_t v) {
    return __builtin_bswap32(v);
}

constexpr uint64_t HostToBe64(uint64_t v) {
    return __builtin_bswap64(v);
}

constexpr uint16_t BeToHost16(uint16_t v) {
    return HostToBe16(v);
}

constexpr uint32_t BeToHost32(uint32_t v) {
    return HostToBe32(v);
}

constexpr uint64_t BeToHost64(uint64_t v) {
    return HostToBe64(v);
}

struct Qcow2Header {
    uint32_t magic;
    uint32_t version;
    uint64_t backing_file_offset;
    uint32_t backing_file_size;
    uint32_t cluster_bits;
    uint64_t size;
    uint32_t crypt_method;
    uint32_t l1_size;
    uint64_t l1_table_offset;
    uint64_t refcount_table_offset;
    uint32_t refcount_table_clusters;
    uint32_t nb_snapshots;
    uint64_t snapshots_offset;

    // Version 3 fields
    uint64_t incompatible_features;
    uint64_t compatible_features;
    uint64_t autoclear_features;
    uint32_t refcount_order;
    uint32_t header_length;
    uint8_t compression_type;
    uint8_t padding[7];
};
static_assert(sizeof(Qcow2Header) == 112);

struct Qcow2HeaderExtension {
    uint32_t magic;
    uint32_t len;
};
static_assert(sizeof(Qcow2HeaderExtension) == 8);

struct Qcow2SnapshotHeader {
    uint64_t l1_table_offset;
    uint32_t l1_size;
    uint16_t id_str_size;
    uint16_t name_size;
    uint32_t date_sec;
    uint32_t date_nsec;
    uint64_t vm_clock_nsec;
    uint32_t vm_state_size;
    uint32_t extra_data_size;
};
static_assert(sizeof(Qcow2SnapshotHeader) == 40);

struct Qcow2SnapshotExtraData {
    uint64_t vm_state_size_large;
    uint64_t disk_size;
    uint64_t icount;
};
static_assert(sizeof(Qcow2SnapshotExtraData) == 24);

absl::Status ReadQcow2Header(const fs::path& qcow2_path, std::ifstream* ifs, Qcow2Header* header) {
    if (!android::base::file::exists(qcow2_path)) {
        return absl::NotFoundError(
                absl::StrFormat("Image '%s' does not exist", qcow2_path.string()));
    }

    ifs->open(qcow2_path, std::ios_base::binary);
    if (!ifs->good()) {
        return absl::InternalError(
                absl::StrFormat("Failed to open image '%s'", qcow2_path.string()));
    }

    uint32_t magic_be = 0;
    if (!ifs->read(reinterpret_cast<char*>(&magic_be), sizeof(magic_be)) ||
        BeToHost32(magic_be) != kQcowMagic) {
        return absl::InvalidArgumentError(
                absl::StrFormat("'%s' is not a qcow2 image", qcow2_path.string()));
    }

    *header = {};
    ifs->seekg(0);
    constexpr size_t kQcow2V2HeaderSize = offsetof(Qcow2Header, incompatible_features);
    static_assert(kQcow2V2HeaderSize == 72);
    if (!ifs->read(reinterpret_cast<char*>(header), kQcow2V2HeaderSize)) {
        return absl::DataLossError(
                absl::StrFormat("Truncated qcow2 header in '%s'", qcow2_path.string()));
    }

    const uint32_t version = BeToHost32(header->version);
    if (version != kQcowVersion2 && version != kQcowVersion3) {
        return absl::InvalidArgumentError(absl::StrFormat("Unsupported qcow2 version %u in '%s'",
                                                          version, qcow2_path.string()));
    }
    return absl::OkStatus();
}

}  // namespace

namespace internal {

absl::StatusOr<std::optional<uint64_t>> ReadQcow2VirtualSize(const fs::path& qcow2_path) {
    std::ifstream ifs(qcow2_path, std::ios_base::binary);
    if (!ifs.good()) {
        return absl::InternalError(
                absl::StrFormat("Failed to open image '%s'", qcow2_path.string()));
    }

    uint32_t magic_be = 0;
    if (!ifs.read(reinterpret_cast<char*>(&magic_be), sizeof(magic_be)) ||
        BeToHost32(magic_be) != kQcowMagic) {
        return std::nullopt;
    }

    uint64_t size_be = 0;
    ifs.seekg(offsetof(Qcow2Header, size));
    if (!ifs.read(reinterpret_cast<char*>(&size_be), sizeof(size_be))) {
        return absl::DataLossError(
                absl::StrFormat("Truncated qcow2 header in '%s'", qcow2_path.string()));
    }
    return BeToHost64(size_be);
}

absl::Status CreateQcow2Overlay(const fs::path& qcow2_image, std::string_view backing_path,
                                std::string_view backing_format, const uint64_t raw_size) {
    if (backing_path.empty() != backing_format.empty()) {
        return absl::InvalidArgumentError(
                "Backing image path and format must both be empty or both be non-empty");
    } else if (backing_path.size() > kMaxBackingFileLength) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Backing image path '%s' exceeds maximum length (%u bytes)",
                                backing_path, kMaxBackingFileLength));
    } else if (raw_size > kMaxVirtualSize) {
        return absl::OutOfRangeError(absl::StrFormat(
                "Backing image '%s' virtual size (%u bytes) is too large", backing_path, raw_size));
    }

    const uint64_t virtual_size = (raw_size + (kSectorSize - 1ULL)) & ~(kSectorSize - 1ULL);
    const uint64_t l1_entries = (virtual_size + kBytesPerL1Entry - 1ULL) / kBytesPerL1Entry;
    const uint64_t l1_bytes = l1_entries * sizeof(uint64_t);
    const uint64_t l1_clusters = (l1_bytes + kClusterSize - 1ULL) / kClusterSize;
    const uint64_t total_clusters = 3ULL + l1_clusters;

    std::vector<uint8_t> cluster(kClusterSize, 0U);

    // Populate header extensions in Cluster 0 right after Qcow2Header:
    // 1. Backing format extension (QCOW2_EXT_MAGIC_BACKING_FORMAT) padded to 8 bytes (if present).
    // 2. End-of-extensions marker (QCOW2_EXT_MAGIC_END).
    size_t offset = sizeof(Qcow2Header);

    if (!backing_format.empty()) {
        const Qcow2HeaderExtension fmt_ext = {
            .magic = HostToBe32(kExtMagicBackingFormat),
            .len = HostToBe32(static_cast<uint32_t>(backing_format.size())),
        };
        ::memcpy(cluster.data() + offset, &fmt_ext, sizeof(fmt_ext));
        offset += sizeof(fmt_ext);
        ::memcpy(cluster.data() + offset, backing_format.data(), backing_format.size());
        offset += (backing_format.size() + 7U) & ~size_t{7U};
    }

    const Qcow2HeaderExtension end_ext = {
        .magic = HostToBe32(kExtMagicEnd),
        .len = HostToBe32(0U),
    };
    ::memcpy(cluster.data() + offset, &end_ext, sizeof(end_ext));
    offset += sizeof(end_ext);

    uint64_t backing_file_offset = 0;
    if (!backing_path.empty()) {
        backing_file_offset = offset;
        ::memcpy(cluster.data() + backing_file_offset, backing_path.data(), backing_path.size());
    }

    const Qcow2Header header = {
        .magic = HostToBe32(kQcowMagic),
        .version = HostToBe32(kQcowVersion3),
        .backing_file_offset = HostToBe64(backing_file_offset),
        .backing_file_size = HostToBe32(static_cast<uint32_t>(backing_path.size())),
        .cluster_bits = HostToBe32(kClusterBits),
        .size = HostToBe64(virtual_size),
        .crypt_method = HostToBe32(0U),
        .l1_size = HostToBe32(static_cast<uint32_t>(l1_entries)),
        .l1_table_offset = HostToBe64(l1_entries > 0 ? 3ULL * kClusterSize : 0ULL),
        .refcount_table_offset = HostToBe64(1ULL * kClusterSize),
        .refcount_table_clusters = HostToBe32(1U),
        .nb_snapshots = HostToBe32(0U),
        .snapshots_offset = HostToBe64(0ULL),
        .incompatible_features = HostToBe64(0ULL),
        .compatible_features = HostToBe64(0ULL),
        .autoclear_features = HostToBe64(0ULL),
        .refcount_order = HostToBe32(kRefcountOrder),
        .header_length = HostToBe32(sizeof(Qcow2Header)),
        .compression_type = 0U,
        .padding = {0U},
    };
    ::memcpy(cluster.data(), &header, sizeof(header));

    fs::path tmp_image = qcow2_image;
    tmp_image += ".tmp";

    absl::Cleanup rm_tmp_image = [&tmp_image] { android::base::file::rm(tmp_image).IgnoreError(); };

    std::ofstream ofs(tmp_image, std::ios_base::binary | std::ios_base::trunc);
    if (!ofs.good()) {
        std::move(rm_tmp_image).Cancel();
        return absl::InternalError(absl::StrFormat("Failed to open '%s' for writing qcow2 overlay",
                                                   tmp_image.string()));
    }

    // Write Cluster 0: Header + extensions + backing file path.
    ofs.write(reinterpret_cast<const char*>(cluster.data()),
              static_cast<std::streamsize>(cluster.size()));

    // Write Cluster 1: Refcount table pointing to Cluster 2.
    std::fill(cluster.begin(), cluster.end(), 0U);
    const uint64_t refcount_block_offset_be = HostToBe64(2ULL * kClusterSize);
    ::memcpy(cluster.data(), &refcount_block_offset_be, sizeof(refcount_block_offset_be));
    ofs.write(reinterpret_cast<const char*>(cluster.data()),
              static_cast<std::streamsize>(cluster.size()));

    // Write Cluster 2: Refcount block with refcount=1 for all allocated metadata clusters.
    std::fill(cluster.begin(), cluster.end(), 0U);
    const uint16_t refcount_one_be = HostToBe16(1U);
    for (uint64_t i = 0; i < total_clusters; ++i) {
        ::memcpy(cluster.data() + i * sizeof(refcount_one_be), &refcount_one_be,
                 sizeof(refcount_one_be));
    }
    ofs.write(reinterpret_cast<const char*>(cluster.data()),
              static_cast<std::streamsize>(cluster.size()));

    // Write Clusters 3 .. 3 + l1_clusters - 1: Zero-initialized L1 table.
    std::fill(cluster.begin(), cluster.end(), 0U);
    for (uint64_t i = 0; i < l1_clusters; ++i) {
        ofs.write(reinterpret_cast<const char*>(cluster.data()),
                  static_cast<std::streamsize>(cluster.size()));
    }

    ofs.close();
    if (!ofs.good()) {
        return absl::InternalError(
                absl::StrFormat("Failed to write qcow2 overlay to '%s'", tmp_image.string()));
    }

    return android::base::file::mv_file(tmp_image, qcow2_image, /*fallback_to_copy_rm=*/false);
}

}  // namespace internal

bool IsQcow2Path(const fs::path& path) {
    const auto size_or = internal::ReadQcow2VirtualSize(path);
    return size_or.ok() && size_or->has_value();
}

absl::Status CreateQcow2Overlay(const fs::path& qcow2_image, const fs::path& backing_image) {
    if (!android::base::file::exists(backing_image)) {
        return absl::NotFoundError(
                absl::StrFormat("Backing image '%s' does not exist", backing_image.string()));
    }

    ASSIGN_OR_RETURN(const std::optional<uint64_t> qcow2_size,
                     internal::ReadQcow2VirtualSize(backing_image));

    using std::string_view_literals::operator""sv;
    if (qcow2_size.has_value()) {
        return internal::CreateQcow2Overlay(qcow2_image, backing_image.string(), "qcow2"sv,
                                            *qcow2_size);
    } else {
        ASSIGN_OR_RETURN(const auto file_size, android::base::file::file_size(backing_image));
        return internal::CreateQcow2Overlay(qcow2_image, backing_image.string(), "raw"sv,
                                            file_size.Bytes());
    }
}

absl::Status CreateQcow2Overlay(const fs::path& qcow2_image,
                                const android::base::StorageCapacity raw_size) {
    using std::string_view_literals::operator""sv;
    return internal::CreateQcow2Overlay(qcow2_image, ""sv, ""sv, raw_size.Bytes());
}

absl::StatusOr<std::string> ReadBackingFileFromQcow2(const fs::path& qcow2_path) {
    std::ifstream ifs;
    Qcow2Header header = {};
    RETURN_IF_ERROR(ReadQcow2Header(qcow2_path, &ifs, &header));

    const uint64_t backing_file_offset = BeToHost64(header.backing_file_offset);
    const uint32_t backing_file_size = BeToHost32(header.backing_file_size);
    if (backing_file_offset == 0 || backing_file_size == 0) {
        return std::string{};
    }
    if (backing_file_size > kMaxBackingFileLength) {
        return absl::DataLossError(
                absl::StrFormat("Backing file size (%u) exceeds maximum (%u) in '%s'",
                                backing_file_size, kMaxBackingFileLength, qcow2_path.string()));
    }

    ifs.seekg(static_cast<std::streamoff>(backing_file_offset));
    std::string backing_file(backing_file_size, '\0');
    if (!ifs.read(backing_file.data(), static_cast<std::streamsize>(backing_file_size))) {
        return absl::DataLossError(
                absl::StrFormat("Truncated backing file path in '%s'", qcow2_path.string()));
    }
    return backing_file;
}

absl::StatusOr<std::vector<Qcow2Snapshot>> ListQcow2Snapshots(const fs::path& qcow2_path) {
    std::ifstream ifs;
    Qcow2Header header = {};
    RETURN_IF_ERROR(ReadQcow2Header(qcow2_path, &ifs, &header));

    const uint32_t nb_snapshots = BeToHost32(header.nb_snapshots);
    if (nb_snapshots == 0) {
        return std::vector<Qcow2Snapshot>{};
    }
    if (nb_snapshots > kMaxSnapshots) {
        return absl::DataLossError(absl::StrFormat("Too many snapshots (%u) in '%s'", nb_snapshots,
                                                   qcow2_path.string()));
    }

    const uint64_t snapshots_offset = BeToHost64(header.snapshots_offset);
    if (snapshots_offset == 0) {
        return absl::DataLossError(
                absl::StrFormat("Invalid snapshots offset in '%s'", qcow2_path.string()));
    }

    const uint64_t default_disk_size = BeToHost64(header.size);
    std::vector<Qcow2Snapshot> snapshots;
    snapshots.reserve(nb_snapshots);

    ifs.seekg(static_cast<std::streamoff>(snapshots_offset));
    for (uint32_t i = 0; i < nb_snapshots; ++i) {
        Qcow2SnapshotHeader snap_hdr = {};
        if (!ifs.read(reinterpret_cast<char*>(&snap_hdr), sizeof(snap_hdr))) {
            return absl::DataLossError(absl::StrFormat(
                    "Truncated snapshot header at index %u in '%s'", i, qcow2_path.string()));
        }

        const uint16_t id_str_size = BeToHost16(snap_hdr.id_str_size);
        const uint16_t name_size = BeToHost16(snap_hdr.name_size);
        const uint32_t date_sec = BeToHost32(snap_hdr.date_sec);
        const uint32_t date_nsec = BeToHost32(snap_hdr.date_nsec);
        const uint64_t vm_clock_nsec = BeToHost64(snap_hdr.vm_clock_nsec);
        const uint32_t extra_data_size = BeToHost32(snap_hdr.extra_data_size);

        if (extra_data_size > kMaxSnapshotExtraData) {
            return absl::DataLossError(absl::StrFormat(
                    "Snapshot %u extra data size (%u) exceeds maximum (%u) in '%s'", i,
                    extra_data_size, kMaxSnapshotExtraData, qcow2_path.string()));
        }

        uint64_t vm_state_size = BeToHost32(snap_hdr.vm_state_size);
        uint64_t disk_size = default_disk_size;
        std::optional<uint64_t> icount = std::nullopt;

        if (extra_data_size > 0) {
            Qcow2SnapshotExtraData extra = {};
            const size_t known_extra_size = std::min<size_t>(sizeof(extra), extra_data_size);
            if (!ifs.read(reinterpret_cast<char*>(&extra),
                          static_cast<std::streamsize>(known_extra_size))) {
                return absl::DataLossError(
                        absl::StrFormat("Truncated snapshot extra data at index %u in '%s'", i,
                                        qcow2_path.string()));
            }
            if (extra_data_size >= offsetof(Qcow2SnapshotExtraData, vm_state_size_large) +
                                           sizeof(extra.vm_state_size_large)) {
                vm_state_size = BeToHost64(extra.vm_state_size_large);
            }
            if (extra_data_size >=
                offsetof(Qcow2SnapshotExtraData, disk_size) + sizeof(extra.disk_size)) {
                disk_size = BeToHost64(extra.disk_size);
            }
            if (extra_data_size >=
                offsetof(Qcow2SnapshotExtraData, icount) + sizeof(extra.icount)) {
                const uint64_t raw_icount = BeToHost64(extra.icount);
                if (raw_icount != std::numeric_limits<uint64_t>::max()) {
                    icount = raw_icount;
                }
            }
            if (extra_data_size > known_extra_size) {
                const auto unknown_size =
                        static_cast<std::streamsize>(extra_data_size - known_extra_size);
                if (!ifs.ignore(unknown_size) || ifs.gcount() != unknown_size) {
                    return absl::DataLossError(
                            absl::StrFormat("Truncated snapshot extra data at index %u in '%s'", i,
                                            qcow2_path.string()));
                }
            }
        }

        std::string id(id_str_size, '\0');
        if (id_str_size > 0 && !ifs.read(id.data(), id_str_size)) {
            return absl::DataLossError(absl::StrFormat("Truncated snapshot ID at index %u in '%s'",
                                                       i, qcow2_path.string()));
        }

        std::string name(name_size, '\0');
        if (name_size > 0 && !ifs.read(name.data(), name_size)) {
            return absl::DataLossError(absl::StrFormat(
                    "Truncated snapshot name at index %u in '%s'", i, qcow2_path.string()));
        }

        const size_t entry_size = sizeof(snap_hdr) + extra_data_size + id_str_size + name_size;
        const size_t padding = ((entry_size + 7U) & ~size_t{7U}) - entry_size;
        ifs.ignore(static_cast<std::streamsize>(padding));

        snapshots.push_back(Qcow2Snapshot{
            .id = std::move(id),
            .name = std::move(name),
            .vm_state_size = vm_state_size,
            .disk_size = disk_size,
            .date = absl::FromUnixSeconds(date_sec) + absl::Nanoseconds(date_nsec),
            .vm_clock = absl::Nanoseconds(static_cast<int64_t>(
                    std::min<uint64_t>(vm_clock_nsec, std::numeric_limits<int64_t>::max()))),
            .icount = icount,
        });
    }

    return snapshots;
}

}  // namespace goldfish::fsimage
