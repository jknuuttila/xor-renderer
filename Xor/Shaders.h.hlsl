#ifndef SHADERS_H_HLSL
#define SHADERS_H_HLSL

#define XOR_ROOT_SIGNATURE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
    "DescriptorTable(" \
        "CBV(b0, offset = 0, numDescriptors = unbounded)," \
        "SRV(t0, offset = 0, numDescriptors = unbounded)," \
        "UAV(u0, offset = 0, numDescriptors = unbounded))," \
    "StaticSampler(s0, " \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)"

#endif