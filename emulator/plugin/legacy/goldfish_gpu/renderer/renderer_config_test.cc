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

#include "goldfish/legacy/goldfish_gpu/renderer/renderer_config.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"

#include "android/status/status_matcher_macros.h"
#include "gfxstream/host/features.h"

namespace goldfish::legacy {
namespace {

using ::absl_testing::StatusIs;

TEST(RendererConfigTest, RendererBackendDefaultsToLavapipe) {
    EXPECT_EQ(ParseRendererBackend(""), SELECTED_RENDERER_LAVAPIPE);
    EXPECT_EQ(ParseRendererBackend("lavapipe"), SELECTED_RENDERER_LAVAPIPE);
    EXPECT_EQ(ParseRendererBackend("vulkan"), SELECTED_RENDERER_LAVAPIPE);
}

TEST(RendererConfigTest, RendererBackendHostAndSwiftshaderFallBackToLavapipe) {
    EXPECT_EQ(ParseRendererBackend("host"), SELECTED_RENDERER_LAVAPIPE);
    EXPECT_EQ(ParseRendererBackend("on"), SELECTED_RENDERER_LAVAPIPE);
    EXPECT_EQ(ParseRendererBackend("swiftshader"), SELECTED_RENDERER_LAVAPIPE);
    EXPECT_EQ(ParseRendererBackend("swiftshader_indirect"), SELECTED_RENDERER_LAVAPIPE);
    EXPECT_EQ(ParseRendererBackend("no-such-renderer"), SELECTED_RENDERER_LAVAPIPE);
}

TEST(RendererConfigTest, RendererBackendAngleAndOff) {
    for (const char* v : {"swangle", "swangle_indirect", "angle", "angle_indirect"}) {
        EXPECT_EQ(ParseRendererBackend(v), SELECTED_RENDERER_ANGLE_INDIRECT) << v;
    }
    EXPECT_EQ(ParseRendererBackend("off"), SELECTED_RENDERER_OFF_DEPRECATED);
}

TEST(RendererConfigTest, FeatureSetGoldfishDefaults) {
    gfxstream::host::FeatureSet features;
    ASSERT_OK(BuildFeatureSet("", /*egl_on_egl=*/false, &features));
    EXPECT_TRUE(features.Vulkan.enabled());
    EXPECT_TRUE(features.HostComposition.enabled());
    EXPECT_FALSE(features.VulkanSnapshots.enabled());
    EXPECT_FALSE(features.GuestVulkanOnly.enabled());
    EXPECT_FALSE(features.VulkanNativeSwapchain.enabled());
    EXPECT_FALSE(features.GlDma.enabled());
    EXPECT_FALSE(features.GlDma2.enabled());
    EXPECT_TRUE(features.GlDirectMem.enabled());
    EXPECT_TRUE(features.HasSharedSlotsHostMemoryAllocator.enabled());
    EXPECT_TRUE(features.RefCountPipe.enabled());
    EXPECT_TRUE(features.GlesDynamicVersion.enabled());
    EXPECT_TRUE(features.Surfaceless.enabled());
    EXPECT_FALSE(features.EglOnEgl.enabled());
}

TEST(RendererConfigTest, FeatureSetAppliesOverridesAndEglOnEgl) {
    gfxstream::host::FeatureSet features;
    ASSERT_OK(BuildFeatureSet("GlAsyncSwap:disabled;RefCountPipe:disabled;YuvCache:enabled",
                              /*egl_on_egl=*/true, &features));
    EXPECT_FALSE(features.GlAsyncSwap.enabled());
    EXPECT_FALSE(features.RefCountPipe.enabled());
    EXPECT_TRUE(features.YuvCache.enabled());
    EXPECT_TRUE(features.Vulkan.enabled());
    EXPECT_FALSE(features.VulkanSnapshots.enabled());
    EXPECT_TRUE(features.EglOnEgl.enabled());
}

TEST(RendererConfigTest, FeatureSetPinsUnsupportedVulkanModesOff) {
    gfxstream::host::FeatureSet features;
    ASSERT_OK(BuildFeatureSet(
            "Vulkan:enabled,VulkanSnapshots:enabled;GuestVulkanOnly:enabled;"
            "VulkanNativeSwapchain:enabled",
            /*egl_on_egl=*/false, &features));
    EXPECT_TRUE(features.Vulkan.enabled());
    EXPECT_FALSE(features.VulkanSnapshots.enabled());
    EXPECT_FALSE(features.GuestVulkanOnly.enabled());
    EXPECT_FALSE(features.VulkanNativeSwapchain.enabled());

    gfxstream::host::FeatureSet no_vk_features;
    ASSERT_OK(BuildFeatureSet("Vulkan:disabled,VulkanSnapshots:enabled", /*egl_on_egl=*/false,
                              &no_vk_features));
    EXPECT_FALSE(no_vk_features.Vulkan.enabled());
    EXPECT_FALSE(no_vk_features.VulkanSnapshots.enabled());
}

TEST(RendererConfigTest, FeatureSetGatesVulkanBetweenApi31AndApi36Inclusive) {
    for (uint32_t api : {31U, 32U, 33U, 34U, 35U, 36U}) {
        gfxstream::host::FeatureSet features;
        ASSERT_OK(BuildFeatureSet("VulkanSnapshots:enabled", /*egl_on_egl=*/false, &features, api));
        EXPECT_TRUE(features.Vulkan.enabled()) << "api=" << api;
        EXPECT_FALSE(features.VulkanSnapshots.enabled()) << "api=" << api;
    }

    for (uint32_t api : {24U, 29U, 30U, 37U}) {
        gfxstream::host::FeatureSet defaults;
        ASSERT_OK(BuildFeatureSet("", /*egl_on_egl=*/false, &defaults, api));
        EXPECT_FALSE(defaults.Vulkan.enabled()) << "api=" << api;
        EXPECT_FALSE(defaults.VulkanSnapshots.enabled()) << "api=" << api;

        gfxstream::host::FeatureSet overridden;
        ASSERT_OK(BuildFeatureSet("Vulkan:enabled;VulkanSnapshots:enabled", /*egl_on_egl=*/false,
                                  &overridden, api));
        EXPECT_FALSE(overridden.Vulkan.enabled()) << "api=" << api;
        EXPECT_FALSE(overridden.VulkanSnapshots.enabled()) << "api=" << api;
    }
}

TEST(RendererConfigTest, FeatureSetRejectsMalformedEntry) {
    gfxstream::host::FeatureSet features;
    EXPECT_THAT(BuildFeatureSet("NotAFeature:enabled", false, &features),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(BuildFeatureSet("GlAsyncSwap", false, &features),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RendererConfigTest, VerifyModeParsesAliasesAndKeepsDefaultOnUnknown) {
    const auto def = GoldfishGpuVerifyMode::kEnforce;
    EXPECT_EQ(ParseVerifyMode("", def), def);
    EXPECT_EQ(ParseVerifyMode("garbage", def), def);
    for (const char* v : {"0", "off", "false", "none", "OFF"}) {
        EXPECT_EQ(ParseVerifyMode(v, def), GoldfishGpuVerifyMode::kOff) << v;
    }
    for (const char* v : {"1", "log", "warn"}) {
        EXPECT_EQ(ParseVerifyMode(v, def), GoldfishGpuVerifyMode::kLog) << v;
    }
    for (const char* v : {"2", "enforce", "error"}) {
        EXPECT_EQ(ParseVerifyMode(v, GoldfishGpuVerifyMode::kLog), GoldfishGpuVerifyMode::kEnforce)
                << v;
    }
    for (const char* v : {"3", "strict", "abort", "fatal"}) {
        EXPECT_EQ(ParseVerifyMode(v, def), GoldfishGpuVerifyMode::kStrict) << v;
    }
}

TEST(RendererConfigTest, VerifyModeNames) {
    EXPECT_EQ(VerifyModeName(GoldfishGpuVerifyMode::kOff), "OFF");
    EXPECT_EQ(VerifyModeName(GoldfishGpuVerifyMode::kLog), "LOG");
    EXPECT_EQ(VerifyModeName(GoldfishGpuVerifyMode::kEnforce), "ENFORCE");
    EXPECT_EQ(VerifyModeName(GoldfishGpuVerifyMode::kStrict), "STRICT");
}

}  // namespace
}  // namespace goldfish::legacy
