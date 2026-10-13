/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "ver/virtual_environment_renderer_types.h"

namespace android {
namespace ver {

class Scene;
class RendererView;

/**
 * @brief Singleton-like manager for all virtual scenes and views.
 */
class ScenesManager {
  public:
    static void setSearchPaths(const std::vector<std::filesystem::path>& resourceSearchPaths);

    static void setVulkanBasePath(const std::filesystem::path& vulkanBasePath);

    static bool configArgumentFileExists(const SceneConfig& config);

    static Scene* createScene(const SceneConfig& config);

    static bool renderView(Scene* scene, RendererView* view, std::function<void()> finishCallback,
                           uint64_t* outFrameTime);

    static bool removeScene(Scene* scene);

    static RendererView* createView();
    static bool removeView(RendererView* viewPtr);

    static bool removeAll();

  private:
    static std::mutex mScenesLock;
    static std::vector<std::unique_ptr<Scene>> mScenes;
    static std::vector<std::filesystem::path> mResourceSearchPaths;
    static std::filesystem::path mVulkanBasePath;

    static std::mutex mViewsLock;
    static std::vector<std::unique_ptr<RendererView>> mViews;
};

}  // namespace ver
}  // namespace android
