// Copyright 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// The loader caches its first outcome for the lifetime of the process, so the
// four scenarios run as separate cc_test targets (see BUILD.bazel) selected
// through GFXSTREAM_LOADER_TEST_SCENARIO:
//   "fake":       ANDROID_EMULATOR_LIBRARY_DIR is pointed at a non-ASCII UTF-8
//                 directory holding a copy of the fake libgfxstream_backend,
//                 exercising UTF-8 lookup, symbol resolution, and concurrent
//                 multi-threaded initialization via g_once_init_enter/leave.
//   "missing":    nothing to load anywhere; out_errno is ENOENT and the error
//                 names the library and the directory that was tried.
//   "corrupt":    a corrupt file exists at the candidate path; out_errno is EIO
//                 and the error reports the load failure.
//   "incomplete": the fake library omits a required symbol; out_errno is ENOSYS
//                 and the error names the missing symbol.

#include "gfxstream_backend_loader.h"

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "android/base/bazel_info.h"

namespace {

#if defined(_WIN32)
constexpr const char* kBackendLibraryName = "gfxstream_backend.dll";
#elif defined(__APPLE__)
constexpr const char* kBackendLibraryName = "libgfxstream_backend.dylib";
#else
constexpr const char* kBackendLibraryName = "libgfxstream_backend.so";
#endif

std::string Scenario() {
    const char* s = g_getenv("GFXSTREAM_LOADER_TEST_SCENARIO");
    return s ? s : "";
}

TEST(GfxstreamBackendLoaderTest, Fake) {
    if (Scenario() != "fake") {
        GTEST_SKIP() << "not the fake scenario";
    }
    EXPECT_EQ(gfxstream_backend_fns(), nullptr) << "nothing loaded before the first call";

    const char* rlocation = g_getenv("GFXSTREAM_FAKE_BACKEND_RLOCATION");
    ASSERT_NE(rlocation, nullptr);
    const std::string src_path = android::base::Bazel::RunfilesPath(rlocation);
    ASSERT_TRUE(g_file_test(src_path.c_str(), G_FILE_TEST_EXISTS)) << src_path;

    // Copy the fake library into a non-ASCII UTF-8 directory so Windows and
    // POSIX both exercise UTF-8 path loading end-to-end.
    char* utf8_dir = g_dir_make_tmp("gfxstream_loader_测试_🚀_é_XXXXXX", nullptr);
    ASSERT_NE(utf8_dir, nullptr);
    char* dst_path = g_build_filename(utf8_dir, kBackendLibraryName, nullptr);
    char* contents = nullptr;
    gsize length = 0;
    ASSERT_TRUE(g_file_get_contents(src_path.c_str(), &contents, &length, nullptr));
    ASSERT_TRUE(g_file_set_contents(dst_path, contents, static_cast<gssize>(length), nullptr));
    g_free(contents);
    ASSERT_TRUE(g_setenv("ANDROID_EMULATOR_LIBRARY_DIR", utf8_dir, TRUE));

    // Exercise concurrent initialization across 8 threads (g_once_init_enter/leave guarantee).
    constexpr int kNumThreads = 8;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<const struct gfxstream_backend_fns*> results(kNumThreads, nullptr);
    std::vector<int> errnos(kNumThreads, -1);
    std::vector<char*> errors(kNumThreads, nullptr);
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&, i]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            results[i] = gfxstream_backend_load(&errors[i], &errnos[i]);
        });
    }
    while (ready.load(std::memory_order_acquire) < kNumThreads) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& t : threads) {
        t.join();
    }

    const struct gfxstream_backend_fns* fns = results[0];
    ASSERT_NE(fns, nullptr) << (errors[0] ? errors[0] : "");
    for (int i = 0; i < kNumThreads; ++i) {
        EXPECT_EQ(results[i], fns) << "thread " << i;
        EXPECT_EQ(errnos[i], 0) << "thread " << i;
        EXPECT_EQ(errors[i], nullptr) << "thread " << i;
        g_free(errors[i]);
    }

    EXPECT_EQ(gfxstream_backend_fns(), fns);
    EXPECT_EQ(gfxstream_backend_load(nullptr, nullptr), fns) << "repeat call returns same table";

#define EXPECT_SLOT_RESOLVED(slot, ret, params, args) EXPECT_NE(fns->slot, nullptr) << #slot;
    GFXSTREAM_BACKEND_FOR_EACH_FN(EXPECT_SLOT_RESOLVED)
#undef EXPECT_SLOT_RESOLVED

    // The fake reports (7, 4 * set) from stream_renderer_get_cap_set.
    uint32_t ver = 0;
    uint32_t size = 0;
    fns->get_cap_set(3, &ver, &size);
    EXPECT_EQ(ver, 7U);
    EXPECT_EQ(size, 12U);
    EXPECT_EQ(fns->init(nullptr, 0), 0);

    // Best-effort cleanup (on Windows the mapped DLL remains locked until process exit).
    (void)g_remove(dst_path);
    (void)g_rmdir(utf8_dir);
    g_free(dst_path);
    g_free(utf8_dir);
}

TEST(GfxstreamBackendLoaderTest, Missing) {
    if (Scenario() != "missing") {
        GTEST_SKIP() << "not the missing scenario";
    }
    char* error = nullptr;
    int load_errno = 0;
    EXPECT_EQ(gfxstream_backend_load(&error, &load_errno), nullptr);
    EXPECT_EQ(load_errno, ENOENT);
    ASSERT_NE(error, nullptr);
    const std::string message(error);
    g_free(error);
    EXPECT_NE(message.find("gfxstream_backend"), std::string::npos) << message;
    EXPECT_NE(message.find("ANDROID_EMULATOR_LIBRARY_DIR=/nonexistent/测试_🚀_é"),
              std::string::npos)
            << message;
    EXPECT_EQ(gfxstream_backend_fns(), nullptr);

    // The failure is sticky and reported again without retrying.
    char* error2 = nullptr;
    int load_errno2 = 0;
    EXPECT_EQ(gfxstream_backend_load(&error2, &load_errno2), nullptr);
    EXPECT_EQ(load_errno2, ENOENT);
    ASSERT_NE(error2, nullptr);
    EXPECT_EQ(message, std::string(error2));
    g_free(error2);
}

TEST(GfxstreamBackendLoaderTest, Corrupt) {
    if (Scenario() != "corrupt") {
        GTEST_SKIP() << "not the corrupt scenario";
    }
    char* dir = g_dir_make_tmp("gfxstream_loader_corrupt_XXXXXX", nullptr);
    ASSERT_NE(dir, nullptr);
    char* bad_lib = g_build_filename(dir, kBackendLibraryName, nullptr);
    constexpr char kInvalidImage[] = "not a valid shared library image";
    ASSERT_TRUE(g_file_set_contents(bad_lib, kInvalidImage, sizeof(kInvalidImage) - 1, nullptr));
    ASSERT_TRUE(g_setenv("ANDROID_EMULATOR_LIBRARY_DIR", dir, TRUE));

    char* error = nullptr;
    int load_errno = 0;
    EXPECT_EQ(gfxstream_backend_load(&error, &load_errno), nullptr);
    EXPECT_EQ(load_errno, EIO);
    ASSERT_NE(error, nullptr);
    const std::string message(error);
    g_free(error);
    EXPECT_NE(message.find(kBackendLibraryName), std::string::npos) << message;
    EXPECT_EQ(gfxstream_backend_fns(), nullptr);

    (void)g_remove(bad_lib);
    (void)g_rmdir(dir);
    g_free(bad_lib);
    g_free(dir);
}

TEST(GfxstreamBackendLoaderTest, Incomplete) {
    if (Scenario() != "incomplete") {
        GTEST_SKIP() << "not the incomplete scenario";
    }
    const char* rlocation = g_getenv("GFXSTREAM_FAKE_BACKEND_RLOCATION");
    ASSERT_NE(rlocation, nullptr);
    const std::string lib_path = android::base::Bazel::RunfilesPath(rlocation);
    ASSERT_TRUE(g_file_test(lib_path.c_str(), G_FILE_TEST_EXISTS)) << lib_path;
    char* lib_dir = g_path_get_dirname(lib_path.c_str());
    ASSERT_TRUE(g_setenv("ANDROID_EMULATOR_LIBRARY_DIR", lib_dir, TRUE));
    g_free(lib_dir);

    char* error = nullptr;
    int load_errno = 0;
    EXPECT_EQ(gfxstream_backend_load(&error, &load_errno), nullptr);
    EXPECT_EQ(load_errno, ENOSYS);
    ASSERT_NE(error, nullptr);
    const std::string message(error);
    g_free(error);
    EXPECT_NE(message.find("missing required symbols"), std::string::npos) << message;
    EXPECT_NE(message.find("stream_renderer_create_fence"), std::string::npos) << message;
    EXPECT_EQ(gfxstream_backend_fns(), nullptr);
}

}  // namespace
