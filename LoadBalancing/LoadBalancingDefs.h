#ifndef LOADBALANCINGDEFS_H
#define LOADBALANCINGDEFS_H

#ifdef __cplusplus
#include "Core/Math.hpp"
#endif

static const uint WorkItemIndexBits = 22;
static const uint WorkItemCountBits = 10;
static const uint WorkItemCountMask = (1 << WorkItemCountBits) - 1;

#endif
