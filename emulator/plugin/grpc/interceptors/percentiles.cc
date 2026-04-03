/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "android/control/interceptor/percentiles.h"

#include <cmath>
#include <unordered_set>

#include "absl/log/check.h"

namespace android::control::interceptor {

Percentiles::Percentiles(int raw_data_size, std::initializer_list<double> targets)
        : max_raw_samples_(std::max<int>(raw_data_size, static_cast<int>(targets.size())))
        , target_percentiles_(targets.begin(), targets.end())
        , buckets_(targets.size() * 2 + 3) {
    DCHECK(max_raw_samples_ > 0);
    raw_samples_.reserve(max_raw_samples_);
    std::sort(target_percentiles_.begin(), target_percentiles_.end());
}

void Percentiles::AddSample(double val) {
    if (total_samples_ < max_raw_samples_) {
        DCHECK(static_cast<int>(raw_samples_.size()) == total_samples_);
        raw_samples_.push_back(val);
        ++total_samples_;
        return;
    }

    if (total_samples_ == max_raw_samples_) {
        CreateBucketsFromRawData();
    }

    DCHECK(buckets_.size() > 2);

    ++total_samples_;
    if (buckets_.front().value > val) {
        buckets_.front().value = val;
    } else if (buckets_.back().value < val) {
        buckets_.back().value = val;
    }

    for (size_t i = 1; i < buckets_.size() - 1; ++i) {
        buckets_[i].count_optimal += buckets_[i].p;
        if (buckets_[i].value > val) {
            ++buckets_[i].count;
        }
    }

    buckets_.back().count_optimal += buckets_.back().p;
    ++buckets_.back().count;

    InterpolateBuckets();
}

int Percentiles::TargetCount() const {
    return static_cast<int>(target_percentiles_.size());
}

int Percentiles::SamplesCount() const {
    return total_samples_;
}

bool Percentiles::IsBucketized() const {
    return total_samples_ > max_raw_samples_;
}

std::optional<double> Percentiles::Target(int index) const {
    if (index < 0 || index >= static_cast<int>(target_percentiles_.size())) {
        return {};
    }
    return target_percentiles_[index];
}

std::optional<double> Percentiles::CalcValueForTarget(double target) const {
    auto it = std::find(target_percentiles_.begin(), target_percentiles_.end(), target);
    if (it == target_percentiles_.end()) {
        return {};
    }
    return CalcValueForTargetAtIndex(
            static_cast<int>(std::distance(target_percentiles_.begin(), it)));
}

std::optional<double> Percentiles::CalcValueForTargetAtIndex(int target_index) const {
    if (target_index < 0 || target_index >= static_cast<int>(target_percentiles_.size())) {
        return {};
    }
    if (total_samples_ <= max_raw_samples_) {
        if (total_samples_ == 0) {
            return {};
        }
        // We need to sort if we are still in raw data mode.
        // Since this is a const method, we use a temporary sorted vector.
        std::vector<double> sorted = raw_samples_;
        std::sort(sorted.begin(), sorted.end());
        return sorted[static_cast<size_t>(total_samples_ * target_percentiles_[target_index])];
    }

    for (const auto& b : buckets_) {
        if (b.p == target_percentiles_[target_index]) {
            return b.value;
        }
    }
    return {};
}

void Percentiles::CreateBucketsFromRawData() {
    std::sort(raw_samples_.begin(), raw_samples_.end());

    // Min bucket
    buckets_[0] = Bucket(0.0, raw_samples_.front(), 0, static_cast<double>(max_raw_samples_));

    double last_p = 0.0;
    size_t bucket_idx = 1;
    for (double p : target_percentiles_) {
        // Intermediate bucket between last target and this target
        double mid_p = (last_p + p) / 2.0;
        size_t mid_pos = static_cast<size_t>(mid_p * max_raw_samples_);
        buckets_[bucket_idx++] = Bucket(mid_p, raw_samples_[mid_pos], static_cast<int64_t>(mid_pos),
                                        static_cast<double>(max_raw_samples_));

        // Bucket for this target percentile
        size_t pos = static_cast<size_t>(p * max_raw_samples_);
        buckets_[bucket_idx++] = Bucket(p, raw_samples_[pos], static_cast<int64_t>(pos),
                                        static_cast<double>(max_raw_samples_));
        last_p = p;
    }

    // Intermediate bucket between last target and Max
    double final_mid_p = (last_p + 1.0) / 2.0;
    size_t final_mid_pos = static_cast<size_t>(final_mid_p * max_raw_samples_);
    buckets_[bucket_idx++] =
            Bucket(final_mid_p, raw_samples_[final_mid_pos], static_cast<int64_t>(final_mid_pos),
                   static_cast<double>(max_raw_samples_));

    // Max bucket
    buckets_[bucket_idx] = Bucket(1.0, raw_samples_.back(), static_cast<int64_t>(max_raw_samples_),
                                  static_cast<double>(max_raw_samples_));

    DCHECK(bucket_idx + 1 == buckets_.size());

    raw_samples_.clear();
    raw_samples_.shrink_to_fit();
}

void Percentiles::InterpolateBuckets() {
    for (size_t i = 1; i < buckets_.size() - 1; ++i) {
        Bucket& b = buckets_[i];
        double delta = b.count_optimal - static_cast<double>(b.count);

        if ((delta >= 1.0 && (buckets_[i + 1].count - b.count) > 1) ||
            (delta <= -1.0 && (buckets_[i - 1].count - b.count) < -1)) {
            int direction = (delta >= 1.0) ? 1 : -1;
            UpdateBucket(&b, buckets_[i - 1], buckets_[i + 1], direction);
        }
    }
}

void Percentiles::UpdateBucket(Bucket* b, const Bucket& prev, const Bucket& next, int direction) {
    // Parabolic interpolation
    double n = static_cast<double>(b->count);
    double n_prev = static_cast<double>(prev.count);
    double n_next = static_cast<double>(next.count);
    double d = static_cast<double>(direction);

    double v_new = b->value + (d / (n_next - n_prev)) *
                                      (((n - n_prev + d) * (next.value - b->value) / (n_next - n)) +
                                       ((n_next - n - d) * (b->value - prev.value) / (n - n_prev)));

    // Ensure value is between neighbors, fallback to linear if not.
    if (v_new > prev.value && v_new < next.value) {
        b->value = v_new;
    } else {
        if (direction > 0) {
            b->value += (next.value - b->value) / (n_next - n);
        } else {
            b->value -= (prev.value - b->value) / (n_prev - n);
        }
    }
    b->count += direction;
}

}  // namespace android::control::interceptor
