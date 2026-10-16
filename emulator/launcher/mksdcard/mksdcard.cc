// Copyright 2007 The Android Open Source Project
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of Google Inc. nor the names of its contributors may
//       be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY Google Inc. ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL Google Inc. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/status/status.h"

#include "goldfish/file/storage_capacity.h"
#include "goldfish/fsimage/exfat.h"

ABSL_FLAG(std::string, l, "", "The volume label of the disk image to create");
ABSL_FLAG(std::string, src, "", "Source directory to pre-populate the exFAT image from");

namespace {

constexpr std::string_view kUsageMessage =
        "mksdcard: create a blank or pre-populated exFAT image to be used with the "
        "Android emulator\n"
        "usage: mksdcard [-l label] [-src source_directory] <size> <file>\n\n"
        "  if <size> is a simple integer, it specifies a size in bytes\n"
        "  if <size> is an integer followed by 'K', it specifies a size in KiB\n"
        "  if <size> is an integer followed by 'M', it specifies a size in MiB\n"
        "  if <size> is an integer followed by 'G', it specifies a size in GiB\n"
        "  if <size> is 0 and -src is specified, the image is sized to fit the directory";

[[noreturn]] void Usage() {
    std::cerr << absl::ProgramUsageMessage() << "\n";
    std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage(kUsageMessage);
    const std::vector<char*> positional_args = absl::ParseCommandLine(argc, argv);
    if (positional_args.size() != 3) {
        Usage();
    }

    const std::string_view size_arg(positional_args[1]);
    const std::filesystem::path file_path(positional_args[2]);

    const auto capacity = android::base::StorageCapacity::Parse(size_arg);
    if (!capacity.ok()) {
        std::cerr << "Invalid argument size '" << size_arg << "': " << capacity.status() << "\n\n";
        Usage();
    }

    const std::string volume_label = absl::GetFlag(FLAGS_l);
    const std::string source_directory = absl::GetFlag(FLAGS_src);

    const absl::Status status =
            !source_directory.empty()
                    ? goldfish::fsimage::CreateExFATImageFromDirectory(file_path, source_directory,
                                                                       *capacity, volume_label)
                    : goldfish::fsimage::CreateEmptyExFATImage(file_path, *capacity, volume_label);
    if (!status.ok()) {
        std::cerr << "Failed to create exFAT image '" << file_path.string() << "': " << status
                  << "\n";
        return 1;
    }

    return 0;
}
