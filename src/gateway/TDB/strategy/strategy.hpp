#pragma once
#include <cstddef>
#include "../buffer/buffer.hpp"

class DistributionStrategy {
public:
    virtual ~DistributionStrategy() = default;
};
