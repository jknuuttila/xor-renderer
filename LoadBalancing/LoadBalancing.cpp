#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"

#include "LoadBalancingDefs.h"
#include "LoadBalancedShader.sig.h"

#include <random>
#include <unordered_set>

using namespace Xor;

XOR_DEFINE_CONFIG_ENUM(LBShaderVariant, 
    Naive,
    NaiveLDSAtomics,
    PrefixLinear,
    PrefixLinearSkipZeros,
    PrefixLinearStore4,
    PrefixBinary,
    PrefixBitscan,
    WorkStealing,
    OneAtATime);

XOR_CONFIG_WINDOW(ShaderSettings, 100, 500)
{
    XOR_CONFIG_ENUM(LBShaderVariant, shaderVariant, "Shader variant", LBShaderVariant::OneAtATime);

#if defined(_DEBUG)
    XOR_CONFIG_SLIDER(int, threadGroupSizeExp, "Thread group size exponent", 5, 4, 8);
    XOR_CONFIG_SLIDER(int, subgroupSizeExp   , "Subgroup size exponent", 4, 4, 8);
#else
    XOR_CONFIG_SLIDER(int, threadGroupSizeExp, "Thread group size exponent", 6, 4, 8);
    XOR_CONFIG_SLIDER(int, subgroupSizeExp   , "Subgroup size exponent", 4, 4, 8);
#endif
    XOR_CONFIG_TEXT("Thread group size", "%d", &ShaderSettings::threadGroupSize);
    XOR_CONFIG_TEXT("Subgroup size", "%d", &ShaderSettings::subgroupSize);

    int threadGroupSize() const { return 1 << threadGroupSizeExp; }
    int subgroupSize() const { return 1 << subgroupSizeExp; }
} cfg_ShaderSettings;

XOR_CONFIG_WINDOW(WorkloadSettings, 100, 100)
{
#if defined(_DEBUG)
    XOR_CONFIG_SLIDER(  int, iterations, "Iterations",       1, 1, 50);
    XOR_CONFIG_SLIDER(  int, sizeExp,    "Size exponent",    5, 0, 24);
    XOR_CONFIG_TEXT("Size", "%u", &WorkloadSettings::size);
    XOR_CONFIG_INPUT(   int, minItems,   "Minimum items",    0);
    XOR_CONFIG_INPUT(   int, maxItems,   "Maximum items",    5);
    XOR_CONFIG_INPUT(   int, multiplier, "Multiplier",       1);
    XOR_CONFIG_SLIDER(float, zeroProb,   "Zero probability", .5f);
    XOR_CONFIG_CHECKBOX(verify, "Verify output", true);
#else
    XOR_CONFIG_SLIDER(  int, iterations, "Iterations",       15, 1, 50);
    XOR_CONFIG_SLIDER(  int, sizeExp,    "Size exponent",    18, 0, 24);
    XOR_CONFIG_INPUT(   int, minItems,   "Minimum items",     0);
    XOR_CONFIG_INPUT(   int, maxItems,   "Maximum items",    30);
    XOR_CONFIG_INPUT(   int, multiplier, "Multiplier",       1);
    XOR_CONFIG_SLIDER(float, zeroProb,   "Zero probability", .5f);
    XOR_CONFIG_CHECKBOX(verify, "Verify output", true);
#endif
    XOR_CONFIG_CHECKBOX(vsync, "VSync", true);

    uint size() const { return 1u << uint(sizeExp) ; }
} cfg_WorkloadSettings;

class LoadBalancing : public Window
{
    XorLibrary xorLib;
    Device device;
    SwapChain swapChain;

    ComputePipeline loadBalancedShader;

    struct Workload
    {
        BufferSRV inputSRV;
        BufferUAV outputUAV;
        BufferUAV outputCounter;

        std::vector<uint> input;
        std::vector<uint> correctOutput;
    } workload;

public:
    LoadBalancing()
        : Window { XOR_PROJECT_NAME, { 1600, 900 } }
#if 0
        , xorLib(XorLibrary::DebugLayer::GPUBasedValidation)
#endif
    {
        xorLib.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

#if 1
        device      = xorLib.defaultDevice();
#else
        device      = xorLib.warpDevice();
#endif
        swapChain   = device.createSwapChain(*this);

        loadBalancedShader = device.createComputePipeline(info::ComputePipelineInfo()
                                              .computeShader("LoadBalancedShader.cs"));

        generateWorkload();
    }

    void handleInput(const Input &input) override
    {
        auto imguiInput = device.imguiInput(input);
    }

    void keyDown(int keyCode) override
    {
        if (keyCode == VK_ESCAPE)
            terminate(0);
    }

    void generateWorkload()
    {
        Timer t;

        workload.input.clear();
        workload.correctOutput.clear();

        Random gen(2358279);
        std::uniform_int_distribution<uint> dist(cfg_WorkloadSettings.minItems, cfg_WorkloadSettings.maxItems);
        std::uniform_real_distribution<float> zeroDist;

        uint size = cfg_WorkloadSettings.size();
        workload.input.reserve(size);
        for (uint i = 0; i < size; ++i)
        {
            uint items = dist(gen) & WorkItemCountMask;
            items *= cfg_WorkloadSettings.multiplier;

            if (zeroDist(gen) < cfg_WorkloadSettings.zeroProb)
                items = 0;

            uint inputValue = (i << WorkItemCountBits) | items;
            workload.input.emplace_back(inputValue);

            for (uint j = 0; j < items; ++j)
            {
                uint outputValue = (i << WorkItemCountBits) | j;
                workload.correctOutput.emplace_back(outputValue);
            }
        }

        sort(workload.correctOutput);

        workload.inputSRV = device.createBufferSRV(info::BufferInfoBuilder()
                                                   .rawBuffer(sizeBytes(workload.input))
                                                   .initialData(asBytes(workload.input)));
        // Add some extra room in the output in case a shader outputs too many values to catch that error.
        workload.outputUAV = device.createBufferUAV(info::BufferInfoBuilder()
                                                   .rawBuffer(sizeBytes(workload.correctOutput) + 1024));
        workload.outputCounter = device.createBufferUAV(info::BufferInfoBuilder()
                                                        .rawBuffer(sizeof(uint32_t)));

        log("generateWorkload", "Generated new %zu item workload in %.3f ms\n",
            workload.input.size(),
            t.milliseconds());
    }

    bool verifyOutput(Span<const uint> output)
    {
        constexpr uint MaximumFailures = 10;

        auto &correct = workload.correctOutput;

        size_t size = std::min(output.size(), correct.size());
        std::vector<uint> sortedOutput(output.begin(), output.begin() + size);
        sort(sortedOutput);

        uint failures = 0;

        for (size_t i = 0; i < size; ++i)
        {
            if (correct[i] != sortedOutput[i])
            {
                if (failures < MaximumFailures)
                {
                    log("verifyOutput", "INCORRECT OUTPUT: correct[%zu] == %08x, output[%zu] == %08x\n",
                        i, correct[i],
                        i, sortedOutput[i]);
                }
                ++failures;
            }
        }

        if (failures)
        {
            uint missing = 0;
            std::unordered_map<uint, uint> correctSet;

            for (size_t i = 0; i < size; ++i)
                correctSet[correct[i]] = static_cast<uint>(i);

            std::unordered_set<uint> outputSet(sortedOutput.begin(), sortedOutput.end());

            for (auto &kv : correctSet)
            {
                if (missing >= MaximumFailures)
                    break;

                if (!outputSet.count(kv.first))
                {
                    log("verifyOutput", "MISSING OUTPUT: correct[%u] == %08x\n",
                        kv.second, kv.first);
                }
            }
        }

        return failures == 0;
    }

    void runBenchmark()
    {
        std::vector<info::ShaderDefine> defines;
        defines.reserve(2);

        switch (cfg_ShaderSettings.shaderVariant)
        {
        case LBShaderVariant::Naive:
        default:
            break;
        case LBShaderVariant::NaiveLDSAtomics:
            defines.emplace_back("NAIVE_LDS_ATOMICS");
            break;
        case LBShaderVariant::PrefixLinear:
            defines.emplace_back("PREFIX_LINEAR");
            break;
        case LBShaderVariant::PrefixLinearSkipZeros:
            defines.emplace_back("PREFIX_LINEAR");
            defines.emplace_back("ZERO_SKIPPING");
            break;
        case LBShaderVariant::PrefixLinearStore4:
            defines.emplace_back("PREFIX_LINEAR_STORE4");
            break;
        case LBShaderVariant::PrefixBinary:
            defines.emplace_back("PREFIX_BINARY");
            break;
        case LBShaderVariant::PrefixBitscan:
            defines.emplace_back("PREFIX_BITSCAN");
            cfg_ShaderSettings.subgroupSizeExp = std::min<int>(cfg_ShaderSettings.subgroupSizeExp, 5);
            break;
        case LBShaderVariant::WorkStealing:
            defines.emplace_back("WORK_STEALING");
            cfg_ShaderSettings.subgroupSizeExp = std::min<int>(cfg_ShaderSettings.subgroupSizeExp, 5);
            break;
        case LBShaderVariant::OneAtATime:
            defines.emplace_back("ONE_AT_A_TIME");
            break;
        }

        int sgs     = std::min<int>(cfg_ShaderSettings.subgroupSize(),  cfg_ShaderSettings.threadGroupSize());
        int sgsLog2 = std::min<int>(cfg_ShaderSettings.subgroupSizeExp, cfg_ShaderSettings.threadGroupSizeExp);

        defines.emplace_back("LB_THREADGROUP_SIZE",      cfg_ShaderSettings.threadGroupSize());
        defines.emplace_back("LB_THREADGROUP_SIZE_LOG2", cfg_ShaderSettings.threadGroupSizeExp);
        defines.emplace_back("LB_SUBGROUP_SIZE",      sgs);
        defines.emplace_back("LB_SUBGROUP_SIZE_LOG2", sgsLog2);

        auto variant = loadBalancedShader.variant()
            .computeShader(info::SameShader {}, defines);

        bool verified = false;

        if (!cfg_WorkloadSettings.verify)
            verified = true;

        float time = 1e12f;

        auto cmd = device.graphicsCommandList("Benchmark");

        for (int i = 0; i < cfg_WorkloadSettings.iterations; ++i)
        {
            cmd.clearUAV(workload.outputCounter);

            LoadBalancedShader::Constants constants;
            constants.size = cfg_WorkloadSettings.size();

            cmd.bind(variant);
            cmd.setConstants(constants);
            cmd.setShaderView(LoadBalancedShader::input,         workload.inputSRV);
            cmd.setShaderView(LoadBalancedShader::output,        workload.outputUAV);
            cmd.setShaderView(LoadBalancedShader::outputCounter, workload.outputCounter);

            auto e = cmd.profilingEvent("Iteration", i);
            cmd.dispatchThreads(uint3(cfg_ShaderSettings.threadGroupSize(), 1, 1),
                                uint3(cfg_WorkloadSettings.size(), 0, 0));
            time = std::min(time, e.minimumMs());
        }

        if (cfg_WorkloadSettings.verify)
        {
            cmd.readbackBuffer(workload.outputUAV.buffer(), [&](auto results)
            {
                bool correct = this->verifyOutput(reinterpretSpan<const uint>(results));
                verified     = true;
                // XOR_CHECK(correct, "Output was incorrect");
            });
        }

        device.execute(cmd);
        device.waitUntilCompleted(cmd.number());

        XOR_CHECK(verified, "Output was not verified");

        if (device.frameNumber() % 10 == 0)
        {
            log("runBenchmark", "Variant: %25s, TGS: %3d, SGS: %3d, minimum GPU time: %.4f\n",
                cfg_ShaderSettings.shaderVariant.valueName(),
                cfg_ShaderSettings.threadGroupSize(),
                cfg_ShaderSettings.subgroupSize(),
                time);
        }
    }

    void mainLoop(double deltaTime) override
    {
        auto cmd        = device.graphicsCommandList("Frame");
        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime);

        cmd.clearRTV(backbuffer, float4(.1f, .1f, .25f, 1.f));

        if (cfg_WorkloadSettings.changed())
            generateWorkload();

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);

        runBenchmark();

        device.present(swapChain, cfg_WorkloadSettings.vsync);
    }
};

int main(int argc, const char *argv[])
{
    return LoadBalancing().run();
}
