#pragma once

#include <memory>
#include <vector>

namespace dsp {

class Filter {
public:
    virtual ~Filter() = default;
    virtual float process(float input) = 0;
    virtual void reset() {}
};

class OnePoleLowPass : public Filter {
public:
    explicit OnePoleLowPass(float alpha = 0.5f);

    void setAlpha(float alpha);
    float process(float input) override;
    void reset() override;

private:
    float alpha_;
    float state_;
};

class FilterChain {
public:
    void addFilter(std::unique_ptr<Filter> filter);
    void clear();
    void reset();
    bool empty() const;
    float process(float input);

private:
    std::vector<std::unique_ptr<Filter>> filters_;
};

}  // namespace dsp
