#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"

#include "LoadBalancingDefs.h"
#include "LoadBalancedShader.sig.h"

#include <random>
#include <unordered_set>

using namespace xor;

enum class LBShaderVariant
{
    Naive,
    PrefixLinear,
    PrefixLinearSkipZeros,
    PrefixLinearStore4,
    PrefixBinary,
};

const char *ShaderSettingNames[] =
{
    "Naive",
    "PrefixLinear",
    "PrefixLinearSkipZeros",
    "PrefixLinearStore4",
    "PrefixBinary",
};

class LoadBalancing : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;

    ComputePipeline loadBalancedShader;

    struct ShaderSettings
    {
        LBShaderVariant shaderVariant = LBShaderVariant::Naive;
        int threadGroupSizeExp        = 6;
        int subgroupSizeExp           = 4;

        int threadGroupSize() const { return 1 << threadGroupSizeExp; }
        int subgroupSize() const { return 1 << subgroupSizeExp; }
    } shaderSettings;

    struct WorkloadSettings
    {
#if defined(_DEBUG)
        int iterations = 1;
        //int sizeExp = 4;
        int sizeExp = 5;
        int minItems = 0;
        int maxItems = 5;
        float zeroProb = .5f;
#if 1
        bool verify  = true;
#else
        bool verify  = false;
#endif
#else
        int iterations = 15;
        int sizeExp = 18;
        int minItems = 0;
        int maxItems = 30;
        float zeroProb = .5f;
        bool verify = false;
#endif
		bool vsync = true;

        uint size() const { return 1u << uint(sizeExp) ; }
    } workloadSettings;

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
        , xor(Xor::DebugLayer::GPUBasedValidation)
#endif
    {
        xor.registerShaderTlog(XOR_PROJECT_NAME, XOR_PROJECT_TLOG);

#if 1
        device      = xor.defaultDevice();
#else
        device      = xor.warpDevice();
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

        std::mt19937 gen(2358279);
        std::uniform_int_distribution<uint> dist(workloadSettings.minItems, workloadSettings.maxItems);
        std::uniform_real_distribution<float> zeroDist;

        uint size = workloadSettings.size();
        workload.input.reserve(size);
        for (uint i = 0; i < size; ++i)
        {
            uint items = dist(gen) & WorkItemCountMask;

            if (zeroDist(gen) < workloadSettings.zeroProb)
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

        switch (shaderSettings.shaderVariant)
        {
        case LBShaderVariant::Naive:
        default:
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
        }

        int sgs     = std::min(shaderSettings.subgroupSize(),  shaderSettings.threadGroupSize());
        int sgsLog2 = std::min(shaderSettings.subgroupSizeExp, shaderSettings.threadGroupSizeExp);

        defines.emplace_back("LB_THREADGROUP_SIZE",      shaderSettings.threadGroupSize());
        defines.emplace_back("LB_THREADGROUP_SIZE_LOG2", shaderSettings.threadGroupSizeExp);
        defines.emplace_back("LB_SUBGROUP_SIZE",      sgs);
        defines.emplace_back("LB_SUBGROUP_SIZE_LOG2", sgsLog2);

        auto variant = loadBalancedShader.variant()
            .computeShader(info::SameShader {}, defines);

        bool verified = false;

        if (!workloadSettings.verify)
            verified = true;

        float time = 1e12f;

        auto cmd = device.graphicsCommandList("Benchmark");

        for (int i = 0; i < workloadSettings.iterations; ++i)
        {
            cmd.clearUAV(workload.outputCounter);

            LoadBalancedShader::Constants constants;
            constants.size = workloadSettings.size();

            cmd.bind(variant);
            cmd.setConstants(constants);
            cmd.setShaderView(LoadBalancedShader::input,         workload.inputSRV);
            cmd.setShaderView(LoadBalancedShader::output,        workload.outputUAV);
            cmd.setShaderView(LoadBalancedShader::outputCounter, workload.outputCounter);

            auto e = cmd.profilingEvent("Iteration", i);
            cmd.dispatchThreads(uint3(shaderSettings.threadGroupSize(), 1, 1),
                                uint3(workloadSettings.size(), 0, 0));
            time = std::min(time, e.minimumMs());
        }

        if (workloadSettings.verify)
        {
            cmd.readbackBuffer(workload.outputUAV.buffer(), [&](auto results)
            {
                bool correct = this->verifyOutput(reinterpretSpan<const uint>(results));
                verified     = true;
                XOR_CHECK(correct, "Output was incorrect");
            });
        }

        device.execute(cmd);
        device.waitUntilCompleted(cmd.number());

        XOR_CHECK(verified, "Output was not verified");

        if (device.frameNumber() % 10 == 0)
        {
            log("runBenchmark", "Variant: %25s, TGS: %3d, SGS: %3d, minimum GPU time: %.4f\n",
                ShaderSettingNames[static_cast<int>(shaderSettings.shaderVariant)],
                shaderSettings.threadGroupSize(),
                shaderSettings.subgroupSize(),
                time);
        }
    }

    void mainLoop(double deltaTime) override
    {
        auto cmd        = device.graphicsCommandList("Frame");
        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime);

        cmd.clearRTV(backbuffer, float4(.1f, .1f, .25f, 1.f));

        ImGui::SetNextWindowPos(float2(100, 100));
        if (ImGui::Begin("Workload", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SliderInt("Shader iterations", &workloadSettings.iterations, 1, 50);
            ImGui::SliderInt("Size exponent", &workloadSettings.sizeExp, 0, 24);
            ImGui::Text("Size: %u", workloadSettings.size());
            ImGui::InputInt("Minimum items", &workloadSettings.minItems);
            ImGui::InputInt("Maximum items", &workloadSettings.maxItems);
            ImGui::SliderFloat("Probability of zero items", &workloadSettings.zeroProb, 0, 1);
			ImGui::Checkbox("VSync", &workloadSettings.vsync);
            ImGui::Checkbox("Verify output", &workloadSettings.verify);
            if (ImGui::Button("Update workload"))
                generateWorkload();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(float2(100, 500));
        if (ImGui::Begin("Shader variant", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Combo("Variant", reinterpret_cast<int *>(&shaderSettings.shaderVariant),
                         "Naive\0"
                         "PrefixLinear\0"
                         "PrefixLinearSkipZeros\0"
                         "PrefixLinearStore4\0"
                         "PrefixBinary\0");
            ImGui::SliderInt("Thread group size", &shaderSettings.threadGroupSizeExp, 4, 8); 
            ImGui::Text("Thread group size: %d", shaderSettings.threadGroupSize());
            ImGui::SliderInt("Subgroup size",     &shaderSettings.subgroupSizeExp,    4, 8); 
            ImGui::Text("Subgroup size: %d", shaderSettings.subgroupSize());
        }
        ImGui::End();

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);

        runBenchmark();

        device.present(swapChain, workloadSettings.vsync);
    }
};

int main(int argc, const char *argv[])
{
    return LoadBalancing().run();
}
