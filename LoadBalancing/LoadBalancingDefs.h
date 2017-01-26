#ifndef LOADBALANCINGDEFS_H
#define LOADBALANCINGDEFS_H

#ifdef __cplusplus
#include "Core/Math.hpp"
#endif

static const uint WorkItemIndexBits = 22;
static const uint WorkItemCountBits = 10;
static const uint WorkItemCountMask = (1 << WorkItemCountBits) - 1;

#if 0
static const uint LBThreadGroupSize     = 256;
static const uint LBThreadGroupSizeLog2 = 8;
#else
static const uint LBThreadGroupSize     = 16;
static const uint LBThreadGroupSizeLog2 = 4;
#endif

#endif