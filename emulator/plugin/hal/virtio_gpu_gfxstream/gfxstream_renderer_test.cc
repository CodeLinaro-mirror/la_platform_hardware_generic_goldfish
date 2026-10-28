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

// The gfxstream_* compat layer (gfxstream_renderer.c) against the fake
// libgfxstream_backend: checks the rutabaga behaviours the layer owns
// (capset table, default context name, cmd_size % 4, empty transfers, ring 1
// fences, snapshot sub-directory, callback lifetime) rather than the renderer.

#include "gfxstream_renderer.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gmodule.h>

#ifndef _WIN32
#include <sys/uio.h>
#endif

#include <cstring>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "android/base/bazel_info.h"
#include "fake_gfxstream_backend.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"

namespace {

struct FenceRecord {
    uint64_t fence_id;
    uint32_t ring_idx;
    uint32_t flags;
};

std::vector<FenceRecord>* g_fences;
std::vector<std::string>* g_debug;

void RecordFence(uint64_t user_data, const struct gfxstream_fence* fence) {
    EXPECT_EQ(user_data, 0x1234U);
    g_fences->push_back({fence->fence_id, fence->ring_idx, fence->flags});
}

void RecordDebug(uint64_t user_data, const struct gfxstream_debug_ex* debug) {
    EXPECT_EQ(user_data, 0x1234U);
    g_debug->push_back(debug->message);
}

class GfxstreamRendererTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        // Point the loader at the fake library's runfiles directory.
        const char* rlocation = g_getenv("GFXSTREAM_FAKE_BACKEND_RLOCATION");
        ASSERT_NE(rlocation, nullptr);
        const std::string lib_path = android::base::Bazel::RunfilesPath(rlocation);
        ASSERT_TRUE(g_file_test(lib_path.c_str(), G_FILE_TEST_EXISTS)) << lib_path;
        char* lib_dir = g_path_get_dirname(lib_path.c_str());
        ASSERT_TRUE(g_setenv("ANDROID_EMULATOR_LIBRARY_DIR", lib_dir, TRUE));
        g_free(lib_dir);

        // Same file as the loader opens, so g_module_open() returns the same image.
        GModule* module = g_module_open(lib_path.c_str(), G_MODULE_BIND_LOCAL);
        ASSERT_NE(module, nullptr) << g_module_error();
        gpointer state_sym = nullptr;
        gpointer reset_sym = nullptr;
        ASSERT_TRUE(g_module_symbol(module, "fake_gfxstream_state", &state_sym));
        ASSERT_TRUE(g_module_symbol(module, "fake_gfxstream_reset", &reset_sym));
        state_ = static_cast<struct fake_gfxstream_state*>(state_sym);
        std::memcpy(&reset_, &reset_sym, sizeof(reset_));
        ASSERT_NE(state_, nullptr);
        ASSERT_NE(reset_, nullptr);
    }

    void SetUp() override {
        reset_();
        fences_.clear();
        debug_.clear();
        g_fences = &fences_;
        g_debug = &debug_;
        builder_ = {};
        builder_.user_data = 0x1234;
        builder_.capset_mask =
                (1ULL << GFXSTREAM_CAPSET_VULKAN) | (1ULL << GFXSTREAM_CAPSET_COMPOSER);
        builder_.wsi = GFXSTREAM_WSI_SURFACELESS;
        builder_.fence_cb = RecordFence;
        builder_.debug_cb_ex = RecordDebug;
        builder_.renderer_features = "A:enabled";
        builder_.display_width = 640;
        builder_.display_height = 480;
    }

    void TearDown() override {
        if (gfx_) {
            EXPECT_EQ(gfxstream_finish(&gfx_), 0);
            EXPECT_EQ(gfx_, nullptr);
        }
    }

    void Init() {
        char* error = nullptr;
        ASSERT_EQ(gfxstream_init(&builder_, &gfx_, &error), 0) << (error ? error : "");
        ASSERT_NE(gfx_, nullptr);
        EXPECT_EQ(error, nullptr);
    }

    static struct fake_gfxstream_state* state_;
    static void (*reset_)();
    struct gfxstream_builder builder_{};
    struct gfxstream* gfx_ = nullptr;
    std::vector<FenceRecord> fences_;
    std::vector<std::string> debug_;
};

struct fake_gfxstream_state* GfxstreamRendererTest::state_ = nullptr;
void (*GfxstreamRendererTest::reset_)() = nullptr;

TEST_F(GfxstreamRendererTest, InitPassesRutabagaEquivalentParams) {
    Init();
    EXPECT_EQ(state_->init_calls, 1);
    EXPECT_EQ(state_->renderer_flags,
              uint64_t{STREAM_RENDERER_FLAGS_USE_EGL_BIT | STREAM_RENDERER_FLAGS_USE_VK_BIT});
    EXPECT_EQ(state_->win0_width, 640U);
    EXPECT_EQ(state_->win0_height, 480U);
    EXPECT_STREQ(state_->renderer_features, "A:enabled");
    EXPECT_NE(state_->fence_callback, nullptr);
    EXPECT_NE(state_->debug_callback_ex, nullptr);
    EXPECT_NE(state_->user_data, 0U);
}

TEST_F(GfxstreamRendererTest, GlesCapsetAddsGlesFlagAndDefaultsWindowSize) {
    builder_.capset_mask = 1ULL << GFXSTREAM_CAPSET_GLES;
    builder_.display_width = 0;
    builder_.display_height = 0;
    Init();
    EXPECT_EQ(state_->renderer_flags,
              uint64_t{STREAM_RENDERER_FLAGS_USE_EGL_BIT | STREAM_RENDERER_FLAGS_USE_GLES_BIT});
    EXPECT_EQ(state_->win0_width, 1280U);
    EXPECT_EQ(state_->win0_height, 1024U);
}

TEST_F(GfxstreamRendererTest, InitRejectsEmptyCapsetMaskWithMessage) {
    builder_.capset_mask = 0;
    char* error = nullptr;
    EXPECT_EQ(gfxstream_init(&builder_, &gfx_, &error), -EINVAL);
    EXPECT_EQ(gfx_, nullptr);
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("gfxstream-vulkan"), std::string::npos) << error;
    g_free(error);
    EXPECT_EQ(state_->init_calls, 0);
}

TEST_F(GfxstreamRendererTest, InitRejectsSecondInstanceUntilFinished) {
    Init();
    struct gfxstream* second = nullptr;
    char* error = nullptr;
    EXPECT_EQ(gfxstream_init(&builder_, &second, &error), -EBUSY);
    EXPECT_EQ(second, nullptr);
    g_free(error);

    EXPECT_EQ(gfxstream_finish(&gfx_), 0);
    EXPECT_EQ(state_->teardown_calls, 1);
    Init();
    EXPECT_EQ(state_->init_calls, 2);
}

TEST_F(GfxstreamRendererTest, CapsetsEnumerateInRutabagaOrder) {
    builder_.capset_mask = (1ULL << GFXSTREAM_CAPSET_COMPOSER) | (1ULL << GFXSTREAM_CAPSET_GLES) |
                           (1ULL << GFXSTREAM_CAPSET_VULKAN);
    Init();
    uint32_t num = 0;
    EXPECT_EQ(gfxstream_get_num_capsets(gfx_, &num), 0);
    ASSERT_EQ(num, 3U);
    const uint32_t expected[] = {GFXSTREAM_CAPSET_VULKAN, GFXSTREAM_CAPSET_GLES,
                                 GFXSTREAM_CAPSET_COMPOSER};
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t id = 0, version = 99, size = 0;
        EXPECT_EQ(gfxstream_get_capset_info(gfx_, i, &id, &version, &size), 0);
        EXPECT_EQ(id, expected[i]);
        // The fake reports (7, 4 * set); the real renderer leaves the version
        // at the 0 the layer pre-sets.
        EXPECT_EQ(version, 7U);
        EXPECT_EQ(size, 4 * expected[i]);
    }
    uint32_t id = 0, version = 0, size = 0;
    EXPECT_EQ(gfxstream_get_capset_info(gfx_, 3, &id, &version, &size), -EINVAL);

    std::vector<uint8_t> caps(size_t{4} * GFXSTREAM_CAPSET_GLES);
    EXPECT_EQ(gfxstream_get_capset(gfx_, GFXSTREAM_CAPSET_GLES, 0, caps.data(),
                                   static_cast<uint32_t>(caps.size())),
              0);
    EXPECT_EQ(caps.back(), GFXSTREAM_CAPSET_GLES);
    EXPECT_EQ(gfxstream_get_capset(gfx_, GFXSTREAM_CAPSET_GLES, 0, caps.data(),
                                   static_cast<uint32_t>(caps.size() - 1)),
              -EINVAL);
}

TEST_F(GfxstreamRendererTest, ContextCreateDefaultsEmptyNameAndTrimsAtNul) {
    Init();
    EXPECT_EQ(gfxstream_context_create(gfx_, 7, 3, "", 0), 0);
    EXPECT_EQ(state_->context_create_calls, 1);
    EXPECT_EQ(state_->context_id, 7U);
    EXPECT_EQ(state_->context_init, 3U);
    EXPECT_STREQ(state_->context_name, "gpu_renderer");
    EXPECT_EQ(state_->context_name_len, 12U);

    const char padded[8] = {'a', 'b', 'c', '\0', '\0', '\0', '\0', '\0'};
    EXPECT_EQ(gfxstream_context_create(gfx_, 8, 0, padded, sizeof(padded)), 0);
    EXPECT_STREQ(state_->context_name, "abc");
    EXPECT_EQ(state_->context_name_len, 3U);
}

TEST_F(GfxstreamRendererTest, SubmitCommandRejectsSizesNotMultipleOfFour) {
    Init();
    uint8_t buf[12] = {};
    struct gfxstream_command cmd = {};
    cmd.ctx_id = 1;
    cmd.cmd = buf;
    cmd.cmd_size = 6;
    EXPECT_EQ(gfxstream_submit_command(gfx_, &cmd), -EINVAL);
    EXPECT_EQ(state_->submit_cmd_calls, 0);
    cmd.cmd_size = 8;
    EXPECT_EQ(gfxstream_submit_command(gfx_, &cmd), 0);
    EXPECT_EQ(state_->submit_cmd_calls, 1);
    EXPECT_EQ(state_->submit_cmd_size, 8U);
}

TEST_F(GfxstreamRendererTest, EmptyTransfersSucceedWithoutCallingTheRenderer) {
    Init();
    struct gfxstream_transfer transfer = {};
    transfer.w = 16;
    transfer.h = 0;
    transfer.d = 1;
    uint8_t pixels[64] = {};
    struct iovec iov = {pixels, sizeof(pixels)};
    EXPECT_EQ(gfxstream_resource_transfer_read(gfx_, 0, 1, &transfer, &iov), 0);
    EXPECT_EQ(gfxstream_resource_transfer_write(gfx_, 0, 1, &transfer), 0);
    EXPECT_EQ(state_->transfer_read_calls, 0);
    EXPECT_EQ(state_->transfer_write_calls, 0);

    transfer.h = 4;
    EXPECT_EQ(gfxstream_resource_transfer_read(gfx_, 0, 1, &transfer, &iov), 0);
    EXPECT_EQ(state_->transfer_read_calls, 1);
    EXPECT_EQ(state_->transfer_iovec_cnt, 1);
    EXPECT_EQ(state_->transfer_box_w, 16U);
    EXPECT_EQ(gfxstream_resource_transfer_write(gfx_, 0, 1, &transfer), 0);
    EXPECT_EQ(state_->transfer_write_calls, 1);
    EXPECT_EQ(state_->transfer_iovec_cnt, 0);
    EXPECT_EQ(gfxstream_resource_transfer_write_goldfish(gfx_, 0, 1, &transfer, &iov), 0);
    EXPECT_EQ(state_->transfer_write_calls, 2);
    EXPECT_EQ(state_->transfer_iovec_cnt, 1);
}

TEST_F(GfxstreamRendererTest, RingOneFencesCompleteSynchronously) {
    Init();
    struct gfxstream_fence fence = {};
    fence.flags = GFXSTREAM_FLAG_FENCE | GFXSTREAM_FLAG_INFO_RING_IDX;
    fence.fence_id = 42;
    fence.ctx_id = 1;
    fence.ring_idx = 1;
    EXPECT_EQ(gfxstream_create_fence(gfx_, &fence), 0);
    EXPECT_EQ(state_->create_fence_calls, 0);
    ASSERT_EQ(fences_.size(), 1U);
    EXPECT_EQ(fences_[0].fence_id, 42U);
    EXPECT_EQ(fences_[0].ring_idx, 1U);
    EXPECT_EQ(fences_[0].flags, fence.flags);

    // Any other ring, and ring 1 without INFO_RING_IDX, go to the renderer.
    fence.fence_id = 43;
    fence.ring_idx = 0;
    EXPECT_EQ(gfxstream_create_fence(gfx_, &fence), 0);
    EXPECT_EQ(state_->create_fence_calls, 1);
    EXPECT_EQ(state_->fence_id, 43U);
    fence.fence_id = 44;
    fence.ring_idx = 1;
    fence.flags = GFXSTREAM_FLAG_FENCE;
    EXPECT_EQ(gfxstream_create_fence(gfx_, &fence), 0);
    EXPECT_EQ(state_->create_fence_calls, 2);
    EXPECT_EQ(state_->fence_ring_idx, 1U);
    EXPECT_EQ(fences_.size(), 1U);
}

TEST_F(GfxstreamRendererTest, RendererCallbacksAreForwardedAndSurviveFinish) {
    Init();
    struct stream_renderer_fence srfence = {};
    srfence.fence_id = 5;
    srfence.ring_idx = 2;
    state_->fence_callback(reinterpret_cast<void*>(state_->user_data), &srfence);
    ASSERT_EQ(fences_.size(), 1U);
    EXPECT_EQ(fences_[0].fence_id, 5U);
    EXPECT_EQ(fences_[0].ring_idx, 2U);

    struct stream_renderer_debug_ex dbg = {};
    dbg.debug_type = STREAM_RENDERER_DEBUG_INFO;
    dbg.message = "hello";
    state_->debug_callback_ex(reinterpret_cast<void*>(state_->user_data), &dbg);
    ASSERT_EQ(debug_.size(), 1U);
    EXPECT_EQ(debug_[0], "hello");

    // gfxstream keeps the cookie in its global log callback after teardown:
    // calling back after gfxstream_finish() must be harmless.
    void* cookie = reinterpret_cast<void*>(state_->user_data);
    EXPECT_EQ(gfxstream_finish(&gfx_), 0);
    state_->fence_callback(cookie, &srfence);
    state_->debug_callback_ex(cookie, &dbg);
    EXPECT_EQ(fences_.size(), 1U);
    EXPECT_EQ(debug_.size(), 1U);
}

TEST_F(GfxstreamRendererTest, SnapshotAndRestoreUseTheGfxstreamSubdirectory) {
    Init();
    char* dir = g_dir_make_tmp("gfxstream_renderer_test_XXXXXX", nullptr);
    ASSERT_NE(dir, nullptr);
    char* subdir = g_build_filename(dir, "gfxstream", nullptr);
    EXPECT_FALSE(g_file_test(subdir, G_FILE_TEST_IS_DIR));

    EXPECT_EQ(gfxstream_snapshot(gfx_, dir), 0);
    EXPECT_EQ(state_->snapshot_calls, 1);
    EXPECT_STREQ(state_->snapshot_dir, subdir);
    EXPECT_TRUE(g_file_test(subdir, G_FILE_TEST_IS_DIR));

    EXPECT_EQ(gfxstream_restore(gfx_, dir), 0);
    EXPECT_EQ(state_->restore_calls, 1);
    EXPECT_STREQ(state_->restore_dir, subdir);

    // A directory that cannot be created is reported as -errno.
    char* file = g_build_filename(dir, "file", nullptr);
    ASSERT_TRUE(g_file_set_contents(file, "x", 1, nullptr));
    EXPECT_LT(gfxstream_snapshot(gfx_, file), 0);
    EXPECT_EQ(state_->snapshot_calls, 1);

    g_remove(file);
    g_rmdir(subdir);
    g_rmdir(dir);
    g_free(file);
    g_free(subdir);
    g_free(dir);
}

}  // namespace
