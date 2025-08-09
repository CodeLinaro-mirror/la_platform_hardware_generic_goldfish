#include "bootconfig.h"

#include <stdio.h>

#include <memory>
#include <numeric>

#include "absl/log/log.h"

#include "android/base/file/file_io.h"

namespace goldfish {
using namespace std::literals;

constexpr std::string_view kBootconfigMagic = "#BOOTCONFIG\n"sv;
constexpr uint32_t kBootconfigAlign = 4;

std::pair<int, size_t> copyFile(FILE* src, FILE* dst) {
    size_t sz = 0;
    std::vector<char> buf(64 * 1024);

    while (true) {
        const size_t szR = ::fread(buf.data(), 1, buf.size(), src);
        if (!szR) {
            return {::ferror(src), sz};
        }

        const size_t szW = ::fwrite(buf.data(), 1, szR, dst);
        if (szR != szW) {
            return {::ferror(dst), sz};
        }

        sz += szR;
    }
}

void host2le32(const uint32_t v32, void* dst) {
    auto m8 = static_cast<uint8_t*>(dst);
    m8[0] = v32;
    m8[1] = v32 >> 8;
    m8[2] = v32 >> 16;
    m8[3] = v32 >> 24;
}

std::vector<char> flattenBootconfig(
        const std::vector<std::pair<std::string, std::string>>& bootconfig) {
    std::vector<char> bits;

    for (const auto& kv : bootconfig) {
        bits.insert(bits.end(), kv.first.begin(), kv.first.end());
        bits.push_back('=');
        bits.push_back('\"');
        bits.insert(bits.end(), kv.second.begin(), kv.second.end());
        bits.push_back('\"');
        bits.push_back('\n');
    }
    bits.push_back(0);  // it is ASCIIZ

    return bits;
}

int appendBootconfig(const size_t srcSize,
                     const std::vector<std::pair<std::string, std::string>>& bootconfig,
                     FILE* dst) {
    const std::vector<char> blob = buildBootconfigBlob(srcSize, bootconfig);

    if (blob.size() != ::fwrite(blob.data(), 1, blob.size(), dst)) {
        return ::ferror(dst);
    }

    return 0;
}

std::vector<char> buildBootconfigBlob(
        const size_t srcSize, const std::vector<std::pair<std::string, std::string>>& bootconfig) {
    std::vector<char> blob = flattenBootconfig(bootconfig);

    const size_t unaligend = (srcSize + blob.size()) % kBootconfigAlign;
    if (unaligend) {
        blob.insert(blob.end(), kBootconfigAlign - unaligend, '+');
    }

    const uint32_t csum = std::accumulate(
            blob.begin(), blob.end(), 0,
            [](const uint32_t z, const char c) { return z + static_cast<uint8_t>(c); });

    const size_t size = blob.size();

    blob.insert(blob.end(), 8, '+');  // size(u32, LE), csum(u32, LE)
    host2le32(size, &blob[blob.size() - 8]);
    host2le32(csum, &blob[blob.size() - 4]);

    blob.insert(blob.end(), kBootconfigMagic.begin(), kBootconfigMagic.end());

    return blob;
}

int createRamdiskWithBootconfig(const std::string &srcRamdiskPath, const std::string &dstRamdiskPath,
        const std::vector<std::pair<std::string, std::string>>& bootconfig) {
    struct FILE_deleter {
        void operator()(FILE* fp) const { ::fclose(fp); }
    };

    std::unique_ptr<FILE, FILE_deleter> srcRamdisk(android_fopen(srcRamdiskPath.c_str(), "rb"));
    if (!srcRamdisk) {
        LOG(ERROR) << " Can't open '" << srcRamdiskPath << "' for reading";
        return 1;
    }

    std::unique_ptr<FILE, FILE_deleter> dstRamdisk(android_fopen(dstRamdiskPath.c_str(), "wb"));
    if (!dstRamdisk) {
        LOG(ERROR) << ": Can't open '" << dstRamdiskPath << "' for writing";
        return 1;
    }

    const auto r = copyFile(srcRamdisk.get(), dstRamdisk.get());
    if (r.first) {
        LOG(ERROR) << "Error copying '" << srcRamdiskPath << "' into '" << dstRamdiskPath << "'";

        return r.first;
    }

    return appendBootconfig(r.second, bootconfig, dstRamdisk.get());
}
}  // namespace goldfish