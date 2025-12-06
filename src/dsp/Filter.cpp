#include "dsp/Filter.h"

#include <algorithm>
#include <cmath>

namespace dsp {

namespace {
float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}
}  // namespace

OnePoleLowPass::OnePoleLowPass(float alpha)
    : alpha_(clamp01(alpha)), state_(0.0f) {}

void OnePoleLowPass::setAlpha(float alpha) {
    alpha_ = clamp01(alpha);
}

float OnePoleLowPass::process(float input) {
    state_ = alpha_ * input + (1.0f - alpha_) * state_;
    return state_;
}

void OnePoleLowPass::reset() {
    state_ = 0.0f;
}

FirstOrderAllPass::FirstOrderAllPass(float coefficient) : coefficient_(0.0f), z1_(0.0f) {
    setCoefficient(coefficient);
}

void FirstOrderAllPass::setCoefficient(float coefficient) {
    const float clamped = clamp01(std::abs(coefficient));
    coefficient_ = (coefficient < 0.0f) ? -clamped : clamped;
}

float FirstOrderAllPass::process(float input) {
    const float y = -coefficient_ * input + z1_;
    z1_ = input + coefficient_ * y;
    return y;
}

void FirstOrderAllPass::reset() {
    z1_ = 0.0f;
}

void FilterChain::addFilter(std::unique_ptr<Filter> filter) {
    filters_.emplace_back(std::move(filter));
}

void FilterChain::clear() {
    filters_.clear();
}

void FilterChain::reset() {
    for (auto& filter : filters_) {
        filter->reset();
    }
}

bool FilterChain::empty() const {
    return filters_.empty();
}

float FilterChain::process(float input) {
    float value = input;
    for (auto& filter : filters_) {
        value = filter->process(value);
    }
    return value;
}

}  // namespace dsp
