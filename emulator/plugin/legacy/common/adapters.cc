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

#include "goldfish/legacy/common/adapters.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "absl/log/check.h"

namespace goldfish::legacy {
namespace {

class GuestMemoryAdapter final : public IGuestMemory {
  public:
    explicit GuestMemoryAdapter(const GoldfishGuestMemoryOps& ops) : ops_(ops) {}

    bool Read(uint64_t gpa, void* dst, size_t size) override {
        DCHECK(ops_.read != nullptr);
        return ops_.read(ops_.opaque, gpa, dst, size);
    }

    bool Write(uint64_t gpa, const void* src, size_t size) override {
        DCHECK(ops_.write != nullptr);
        return ops_.write(ops_.opaque, gpa, src, size);
    }

  private:
    GoldfishGuestMemoryOps ops_;
};

class Bar1Adapter final : public IBar1Mapper {
  public:
    explicit Bar1Adapter(const GoldfishBar1Ops& ops) : ops_(ops) {}

    uint64_t Size() const override { return ops_.size; }

    uint64_t HostPageSize() const override { return ops_.host_page_size; }

    uint64_t GuestPhysBase() const override {
        DCHECK(ops_.guest_phys_base != nullptr);
        return ops_.guest_phys_base(ops_.opaque);
    }

    bool Map(uint64_t offset, void* host_ptr, uint64_t size) override {
        DCHECK(ops_.map != nullptr);
        return ops_.map(ops_.opaque, offset, host_ptr, size);
    }

    void Unmap(uint64_t offset) override {
        DCHECK(ops_.unmap != nullptr);
        ops_.unmap(ops_.opaque, offset);
    }

    void UnmapAndRetire(uint64_t offset, OnRetiredCallback on_retired) override {
        DCHECK(ops_.unmap_and_retire != nullptr);
        auto* fn = new OnRetiredCallback(std::move(on_retired));
        ops_.unmap_and_retire(
                ops_.opaque, offset,
                +[](void* p) {
                    auto* f = static_cast<OnRetiredCallback*>(p);
                    if (*f) {
                        (*f)();
                    }
                    delete f;
                },
                fn);
    }

    void RunUnderBql(const std::function<void()>& fn) override {
        DCHECK(fn);
        DCHECK(ops_.run_locked != nullptr);
        struct Ctx {
            const std::function<void()>* fn;
        } ctx{&fn};
        ops_.run_locked(ops_.opaque, +[](void* p) { (*static_cast<Ctx*>(p)->fn)(); }, &ctx);
    }

  private:
    GoldfishBar1Ops ops_;
};

uint32_t MmioCoreRead(void* core, uint32_t offset) {
    DCHECK(core != nullptr);
    return static_cast<IMmioDevice*>(core)->Read(offset);
}

void MmioCoreWrite(void* core, uint32_t offset, uint32_t value) {
    DCHECK(core != nullptr);
    static_cast<IMmioDevice*>(core)->Write(offset, value);
}

void MmioCoreReset(void* core) {
    DCHECK(core != nullptr);
    static_cast<IMmioDevice*>(core)->Reset();
}

void MmioCoreDestroy(void* core) {
    DCHECK(core != nullptr);
    delete static_cast<IMmioDevice*>(core);
}

}  // namespace

std::unique_ptr<IGuestMemory> WrapGuestMemory(const GoldfishGuestMemoryOps& ops) {
    return std::make_unique<GuestMemoryAdapter>(ops);
}

SetIrqCallback WrapIrq(const GoldfishIrqOps& ops) {
    DCHECK(ops.set_level != nullptr);
    return [ops](bool level) { ops.set_level(ops.opaque, level); };
}

std::unique_ptr<IBar1Mapper> WrapBar1(const GoldfishBar1Ops& ops) {
    return std::make_unique<Bar1Adapter>(ops);
}

GoldfishMmioCore ToMmioCore(std::unique_ptr<IMmioDevice> device) {
    DCHECK(device != nullptr);
    GoldfishMmioCore result{};
    result.core = device.release();
    result.read = &MmioCoreRead;
    result.write = &MmioCoreWrite;
    result.reset = &MmioCoreReset;
    result.destroy = &MmioCoreDestroy;
    return result;
}

}  // namespace goldfish::legacy
