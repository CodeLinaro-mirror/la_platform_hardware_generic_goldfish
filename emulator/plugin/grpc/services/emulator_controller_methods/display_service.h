// Copyright (C) 2024 The Android Open Source Project
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

#pragma once

#include <grpcpp/grpcpp.h>

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"

#include "emulator_controller.grpc.pb.h"
#include "goldfish/display/QemuMultidisplay/multi_display.h"
#include "goldfish/eventing/with_callbacks.h"
#include "goldfish/sensors/physical_model.h"

namespace android {
namespace emulation {
namespace control {

using ::google::protobuf::Empty;
using grpc::ServerContext;
using grpc::Status;

/**
 * @brief Implements the display & multidisplay gRPC APIs.
 */
class DisplayServiceImpl : public EmulatorController::Service {
  public:
    DisplayServiceImpl(::goldfish::display::IMultiDisplay* display,
                       ::goldfish::sensors::PhysicalModel* pm);

    Status streamScreenshot(ServerContext* context, const ImageFormat* request,
                            grpc::ServerWriter<Image>* writer) override;

    Status getScreenshot(ServerContext* context, const ImageFormat* request, Image* reply) override;

    Status getDisplayConfigurations(ServerContext* context, const Empty* request,
                                    DisplayConfigurations* reply) override;
    Status setDisplayConfigurations(ServerContext* context, const DisplayConfigurations* request,
                                    DisplayConfigurations* reply) override;

    Status getDisplayMode(ServerContext* context, const Empty* request,
                          DisplayMode* reply) override;
    Status setDisplayMode(ServerContext* context, const DisplayMode* request,
                          Empty* reply) override;

    static Posture::PostureValue ToProtoPosture(::goldfish::sensors::FoldablePostures posture);

  private:
    static Status getDisplayConfigurations(const ::goldfish::display::IMultiDisplay& multiDisplay,
                                           DisplayConfigurations* reply);

    /**
     * @brief A callback type for allocating memory for an image.
     *
     * This AnyInvocable is responsible for providing a pointer to a buffer
     * where the image data can be written. It takes an Image protobuf object
     * (which may be modified to hold the buffer) and the required size in bytes.
     *
     * @return A pointer to the allocated memory, or an error status.
     */
    using MemoryAllocator = absl::AnyInvocable<absl::StatusOr<uint8_t*>(Image* img, size_t size)>;

    /**
     * @brief Creates an appropriate MemoryAllocator based on the request.
     *
     * Depending on whether the request specifies a side-channel (like MMAP)
     * or uses the standard gRPC protobuf-allocated string, this function
     * returns a suitable allocator.
     *
     * @param request The ImageFormat request containing transport details.
     * @param display The display whose dimensions are used for MMAP bounds checking.
     * @return A MemoryAllocator instance, or an error status if transport setup fails.
     */
    absl::StatusOr<MemoryAllocator> createAllocator(const ImageFormat& request,
                                                    const ::goldfish::display::IDisplay& display);

    Status getScreenshot(ServerContext* context, const ImageFormat* request, Image* reply,
                         MemoryAllocator& allocator);

    void fireDisplayConfigurationsChanged();

    ::goldfish::display::IMultiDisplay& mMultiDisplay;
    ::goldfish::sensors::PhysicalModel& mPhysicalModel;

    ::goldfish::sensors::FoldableModel::ObservablePosture::ScopedCallbackHandle
            mPostureSubscription;
    DisplayModeValue mCurrentDisplayMode{PHONE};
    mutable absl::Mutex mIsClosedMutex;
    bool mIsClosed ABSL_GUARDED_BY(mIsClosedMutex){false};
};

}  // namespace control
}  // namespace emulation
}  // namespace android
