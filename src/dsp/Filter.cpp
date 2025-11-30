#include "dsp/Filter.h"

#include <algorithm>

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
