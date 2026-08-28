#pragma once

#include <functional>
#include <memory>
#include <utility>

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
// IWYU pragma: end_keep
// clang-format on
}
namespace goldfish::qemu {

using QemuCallbackFn = std::function<void()>;

/**
 * @class QEMUBHDeleter
 * @brief A custom deleter for QEMUBH unique_ptrs that manages C++ resources.
 *
 * @details This deleter manages the lifetime of a C++ callable alongside the
 * QEMU BH. It enables the `QEMUBHPtr` to be a uniform type, capable of handling
 * both simple C-style bottom halves and complex C++ callables that require
 * resource management.
 */
class QEMUBHDeleter {
  public:
    QEMUBHDeleter() = default;

    explicit QEMUBHDeleter(std::unique_ptr<QemuCallbackFn> func) : func_(std::move(func)) {}

    void operator()(QEMUBH* bh) const {
        if (bh) {
            qemu_bh_delete(bh);
        }
    }

    QEMUBHDeleter(QEMUBHDeleter&&) noexcept = default;
    QEMUBHDeleter& operator=(QEMUBHDeleter&&) = default;

    QEMUBHDeleter(const QEMUBHDeleter&) = delete;
    QEMUBHDeleter& operator=(const QEMUBHDeleter&) = delete;

  private:
    std::unique_ptr<QemuCallbackFn> func_;
};

/**
 * @brief A smart pointer that manages the lifetime of a QEMU Bottom Half (BH).
 *
 * This unique_ptr uses a custom deleter to ensure that `qemu_bh_delete` is
 * called when the pointer goes out of scope. It should be created using one of
 * the `make_qemu_bh` factory functions.
 */
using QEMUBHPtr = std::unique_ptr<QEMUBH, QEMUBHDeleter>;

/**
 * @brief Creates a QEMUBHPtr from a C-style function pointer.
 * @param cb The C-style callback function (`void (*func)(void*)`).
 * @param opaque A context pointer to pass to the callback.
 * @return A QEMUBHPtr that manages the created BH.
 */
inline QEMUBHPtr MakeQemuBh(QEMUBHFunc* cb, void* opaque) {
    return {qemu_bh_new(cb, opaque), QEMUBHDeleter()};
}

/**
 * @brief Creates a QEMUBHPtr from a C++ std::function.
 *
 * @details This factory function provides a C++ wrapper that automatically
 * manages the lifetime of the `std::function` object, which may be a lambda
 * with captures.
 *
 * @param cb The C++ callable to be scheduled. Ownership is moved into the BH.
 * @return A QEMUBHPtr that manages both the QEMU BH and the C++ callback.
 */
inline QEMUBHPtr MakeQemuBh(QemuCallbackFn cb) {
    auto func_ptr = std::make_unique<QemuCallbackFn>(std::move(cb));
    auto trampoline = [](void* opaque) { (*static_cast<QemuCallbackFn*>(opaque))(); };

    QEMUBH* bh = qemu_bh_new(trampoline, func_ptr.get());

    // The deleter takes ownership of the unique_ptr itself.
    return {bh, QEMUBHDeleter(std::move(func_ptr))};
}

}  // namespace goldfish::qemu