// SPDX-License-Identifier: MIT
// Minimal, dependency-free MLP with manual backprop + Adam.
//
// Why not LibTorch? A GD policy is a ~600->256->256->3 MLP evaluated tens of
// millions of times on tiny batches. The framework overhead dominates. This
// file is ~200 lines, has no install step, and is 3-5x faster in that regime.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/rng.hpp"

namespace gd::nn {

enum class Act : uint8_t { Tanh, Relu, None };

struct Layer {
  int in = 0, out = 0;
  Act act = Act::Tanh;
  std::vector<float> w, b;        // parameters, w is [out][in]
  std::vector<float> gw, gb;      // gradients
  std::vector<float> mw, vw, mb, vb;  // Adam moments

  void init(int inDim, int outDim, Act a, float gain, Rng& rng);
  void zeroGrad();
};

// Actor-critic with a shared trunk. Output layout: [logits..., value].
class Net {
 public:
  void build(int obsDim, const std::vector<int>& hidden, int actions,
             uint64_t seed);

  int obsDim() const { return obsDim_; }
  int actions() const { return actions_; }

  // Forward a batch. `x` is [batch][obsDim] row-major.
  // Fills logits [batch][actions] and values [batch].
  void forward(const float* x, int batch, std::vector<float>* logits,
               std::vector<float>* values, bool cache);

  // Backward from dLogits [batch][actions] and dValues [batch].
  // Requires the matching forward(cache = true).
  void backward(const float* x, int batch, const float* dLogits,
                const float* dValues);

  void zeroGrad();
  float clipGradNorm(float maxNorm);
  void adamStep(float lr, float beta1 = 0.9f, float beta2 = 0.999f,
                float eps = 1e-8f);

  int paramCount() const;
  bool save(const std::string& path) const;
  bool load(const std::string& path);

 private:
  std::vector<Layer> layers_;         // trunk
  Layer policyHead_, valueHead_;
  std::vector<std::vector<float>> acts_;  // cached trunk activations
  int obsDim_ = 0, actions_ = 0;
  int64_t adamT_ = 0;
};

// Numerically stable softmax over `n` logits, in place into `out`.
void softmax(const float* logits, int n, float* out);

}  // namespace gd::nn
