// SPDX-License-Identifier: MIT
#include "nn/net.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gd::nn {
namespace {
constexpr uint32_t kMagic = 0x47444C32;  // "GDL2"
}

void Layer::init(int inDim, int outDim, Act a, float gain, Rng& rng) {
  in = inDim;
  out = outDim;
  act = a;
  w.assign(static_cast<size_t>(in) * out, 0.0f);
  b.assign(out, 0.0f);
  // Scaled-normal init (a cheap stand-in for orthogonal init that behaves
  // just as well for MLPs and keeps the file dependency-free).
  const float scale = gain / std::sqrt(static_cast<float>(in));
  for (float& v : w) v = rng.normal() * scale;
  gw.assign(w.size(), 0.0f);
  gb.assign(b.size(), 0.0f);
  mw.assign(w.size(), 0.0f);
  vw.assign(w.size(), 0.0f);
  mb.assign(b.size(), 0.0f);
  vb.assign(b.size(), 0.0f);
}

void Layer::zeroGrad() {
  std::fill(gw.begin(), gw.end(), 0.0f);
  std::fill(gb.begin(), gb.end(), 0.0f);
}

void Net::build(int obsDim, const std::vector<int>& hidden, int actions,
                uint64_t seed) {
  Rng rng(seed);
  obsDim_ = obsDim;
  actions_ = actions;
  layers_.clear();
  int prev = obsDim;
  for (int h : hidden) {
    Layer l;
    l.init(prev, h, Act::Tanh, std::sqrt(2.0f), rng);
    layers_.push_back(std::move(l));
    prev = h;
  }
  // Small policy head => near-uniform initial policy => healthy exploration.
  policyHead_.init(prev, actions, Act::None, 0.01f, rng);
  valueHead_.init(prev, 1, Act::None, 1.0f, rng);
  acts_.resize(layers_.size());
}

void softmax(const float* logits, int n, float* out) {
  float m = logits[0];
  for (int i = 1; i < n; ++i) m = std::max(m, logits[i]);
  float sum = 0;
  for (int i = 0; i < n; ++i) {
    out[i] = std::exp(logits[i] - m);
    sum += out[i];
  }
  const float inv = 1.0f / sum;
  for (int i = 0; i < n; ++i) out[i] *= inv;
}

namespace {
inline void dense(const float* x, int batch, const Layer& l, float* y) {
  for (int bi = 0; bi < batch; ++bi) {
    const float* xr = x + static_cast<size_t>(bi) * l.in;
    float* yr = y + static_cast<size_t>(bi) * l.out;
    for (int o = 0; o < l.out; ++o) {
      const float* wr = l.w.data() + static_cast<size_t>(o) * l.in;
      float s = l.b[o];
      for (int i = 0; i < l.in; ++i) s += wr[i] * xr[i];
      yr[o] = s;
    }
  }
  if (l.act == Act::Tanh) {
    const size_t n = static_cast<size_t>(batch) * l.out;
    for (size_t i = 0; i < n; ++i) y[i] = std::tanh(y[i]);
  } else if (l.act == Act::Relu) {
    const size_t n = static_cast<size_t>(batch) * l.out;
    for (size_t i = 0; i < n; ++i) y[i] = y[i] > 0 ? y[i] : 0.0f;
  }
}
}  // namespace

void Net::forward(const float* x, int batch, std::vector<float>* logits,
                  std::vector<float>* values, bool cache) {
  const float* cur = x;
  std::vector<float> scratchA, scratchB;
  for (size_t li = 0; li < layers_.size(); ++li) {
    const Layer& l = layers_[li];
    std::vector<float>& dst =
        cache ? acts_[li] : (li % 2 == 0 ? scratchA : scratchB);
    dst.assign(static_cast<size_t>(batch) * l.out, 0.0f);
    dense(cur, batch, l, dst.data());
    cur = dst.data();
  }
  logits->assign(static_cast<size_t>(batch) * actions_, 0.0f);
  dense(cur, batch, policyHead_, logits->data());
  values->assign(batch, 0.0f);
  dense(cur, batch, valueHead_, values->data());
}

void Net::backward(const float* x, int batch, const float* dLogits,
                   const float* dValues) {
  const int L = static_cast<int>(layers_.size());
  const float* trunkOut = L ? acts_[L - 1].data() : x;
  const int trunkDim = L ? layers_[L - 1].out : obsDim_;

  std::vector<float> dTrunk(static_cast<size_t>(batch) * trunkDim, 0.0f);

  auto headBackward = [&](Layer& h, const float* dOut, int outDim) {
    for (int bi = 0; bi < batch; ++bi) {
      const float* a = trunkOut + static_cast<size_t>(bi) * trunkDim;
      const float* d = dOut + static_cast<size_t>(bi) * outDim;
      float* dt = dTrunk.data() + static_cast<size_t>(bi) * trunkDim;
      for (int o = 0; o < outDim; ++o) {
        const float go = d[o];
        if (go == 0.0f) continue;
        h.gb[o] += go;
        float* gwr = h.gw.data() + static_cast<size_t>(o) * trunkDim;
        const float* wr = h.w.data() + static_cast<size_t>(o) * trunkDim;
        for (int i = 0; i < trunkDim; ++i) {
          gwr[i] += go * a[i];
          dt[i] += go * wr[i];
        }
      }
    }
  };
  headBackward(policyHead_, dLogits, actions_);
  headBackward(valueHead_, dValues, 1);

  std::vector<float> dCur = std::move(dTrunk);
  for (int li = L - 1; li >= 0; --li) {
    Layer& l = layers_[li];
    const float* a = acts_[li].data();
    // Through the activation.
    if (l.act == Act::Tanh) {
      const size_t n = static_cast<size_t>(batch) * l.out;
      for (size_t i = 0; i < n; ++i) dCur[i] *= (1.0f - a[i] * a[i]);
    } else if (l.act == Act::Relu) {
      const size_t n = static_cast<size_t>(batch) * l.out;
      for (size_t i = 0; i < n; ++i) dCur[i] *= (a[i] > 0 ? 1.0f : 0.0f);
    }
    const float* prevAct = li == 0 ? x : acts_[li - 1].data();
    const int prevDim = l.in;
    std::vector<float> dPrev(static_cast<size_t>(batch) * prevDim, 0.0f);
    for (int bi = 0; bi < batch; ++bi) {
      const float* d = dCur.data() + static_cast<size_t>(bi) * l.out;
      const float* p = prevAct + static_cast<size_t>(bi) * prevDim;
      float* dp = dPrev.data() + static_cast<size_t>(bi) * prevDim;
      for (int o = 0; o < l.out; ++o) {
        const float go = d[o];
        if (go == 0.0f) continue;
        l.gb[o] += go;
        float* gwr = l.gw.data() + static_cast<size_t>(o) * prevDim;
        const float* wr = l.w.data() + static_cast<size_t>(o) * prevDim;
        for (int i = 0; i < prevDim; ++i) {
          gwr[i] += go * p[i];
          dp[i] += go * wr[i];
        }
      }
    }
    dCur = std::move(dPrev);
  }
}

void Net::zeroGrad() {
  for (Layer& l : layers_) l.zeroGrad();
  policyHead_.zeroGrad();
  valueHead_.zeroGrad();
}

float Net::clipGradNorm(float maxNorm) {
  double sum = 0;
  auto acc = [&](const Layer& l) {
    for (float g : l.gw) sum += static_cast<double>(g) * g;
    for (float g : l.gb) sum += static_cast<double>(g) * g;
  };
  for (const Layer& l : layers_) acc(l);
  acc(policyHead_);
  acc(valueHead_);
  const float norm = static_cast<float>(std::sqrt(sum));
  if (norm > maxNorm && norm > 0) {
    const float s = maxNorm / norm;
    auto scale = [&](Layer& l) {
      for (float& g : l.gw) g *= s;
      for (float& g : l.gb) g *= s;
    };
    for (Layer& l : layers_) scale(l);
    scale(policyHead_);
    scale(valueHead_);
  }
  return norm;
}

void Net::adamStep(float lr, float beta1, float beta2, float eps) {
  adamT_++;
  const float bc1 = 1.0f - std::pow(beta1, static_cast<float>(adamT_));
  const float bc2 = 1.0f - std::pow(beta2, static_cast<float>(adamT_));
  auto upd = [&](std::vector<float>& p, std::vector<float>& g,
                 std::vector<float>& m, std::vector<float>& v) {
    for (size_t i = 0; i < p.size(); ++i) {
      m[i] = beta1 * m[i] + (1 - beta1) * g[i];
      v[i] = beta2 * v[i] + (1 - beta2) * g[i] * g[i];
      const float mh = m[i] / bc1;
      const float vh = v[i] / bc2;
      p[i] -= lr * mh / (std::sqrt(vh) + eps);
    }
  };
  auto stepLayer = [&](Layer& l) {
    upd(l.w, l.gw, l.mw, l.vw);
    upd(l.b, l.gb, l.mb, l.vb);
  };
  for (Layer& l : layers_) stepLayer(l);
  stepLayer(policyHead_);
  stepLayer(valueHead_);
}

int Net::paramCount() const {
  int n = 0;
  for (const Layer& l : layers_) n += static_cast<int>(l.w.size() + l.b.size());
  n += static_cast<int>(policyHead_.w.size() + policyHead_.b.size());
  n += static_cast<int>(valueHead_.w.size() + valueHead_.b.size());
  return n;
}

bool Net::save(const std::string& path) const {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const uint32_t magic = kMagic;
  const uint32_t nl = static_cast<uint32_t>(layers_.size());
  std::fwrite(&magic, 4, 1, f);
  std::fwrite(&obsDim_, 4, 1, f);
  std::fwrite(&actions_, 4, 1, f);
  std::fwrite(&nl, 4, 1, f);
  auto wr = [&](const Layer& l) {
    std::fwrite(&l.in, 4, 1, f);
    std::fwrite(&l.out, 4, 1, f);
    std::fwrite(l.w.data(), 4, l.w.size(), f);
    std::fwrite(l.b.data(), 4, l.b.size(), f);
  };
  for (const Layer& l : layers_) wr(l);
  wr(policyHead_);
  wr(valueHead_);
  std::fclose(f);
  return true;
}

bool Net::load(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  uint32_t magic = 0, nl = 0;
  if (std::fread(&magic, 4, 1, f) != 1 || magic != kMagic) {
    std::fclose(f);
    return false;
  }
  if (std::fread(&obsDim_, 4, 1, f) != 1) { std::fclose(f); return false; }
  if (std::fread(&actions_, 4, 1, f) != 1) { std::fclose(f); return false; }
  if (std::fread(&nl, 4, 1, f) != 1) { std::fclose(f); return false; }
  Rng rng(1);
  layers_.assign(nl, Layer());
  auto rd = [&](Layer& l) {
    int in = 0, out = 0;
    if (std::fread(&in, 4, 1, f) != 1) return false;
    if (std::fread(&out, 4, 1, f) != 1) return false;
    l.init(in, out, l.act, 1.0f, rng);
    if (std::fread(l.w.data(), 4, l.w.size(), f) != l.w.size()) return false;
    if (std::fread(l.b.data(), 4, l.b.size(), f) != l.b.size()) return false;
    return true;
  };
  bool ok = true;
  for (uint32_t i = 0; i < nl && ok; ++i) {
    layers_[i].act = Act::Tanh;
    ok = rd(layers_[i]);
  }
  policyHead_.act = Act::None;
  valueHead_.act = Act::None;
  ok = ok && rd(policyHead_) && rd(valueHead_);
  acts_.resize(layers_.size());
  std::fclose(f);
  return ok;
}

}  // namespace gd::nn
