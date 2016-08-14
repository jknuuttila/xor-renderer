#ifndef SHADERS_H_HLSL
#define SHADERS_H_HLSL

#define XOR_ROOT_SIGNATURE_BASE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," \
    "StaticSampler(s0, " \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT), "

#define XOR_ROOT_SIGNATURE_C(numCBVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(CBV(b0, numDescriptors = " #numCBVs ")) " \

#define XOR_ROOT_SIGNATURE_S(numSRVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(SRV(t0, numDescriptors = " #numSRVs ")) " \

#define XOR_ROOT_SIGNATURE_U(numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(UAV(u0, numDescriptors = " #numUAVs ")) " \

#define XOR_ROOT_SIGNATURE_CS(numCBVs, numSRVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(CBV(b0, numDescriptors = " #numCBVs ")," \
    "                SRV(t0, numDescriptors = " #numSRVs ")) " \

#define XOR_ROOT_SIGNATURE_CU(numCBVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(CBV(b0, numDescriptors = " #numCBVs ")," \
    "                UAV(u0, numDescriptors = " #numUAVs ")) " \

#define XOR_ROOT_SIGNATURE_SU(numSRVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(SRV(t0, numDescriptors = " #numSRVs ")," \
    "                UAV(u0, numDescriptors = " #numUAVs ")) " \

#define XOR_ROOT_SIGNATURE_CSU(numCBVs, numSRVs, numUAVs) \
    XOR_ROOT_SIGNATURE_BASE \
    "DescriptorTable(CBV(b0, numDescriptors = " #numCBVs ")," \
    "                SRV(t0, numDescriptors = " #numSRVs "), " \
    "                UAV(u0, numDescriptors = " #numUAVs ")) " \

#endif