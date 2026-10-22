/*
 * Copyright (C) 2017 The Android Open Source Project
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

/*
 * Defines the Virtual Scene Renderer, used by the Virtual Scene Camera
 */

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "VertexTypes.h"
#include "ver/virtual_environment_renderer_types.h"

namespace android {
namespace ver {

class Scene;
class ScenesManager;

struct Material {
    int id = -1;

    bool isValid() const { return id >= 0; }
    bool operator==(const Material& rhs) const { return id == rhs.id; }
    bool operator!=(const Material& rhs) const { return id != rhs.id; }
};

struct Mesh {
    int id = -1;

    bool isValid() const { return id >= 0; }
    bool operator==(const Mesh& rhs) const { return id == rhs.id; }
    bool operator!=(const Mesh& rhs) const { return id != rhs.id; }
};

struct Texture {
    int id = -1;

    bool isValid() const { return id >= 0; }
    bool operator==(const Texture& rhs) const { return id == rhs.id; }
    bool operator!=(const Texture& rhs) const { return id != rhs.id; }
};

struct Renderable {
    Material material;
    Mesh mesh;
    Texture texture;
};

struct RenderableObject {
    glm::mat4 modelViewProj;
    Renderable renderable;
};

class RendererContext {
  public:
    virtual ~RendererContext() = default;

    virtual bool isValid() const { return false; }
};

// Image scaling class to encapsulate different CPU side image scaling
// functionalities.
class ImageScaler {
  public:
    enum class ScaleMode {
        AspectFitLetterbox,  // Preserves aspect ratio by adding black bars.
        AspectFitZoom,       // Preserve aspect ratio, crops excess.
        ScaleToFill          // Ignore aspect ratio, stretch the image to fill.
    };

    ImageScaler(int width, int height, uint8_t* buffer);

    bool updateImage(int inputWidth, int inputHeight, const uint8_t* inputRgba, ScaleMode mode);

  private:
    int mFrameWidth;
    int mFrameHeight;
    uint8_t* mOutputRgba;

    // Helper functions for specific scaling logic
    // Returns libyuv status code (0 on success)
    int aspectFitLetterbox(int inputWidth, int inputHeight, const uint8_t* inputRgba);
    int aspectFitZoom(int inputWidth, int inputHeight, const uint8_t* inputRgba);
    int scaleToFill(int inputWidth, int inputHeight, const uint8_t* inputRgba);
};

class RendererView {
  public:
    // Updates render target related values, and re-creates the render target
    bool updateTarget(VerImageFormat format, int frameWidth, int frameHeight);

    // Updates positional values that do not require render target recreation
    void updateViewProjection(const glm::mat4& viewProj);

    void setBlurFactor(float factor);

    std::vector<uint8_t>& getFramebufferLocked() { return mCache.mFramebufferRGBA8; }

    VerImageFormat getFormatLocked() const { return mFormat; }
    int getWidthLocked() const { return mFrameWidth; }
    int getHeightLocked() const { return mFrameHeight; }

    // Some temporary utility functions for CPU side image processing
    static void applyBlurInPlaceCPU(int width, int height, uint8_t* rgbaDataInOut,
                                    int32_t* scratchBuffer, float sigma);

    bool isCacheValidFor(uint64_t sceneHash, uint64_t frameTime) const {
        std::lock_guard lock(mLock);
        return mCache.isValidFor(sceneHash, frameTime);
    }

  protected:
    friend class RendererImpl;
    friend class RendererVulkan;
    friend class ScenesManager;
    friend class VirtualSceneManager;

    mutable std::mutex mLock;
    int mFrameWidth = 0;
    int mFrameHeight = 0;
    VerImageFormat mFormat = VerImageFormat::RGBA8;

    glm::mat4 mViewProjection = glm::mat4(1.0f);
    float mBlurFactor = 0.0f;

    struct Cache {
        struct alignas(16) BlurScratchPixel {
            int32_t channels[4];
        };

        static const uint64_t INVALID_SCENE_HASH = ~0ULL;
        uint64_t mSceneHash = INVALID_SCENE_HASH;
        uint64_t mFrameTime = 0;
        std::vector<uint8_t> mFramebufferRGBA8;
        std::vector<BlurScratchPixel> mBlurScratchBuffer;

        Cache() = default;

        void invalidate() {
            mSceneHash = INVALID_SCENE_HASH;
            mFramebufferRGBA8.clear();
        }

        bool isValidFor(uint64_t sceneHash, uint64_t frameTime) const {
            if (sceneHash == INVALID_SCENE_HASH) {
                // Cannot cache with an invalid hash
                return false;
            }
            return (mSceneHash == sceneHash) && (mFramebufferRGBA8.size() > 0) &&
                   (frameTime == mFrameTime);
        }
    };

    Cache mCache;  // CPU side readback cache

    // preRender callback to be called before a rendering operation
    void preRenderLocked();

    // postRender callback to be called after a rendering operation
    void postRenderLocked();
};

class Renderer {
    Renderer(const Renderer& other) = delete;
    Renderer& operator=(const Renderer& other) = delete;

  protected:
    Renderer();

  public:
    virtual ~Renderer();

    // Create a virtual scene Renderer (Vulkan-only).
    //
    // Returns a Renderer instance if the renderer was successfully created or
    // null if there was an error.
    static std::unique_ptr<Renderer> create(const std::filesystem::path& vulkanBasePath);

    // Get the aspect ratio of the frame, w/h.
    virtual float getAspectRatio() = 0;

    // Determine if a texture has finished loading, for use with async texture
    // loads.
    virtual bool isTextureLoaded(Texture texture) = 0;

    // Get the dimensions of a texture and the generation of its contents. The
    // generation is unique for each upload of texture contents, so it changes
    // when an asynchronously loaded texture replaces its placeholder.
    //
    // Returns false if the texture could not be found.
    virtual bool getTextureInfo(Texture texture, uint32_t* outWidth, uint32_t* outHeight,
                                uint64_t* outGeneration) = 0;

    // Release a texture. If this is the last reference, the texture resources
    // will be released.
    virtual void releaseTexture(Texture texture) = 0;

    // Release a material.
    virtual void releaseMaterial(Material material) = 0;

    // Release a mesh.
    virtual void releaseMesh(Mesh mesh) = 0;

    // Create a standard material for rendering a test checkerboard texture.
    virtual Material createMaterialCheckerboard() = 0;

    // Create a standard material for rendering with a texture.
    virtual Material createMaterialTextured() = 0;

    // Create a material for rendering a screen-space effect with a custom
    // fragment shader, used by Effect.
    virtual Material createMaterialScreenSpace(const char* frag) = 0;

    // Helper method to create a mesh given std::vectors containing the vertices
    // and indices.
    Mesh createMesh(const std::vector<VertexPositionUV>& vertices,
                    const std::vector<uint32_t>& indices) {
        return createMesh(vertices.data(), vertices.size(), indices.data(), indices.size());
    }

    // Helper method to create a mesh given fixed-size arrays.
    template <size_t verticesSize, size_t indicesSize>
    Mesh createMesh(const VertexPositionUV (&vertices)[verticesSize],
                    const uint32_t (&indices)[indicesSize]) {
        return createMesh(vertices, verticesSize, indices, indicesSize);
    }

    // Create a mesh given a list of vertices and indices.
    virtual Mesh createMesh(const VertexPositionUV* vertices, size_t verticesSize,
                            const uint32_t* indices, size_t indicesSize) = 0;

    // Update the vertex buffer of an existing Mesh object.
    virtual bool updateMesh(Mesh mesh, const VertexPositionUV* vertices, size_t verticesSize) = 0;

    // Load an image from a file and create a Texture from it.
    virtual Texture loadTexture(const char* filename) = 0;

    // Load an image from a file and create a Texture from it asynchronously.
    virtual Texture loadTextureAsync(const char* filename) = 0;

    // Create a Texture directly from in-memory RGBA pixel data.
    virtual Texture createTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height) = 0;

    // Duplicate a Texture instance.
    virtual Texture duplicateTexture(Texture texture) = 0;

    // Render a frame.
    virtual bool render(RendererView* lockedView, const std::vector<RenderableObject>& renderables,
                        float time) = 0;

    // Make graphics context current for the scope.
    virtual std::unique_ptr<RendererContext> makeCurrent() = 0;
};

}  // namespace ver
}  // namespace android
