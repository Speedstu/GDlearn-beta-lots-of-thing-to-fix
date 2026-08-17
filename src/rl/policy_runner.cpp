// SPDX-License-Identifier: MIT
#include "rl/policy_runner.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "core/physics.hpp"
#include "env/obs.hpp"

namespace gd {
namespace fs = std::filesystem;

bool writePolicySchema(const std::string& dir, const std::string& magic,
                       int obsDimension) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return false;
  std::ofstream meta(dir + "/schema.txt", std::ios::trunc);
  if (!meta) return false;
  meta << magic << '\n';
  meta << "tps " << phys::TPS << '\n';
  meta << "obs_dim " << obsDimension << '\n';
  return static_cast<bool>(meta);
}

bool PolicyRunner::load(const std::string& dir, std::string* error) {
  auto fail = [&](const std::string& msg) {
    if (error) *error = msg;
    return false;
  };

  std::ifstream meta(dir + "/schema.txt");
  if (!meta) return fail("missing schema.txt");
  std::getline(meta, schemaMagic_);
  if (schemaMagic_ != "gdlearn_policy_v2" &&
      schemaMagic_ != "gdlearn_imitation_v2")
    return fail("unsupported policy schema '" + schemaMagic_ + "'");

  int tps = -1, dim = -1;
  std::string key;
  while (meta >> key) {
    if (key == "tps") meta >> tps;
    else if (key == "obs_dim") meta >> dim;
    else {
      std::string ignored;
      std::getline(meta, ignored);
    }
  }
  if (tps != phys::TPS)
    return fail("checkpoint TPS does not match native simulator TPS");
  if (dim != obsDim())
    return fail("checkpoint observation schema does not match this build");

  if (!net_.load(dir + "/policy.bin"))
    return fail("cannot load policy.bin");
  if (!norm_.load(dir + "/obs_norm.bin"))
    return fail("cannot load obs_norm.bin");
  if (norm_.dim() != dim || net_.obsDim() != dim)
    return fail("checkpoint tensors disagree with schema.txt");
  if (net_.actions() != 2)
    return fail("checkpoint policy head is not binary hold/release");

  obsDim_ = dim;
  return true;
}

void PolicyRunner::probabilities(const Level& level,
                                 const std::vector<State>& states,
                                 std::vector<float>* pHold) {
  pHold->assign(states.size(), 0.5f);
  if (states.empty() || obsDim_ <= 0) return;

  obs_.assign(states.size() * static_cast<size_t>(obsDim_), 0.0f);
  Sim sim(&level);
  for (size_t i = 0; i < states.size(); ++i) {
    sim.restore(states[i]);
    encodeObs(sim, obs_.data() + i * static_cast<size_t>(obsDim_));
  }
  norm_.apply(obs_.data(), static_cast<int>(states.size()));
  net_.forward(obs_.data(), static_cast<int>(states.size()), &logits_, &values_,
               false);

  float probs[2];
  for (size_t i = 0; i < states.size(); ++i) {
    nn::softmax(logits_.data() + i * 2, 2, probs);
    (*pHold)[i] = std::clamp(probs[1], 1e-6f, 1.0f - 1e-6f);
  }
}

bool PolicyRunner::action(const Level& level, const State& state, float* pHold) {
  std::vector<State> one{state};
  std::vector<float> p;
  probabilities(level, one, &p);
  const float ph = p.empty() ? 0.5f : p[0];
  if (pHold) *pHold = ph;
  return ph > 0.5f;
}

}  // namespace gd
