#include "Core/Core.hpp"
#include "Core/TLog.hpp"
#include "Xor/Xor.hpp"

#include "LoadBalancingDefs.h"
#include "LoadBalancedShader.sig.h"

#include <random>
#include <unordered_set>

using namespace xor;

class LoadBalancing : public Window
{
    Xor xor;
    Device device;
    SwapChain swapChain;

    ComputePipeline loadBalancedShader;
    double measuredCpuTime = 1e12;

    struct ShaderSettings
    {
    } shaderSettings;

    struct WorkloadSettings
    {
#if defined(_DEBUG)
        int iterations = 1;
        int sizeExp = 4;
        int minItems = 0;
        int maxItems = 5;
        bool verify  = true;
#else
        int iterations = 15;
        int sizeExp = 18;
        int minItems = 0;
        int maxItems = 30;
        bool verify  = false;
#endif

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

        uint size = workloadSettings.size();
        workload.input.reserve(size);
        for (uint i = 0; i < size; ++i)
        {
            uint items = dist(gen) & WorkItemCountMask;

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
        std::vector<uint> sortedOutput(output.begin(), output.begin() + workload.correctOutput.size());
        sort(sortedOutput);

        return memcmp(workload.correctOutput.data(), sortedOutput.data(), sizeBytes(workload.correctOutput)) == 0;
    }

    void runBenchmark()
    {
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

            cmd.bind(loadBalancedShader);
            cmd.setConstants(constants);
            cmd.setShaderView(LoadBalancedShader::input,         workload.inputSRV);
            cmd.setShaderView(LoadBalancedShader::output,        workload.outputUAV);
            cmd.setShaderView(LoadBalancedShader::outputCounter, workload.outputCounter);

            auto e = cmd.profilingEvent("Iteration", i);
            cmd.dispatchThreads(LoadBalancedShader::threadGroupSize, uint3(workloadSettings.size(), 0, 0));
            time = std::min(time, e.minimumMs());
        }

        double timeToVerify = 0;
        if (workloadSettings.verify)
        {
            cmd.readbackBuffer(workload.outputUAV.buffer(), [&](auto results)
            {
                Timer t;
                bool correct = this->verifyOutput(reinterpretSpan<const uint>(results));
                verified     = true;
                timeToVerify = t.milliseconds();
                XOR_CHECK(correct, "Output was incorrect");
            });
        }

        Timer cpuTimer;
        device.execute(cmd);
        device.waitUntilCompleted(cmd.number());
        double cpuTime = cpuTimer.milliseconds() - timeToVerify;

        XOR_CHECK(verified, "Output was not verified");

        measuredCpuTime = std::min(measuredCpuTime, cpuTime);
        if (device.frameNumber() % 60 == 0)
        {
            log("runBenchmark", "Minimum CPU time: %.4f ms, GPU time: %.4f\n", measuredCpuTime, time);
            measuredCpuTime = 1e12;
        }
    }

    void mainLoop(double deltaTime) override
    {
        auto cmd        = device.graphicsCommandList("Frame");
        auto backbuffer = swapChain.backbuffer();

        cmd.imguiBeginFrame(swapChain, deltaTime);

        cmd.clearRTV(backbuffer, float4(.1f, .1f, .25f, 1.f));

        if (ImGui::Begin("Workload", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::SliderInt("Shader iterations", &workloadSettings.iterations, 1, 50);
            ImGui::SliderInt("Size exponent", &workloadSettings.sizeExp, 0, 24);
            ImGui::Text("Size: %u", workloadSettings.size());
            ImGui::InputInt("Minimum items", &workloadSettings.minItems);
            ImGui::InputInt("Maximum items", &workloadSettings.maxItems);
            ImGui::Checkbox("Verify output", &workloadSettings.verify);
            if (ImGui::Button("Update workload"))
                generateWorkload();
        }
        ImGui::End();

        cmd.imguiEndFrame(swapChain);

        device.execute(cmd);

        runBenchmark();

        device.present(swapChain);
    }
};

int main(int argc, const char *argv[])
{
    return LoadBalancing().run();
}
