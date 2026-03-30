#pragma once

#include <vector>
#include <random>
#include <cmath>
#include <fstream>
#include <string>
#include <algorithm>
#include <numeric>
#include <cassert>
#include <cstring>

// ============================================================================
// Standalone C++ Neural Network (no PyTorch/TensorFlow dependency)
// Supports: Dense layers, ReLU/Tanh activations, softmax output
// Used for both policy (actor) and value (critic) networks in PPO.
// ============================================================================

// Simple matrix = vector<float> with shape info
struct Matrix {
    std::vector<float> data;
    int rows = 0;
    int cols = 0;

    Matrix() = default;
    Matrix(int r, int c) : rows(r), cols(c), data(r * c, 0.0f) {}

    float& at(int r, int c) { return data[r * cols + c]; }
    float at(int r, int c) const { return data[r * cols + c]; }
    int size() const { return rows * cols; }

    void randomInit(std::mt19937& rng, float scale) {
        std::normal_distribution<float> dist(0.0f, scale);
        for (auto& v : data) v = dist(rng);
    }

    void zero() { std::fill(data.begin(), data.end(), 0.0f); }
};

// Dense layer
struct DenseLayer {
    Matrix weights;    // [out_features x in_features]
    std::vector<float> bias;
    int inFeatures = 0;
    int outFeatures = 0;

    // Gradients
    Matrix weightGrad;
    std::vector<float> biasGrad;

    // Adam optimizer state
    Matrix weightM, weightV;
    std::vector<float> biasM, biasV;

    void init(int in, int out, std::mt19937& rng) {
        inFeatures = in;
        outFeatures = out;

        // He initialization
        float scale = std::sqrt(2.0f / in);
        weights = Matrix(out, in);
        weights.randomInit(rng, scale);
        bias.assign(out, 0.0f);

        weightGrad = Matrix(out, in);
        biasGrad.assign(out, 0.0f);

        weightM = Matrix(out, in);
        weightV = Matrix(out, in);
        biasM.assign(out, 0.0f);
        biasV.assign(out, 0.0f);
    }

    // Original forward (allocates — used by backprop path)
    std::vector<float> forward(const std::vector<float>& input) const {
        assert((int)input.size() == inFeatures);
        std::vector<float> output(outFeatures, 0.0f);
        forwardInto(input.data(), output.data());
        return output;
    }

    // Zero-alloc forward: write into pre-allocated buffer
    void forwardInto(const float* input, float* output) const {
        const float* w = weights.data.data();
        for (int o = 0; o < outFeatures; o++) {
            float sum = bias[o];
            const float* row = w + o * inFeatures;
            for (int i = 0; i < inFeatures; i++) {
                sum += row[i] * input[i];
            }
            output[o] = sum;
        }
    }

    void zeroGrad() {
        weightGrad.zero();
        std::fill(biasGrad.begin(), biasGrad.end(), 0.0f);
    }
};

// Full neural network
class NeuralNet {
public:
    NeuralNet() = default;

    void init(const std::vector<int>& layerSizes, unsigned int seed = 42) {
        rng_.seed(seed);
        layers_.clear();
        layerSizes_ = layerSizes;

        for (size_t i = 0; i + 1 < layerSizes.size(); i++) {
            DenseLayer layer;
            layer.init(layerSizes[i], layerSizes[i + 1], rng_);
            layers_.push_back(std::move(layer));
        }

        paramCount_ = 0;
        for (const auto& l : layers_) {
            paramCount_ += l.weights.size() + (int)l.bias.size();
        }
    }

    // Forward pass with ReLU activations, no activation on last layer
    std::vector<float> forward(const std::vector<float>& input) {
        std::vector<float> x = input;
        activations_.clear();
        activations_.push_back(x); // input

        for (size_t i = 0; i < layers_.size(); i++) {
            x = layers_[i].forward(x);

            // ReLU on all hidden layers, raw output on last
            if (i < layers_.size() - 1) {
                for (auto& v : x) v = std::max(0.0f, v);
            }
            activations_.push_back(x);
        }
        return x;
    }

    // Fast forward: zero-allocation inference using pre-allocated buffers
    // Call initBuffers() once after init/load, then use forwardFast() in hot loop
    void initBuffers() {
        if (layers_.empty()) return;
        int maxSize = 0;
        for (const auto& l : layers_) {
            maxSize = std::max(maxSize, std::max(l.inFeatures, l.outFeatures));
        }
        buf0_.resize(maxSize);
        buf1_.resize(maxSize);
        outSize_ = layers_.back().outFeatures;
    }

    // Returns pointer to internal buffer with outSize_ floats (valid until next call)
    const float* forwardFast(const float* input) {
        float* src = buf0_.data();
        float* dst = buf1_.data();

        // Copy input into src
        int inSize = layers_[0].inFeatures;
        std::memcpy(src, input, inSize * sizeof(float));

        for (size_t i = 0; i < layers_.size(); i++) {
            layers_[i].forwardInto(src, dst);
            int n = layers_[i].outFeatures;
            // ReLU on hidden layers only
            if (i < layers_.size() - 1) {
                for (int j = 0; j < n; j++) {
                    if (dst[j] < 0.0f) dst[j] = 0.0f;
                }
            }
            std::swap(src, dst);
        }
        // Result is in src after last swap
        return src;
    }

    int getOutSize() const { return outSize_; }

    // Softmax for policy output
    static std::vector<float> softmax(const std::vector<float>& logits) {
        std::vector<float> probs(logits.size());
        float maxVal = *std::max_element(logits.begin(), logits.end());
        float sum = 0.0f;
        for (size_t i = 0; i < logits.size(); i++) {
            probs[i] = std::exp(logits[i] - maxVal);
            sum += probs[i];
        }
        for (auto& p : probs) p /= (sum + 1e-8f);
        return probs;
    }

    // Constrained softmax: clamps logits to maintain minimum entropy
    // This prevents policy collapse by allowing up to ~98/2 split (entropy ~0.12)
    static std::vector<float> constrainedSoftmax(const std::vector<float>& logits) {
        // Clamp logits to prevent extreme probabilities
        // logit difference of ~4.0 gives ~98/2 split (entropy ~0.12) - allows learning
        // logit difference of ~2.0 gives ~88/12 split (entropy ~0.3) - too restrictive
        float maxLogit = *std::max_element(logits.begin(), logits.end());
        std::vector<float> clamped = logits;
        for (auto& l : clamped) {
            // Clamp relative to max logit to maintain some exploration
            l = std::max(l, maxLogit - 4.0f);  // allow up to ~98/2 probability split
        }
        return softmax(clamped);
    }

    // Sample action from probability distribution
    int sampleAction(const std::vector<float>& probs) {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        float r = dist(rng_);
        float cum = 0.0f;
        for (size_t i = 0; i < probs.size(); i++) {
            cum += probs[i];
            if (r <= cum) return (int)i;
        }
        return (int)probs.size() - 1;
    }

    // ========================================================================
    // Backpropagation for PPO training
    // ========================================================================

    // Compute gradients for a batch of (input, target_gradient) pairs
    // For policy: gradient comes from PPO loss
    // For critic: gradient comes from value loss
    void backward(const std::vector<float>& input,
                  const std::vector<float>& outputGrad) {
        // Need activations from forward pass
        if (activations_.empty()) forward(input);

        std::vector<float> grad = outputGrad;

        // Backpropagate through layers (reverse order)
        for (int i = (int)layers_.size() - 1; i >= 0; i--) {
            auto& layer = layers_[i];
            const auto& layerInput = activations_[i];
            const auto& layerOutput = activations_[i + 1];

            // Apply ReLU derivative for hidden layers
            std::vector<float> dOut = grad;
            if (i < (int)layers_.size() - 1) {
                for (int j = 0; j < (int)dOut.size(); j++) {
                    if (layerOutput[j] <= 0.0f) dOut[j] = 0.0f;
                }
            }

            // Compute weight and bias gradients
            for (int o = 0; o < layer.outFeatures; o++) {
                layer.biasGrad[o] += dOut[o];
                for (int in = 0; in < layer.inFeatures; in++) {
                    layer.weightGrad.data[o * layer.inFeatures + in] +=
                        dOut[o] * layerInput[in];
                }
            }

            // Compute gradient for previous layer
            if (i > 0) {
                std::vector<float> prevGrad(layer.inFeatures, 0.0f);
                for (int in = 0; in < layer.inFeatures; in++) {
                    float sum = 0.0f;
                    for (int o = 0; o < layer.outFeatures; o++) {
                        sum += layer.weights.data[o * layer.inFeatures + in] * dOut[o];
                    }
                    prevGrad[in] = sum;
                }
                grad = prevGrad;
            }
        }
    }

    // Adam optimizer step
    void adamStep(float lr, float beta1 = 0.9f, float beta2 = 0.999f,
                  float eps = 1e-8f) {
        adamT_++;
        float bc1 = 1.0f - std::pow(beta1, (float)adamT_);
        float bc2 = 1.0f - std::pow(beta2, (float)adamT_);

        for (auto& layer : layers_) {
            // Update weights
            for (int i = 0; i < layer.weights.size(); i++) {
                float g = layer.weightGrad.data[i];
                layer.weightM.data[i] = beta1 * layer.weightM.data[i] + (1.0f - beta1) * g;
                layer.weightV.data[i] = beta2 * layer.weightV.data[i] + (1.0f - beta2) * g * g;
                float mHat = layer.weightM.data[i] / bc1;
                float vHat = layer.weightV.data[i] / bc2;
                layer.weights.data[i] -= lr * mHat / (std::sqrt(vHat) + eps);
            }

            // Update biases
            for (int i = 0; i < layer.outFeatures; i++) {
                float g = layer.biasGrad[i];
                layer.biasM[i] = beta1 * layer.biasM[i] + (1.0f - beta1) * g;
                layer.biasV[i] = beta2 * layer.biasV[i] + (1.0f - beta2) * g * g;
                float mHat = layer.biasM[i] / bc1;
                float vHat = layer.biasV[i] / bc2;
                layer.bias[i] -= lr * mHat / (std::sqrt(vHat) + eps);
            }
        }
    }

    void zeroGrad() {
        for (auto& layer : layers_) layer.zeroGrad();
    }

    // Gradient clipping
    void clipGrad(float maxNorm) {
        float totalNorm = 0.0f;
        for (const auto& layer : layers_) {
            for (float g : layer.weightGrad.data) totalNorm += g * g;
            for (float g : layer.biasGrad) totalNorm += g * g;
        }
        totalNorm = std::sqrt(totalNorm);
        if (totalNorm > maxNorm) {
            float scale = maxNorm / (totalNorm + 1e-8f);
            for (auto& layer : layers_) {
                for (auto& g : layer.weightGrad.data) g *= scale;
                for (auto& g : layer.biasGrad) g *= scale;
            }
        }
    }

    // ========================================================================
    // Save / Load
    // ========================================================================
    void save(const std::string& path) const {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return;

        int numLayers = (int)layers_.size();
        file.write((char*)&numLayers, sizeof(int));

        for (const auto& layer : layers_) {
            file.write((char*)&layer.inFeatures, sizeof(int));
            file.write((char*)&layer.outFeatures, sizeof(int));
            file.write((char*)layer.weights.data.data(),
                       layer.weights.size() * sizeof(float));
            file.write((char*)layer.bias.data(),
                       layer.outFeatures * sizeof(float));
        }
    }

    bool load(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        int numLayers;
        file.read((char*)&numLayers, sizeof(int));

        layers_.resize(numLayers);
        layerSizes_.clear();

        for (int i = 0; i < numLayers; i++) {
            int in, out;
            file.read((char*)&in, sizeof(int));
            file.read((char*)&out, sizeof(int));

            if (i == 0) layerSizes_.push_back(in);
            layerSizes_.push_back(out);

            layers_[i].init(in, out, rng_);
            file.read((char*)layers_[i].weights.data.data(),
                       layers_[i].weights.size() * sizeof(float));
            file.read((char*)layers_[i].bias.data(),
                       layers_[i].outFeatures * sizeof(float));
        }

        paramCount_ = 0;
        for (const auto& l : layers_) {
            paramCount_ += l.weights.size() + (int)l.bias.size();
        }
        return true;
    }

    // ========================================================================
    // Genetic algorithm support (mutation-based evolution)
    // ========================================================================

    // Create a mutated copy of this network
    NeuralNet mutate(float mutationRate, float mutationScale) const {
        NeuralNet child = *this; // copy
        std::normal_distribution<float> dist(0.0f, mutationScale);
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);

        for (auto& layer : child.layers_) {
            for (auto& w : layer.weights.data) {
                if (chance(child.rng_) < mutationRate) {
                    w += dist(child.rng_);
                }
            }
            for (auto& b : layer.bias) {
                if (chance(child.rng_) < mutationRate) {
                    b += dist(child.rng_);
                }
            }
        }
        return child;
    }

    // Crossover between two networks
    static NeuralNet crossover(const NeuralNet& a, const NeuralNet& b) {
        NeuralNet child = a;
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (size_t l = 0; l < child.layers_.size(); l++) {
            for (int i = 0; i < child.layers_[l].weights.size(); i++) {
                if (dist(child.rng_) < 0.5f) {
                    child.layers_[l].weights.data[i] = b.layers_[l].weights.data[i];
                }
            }
            for (int i = 0; i < child.layers_[l].outFeatures; i++) {
                if (dist(child.rng_) < 0.5f) {
                    child.layers_[l].bias[i] = b.layers_[l].bias[i];
                }
            }
        }
        return child;
    }

    int getParamCount() const { return paramCount_; }
    const std::vector<int>& getLayerSizes() const { return layerSizes_; }

    // Get flat parameter vector (for genetic algorithms)
    std::vector<float> getParams() const {
        std::vector<float> params;
        params.reserve(paramCount_);
        for (const auto& l : layers_) {
            params.insert(params.end(), l.weights.data.begin(), l.weights.data.end());
            params.insert(params.end(), l.bias.begin(), l.bias.end());
        }
        return params;
    }

    void setParams(const std::vector<float>& params) {
        int idx = 0;
        for (auto& l : layers_) {
            for (auto& w : l.weights.data) w = params[idx++];
            for (auto& b : l.bias) b = params[idx++];
        }
    }

private:
    std::vector<DenseLayer> layers_;
    std::vector<int> layerSizes_;
    std::vector<std::vector<float>> activations_; // cached for backprop
    std::mt19937 rng_{42};
    int adamT_ = 0;
    int paramCount_ = 0;

    // Pre-allocated inference buffers (forwardFast)
    std::vector<float> buf0_;
    std::vector<float> buf1_;
    int outSize_ = 0;
};
