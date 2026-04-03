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

#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

namespace android::control::interceptor {

/**
 * @brief Percentiles class creates an estimation of the value at target percentiles
 * from a data stream.
 *
 * It is based on the P-square algorithm found at:
 * https://www.cse.wustl.edu/~jain/papers/ftp/psqr.pdf
 * with extensions to allow monitoring multiple percentiles.
 */
class Percentiles {
  public:
    /** @brief A single tracked bucket (bucket in P-square algorithm). */
    struct Bucket {
        Bucket() = default;
        Bucket(double p, double v, int64_t n, double n_optimal)
                : p(p), value(v), count(n), count_optimal(n_optimal) {}

        double p = 0;             /**< Target percentile for this bucket. */
        double value = 0;         /**< Current height/value of the bucket. */
        int64_t count = 0;        /**< Number of samples <= value. */
        double count_optimal = 0; /**< Optimal position of the bucket. */
    };

    /**
     * @brief Constructor.
     * @param raw_data_size number of samples before interpolating.
     * @param targets percentiles to monitor.
     *
     * Be aware that up to raw_data_size elements will not be bucketized
     * which can significantly increase the size of filled out protobuf messages.
     */
    explicit Percentiles(int raw_data_size, std::initializer_list<double> targets);

    /** @brief Adds a sample to the estimator, updates buckets if necessary. */
    void AddSample(double val);

    /** @brief Range versions to add multiple samples at once. */
    template <class Range>
    void AddSamples(const Range& range) {
        std::for_each(std::begin(range), std::end(range), [this](double val) { AddSample(val); });
    }

    template <class T>
    void AddSamples(const std::initializer_list<T>& range) {
        AddSamples<std::initializer_list<T>>(range);
    }

    /** @brief Returns the number of tracked percentiles. */
    int TargetCount() const;

    /** @brief Returns the number of samples collected. */
    int SamplesCount() const;

    /** @brief Returns true if the raw samples have been bucketized. */
    bool IsBucketized() const;

    const std::vector<double>& RawSamples() const { return raw_samples_; }
    const std::vector<Bucket>& Buckets() const { return buckets_; }

    /**
     * @brief Returns the value of tracked percentile #|index|, or empty optional if
     * |index| is out of range.
     */
    std::optional<double> Target(int index) const;

    /**
     * @brief Calculates the estimated value at a percentile |target| (first overload)
     * or target(|target_index|) (second overload). Returns empty optional if the
     * passed percentile is not being tracked, or if there is no samples added
     * to the object yet.
     */
    std::optional<double> CalcValueForTarget(double target) const;
    std::optional<double> CalcValueForTargetAtIndex(int target_index) const;

  private:
    void CreateBucketsFromRawData();
    void InterpolateBuckets();

    static void UpdateBucket(Bucket* b, const Bucket& prev, const Bucket& next, int direction);

    int max_raw_samples_;
    int total_samples_ = 0;
    std::vector<double> target_percentiles_;
    std::vector<Bucket> buckets_;
    std::vector<double> raw_samples_;
};

}  // namespace android::control::interceptor
