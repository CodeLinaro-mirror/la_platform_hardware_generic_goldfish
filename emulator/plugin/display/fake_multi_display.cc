#include "goldfish/display/test/fake_multi_display.h"

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "goldfish/display/test/fake_pixman_display.h"

namespace goldfish::display::test {

FakeMultiDisplay* FakeMultiDisplay::s_instance = nullptr;

IMultiDisplay* FakeMultiDisplay::Instance() {
    return s_instance;
}

FakeMultiDisplay::FakeMultiDisplay(EventLoop* loop) : IMultiDisplay(loop) {
    s_instance = this;
    // Create the default display (ID 0)
    displays_[0] = ActiveFakePixmanDisplay::CreateShared(loop_, 0, 30, 640, 480);
}

absl::StatusOr<DisplayPtr> FakeMultiDisplay::CreateDisplay(DisplayId display_id, uint32_t width,
                                                           uint32_t height,
                                                           [[maybe_unused]] uint32_t dpi,
                                                           [[maybe_unused]] uint32_t flags) {
    if (displays_.count(display_id)) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Display with id %d already exists", display_id));
    }

    auto shared_display = ActiveFakePixmanDisplay::CreateShared(loop_, static_cast<int>(display_id),
                                                                30, static_cast<int>(width),
                                                                static_cast<int>(height));
    displays_[display_id] = shared_display;

    FireEvent(DisplayEvent{DisplayEvent::AddedEvent{shared_display}});
    return shared_display;
}

bool FakeMultiDisplay::IsEnabled() const {
    return enabled_;
}

absl::StatusOr<DisplayPtr> FakeMultiDisplay::GetDisplay(DisplayId display_id) const {
    auto it = displays_.find(display_id);
    if (it == displays_.end()) {
        return absl::NotFoundError(absl::StrFormat("Display with id %d not found", display_id));
    }
    return it->second;
}

absl::Status FakeMultiDisplay::EraseDisplay(DisplayId display_id) {
    if (display_id == 0) {
        return absl::InvalidArgumentError("Cannot erase the default display (ID 0)");
    }

    auto it = displays_.find(display_id);
    if (it == displays_.end()) {
        return absl::NotFoundError(absl::StrFormat("Display with id %d not found", display_id));
    }

    displays_.erase(it);
    FireEvent(DisplayEvent{DisplayEvent::DeletedEvent{display_id}});
    return absl::OkStatus();
}

std::vector<DisplayPtr> FakeMultiDisplay::Displays() const {
    std::vector<DisplayPtr> result;
    result.reserve(displays_.size());
    for (const auto& it : displays_) {
        result.push_back(it.second);
    }
    return result;
}

void FakeMultiDisplay::Clear() {
    // Clear all displays, except the default display (ID 0)
    for (auto it = displays_.begin(); it != displays_.end();) {
        if (it->first != 0) {
            it = displays_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace goldfish::display::test
