#ifndef SHADERS_H_HLSL
#define SHADERS_H_HLSL

#define XOR_ROOT_SIGNATURE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
    "StaticSampler(s0, " \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT)"

    // "DescriptorTable(SRV(t0, numDescriptors = unbounded))," \
    // "DescriptorTable(CBV(b0, numDescriptors = 8))," \
    // "DescriptorTable(UAV(u0, numDescriptors = 64))," \

#endif