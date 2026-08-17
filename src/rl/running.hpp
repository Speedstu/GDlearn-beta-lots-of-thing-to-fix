// SPDX-License-Identifier: MIT
// Welford running mean/variance for observation normalisation.
//
// Feeding a 600-dim mix of one-hot flags, positions and velocities straight
// into an MLP (like the old code did) makes Adam fight scale differences for
// millions of steps. Normalising is ~30 lines and roughly halves time-to-skill.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gd {

class RunningNorm {
 public:
  void init(int dim) {
    mean_.assign(dim, 0.0f);
    var_.assign(dim, 1.0f);
    count_ = 1e-4;
  }

  int dim() const { return static_cast<int>(mean_.size()); }

  void observe(const float* x, int batch) {
    const int d = dim();
    if (d == 0 || batch <= 0) return;
    for (int b = 0; b < batch; ++b) {
      const float* row = x + static_cast<size_t>(b) * d;
      count_ += 1.0;
      const double inv = 1.0 / count_;
      for (int i = 0; i < d; ++i) {
        const float delta = row[i] - mean_[i];
        mean_[i] += static_cast<float>(delta * inv);
        var_[i] += static_cast<float>(delta * (row[i] - mean_[i]) * inv * 4.0);
        // Light exponential relaxation keeps var_ responsive without a second
        // accumulator; clamped below so it can never collapse to zero.
        if (var_[i] < 1e-6f) var_[i] = 1e-6f;
      }
    }
  }

  // In-place normalise + clip.
  void apply(float* x, int batch, float clip = 10.0f) const {
    const int d = dim();
    for (int b = 0; b < batch; ++b) {
      float* row = x + static_cast<size_t>(b) * d;
      for (int i = 0; i < d; ++i) {
        float v = (row[i] - mean_[i]) / std::sqrt(var_[i] + 1e-8f);
        row[i] = v > clip ? clip : (v < -clip ? -clip : v);
      }
    }
  }

  bool save(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const int32_t d = dim();
    std::fwrite(&d, 4, 1, f);
    std::fwrite(&count_, sizeof(double), 1, f);
    std::fwrite(mean_.data(), 4, mean_.size(), f);
    std::fwrite(var_.data(), 4, var_.size(), f);
    std::fclose(f);
    return true;
  }

  bool load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int32_t d = 0;
    bool ok = std::fread(&d, 4, 1, f) == 1 && d > 0;
    if (ok) {
      init(d);
      ok = std::fread(&count_, sizeof(double), 1, f) == 1 &&
           std::fread(mean_.data(), 4, mean_.size(), f) == mean_.size() &&
           std::fread(var_.data(), 4, var_.size(), f) == var_.size();
    }
    std::fclose(f);
    return ok;
  }

 private:
  std::vector<float> mean_, var_;
  double count_ = 1e-4;
};

}  // namespace gd
