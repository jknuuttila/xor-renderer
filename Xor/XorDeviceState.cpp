#include "Xor/XorDeviceState.hpp"
#include "Xor/XorCommandList.hpp"

namespace xor
{
    static const uint MaxRTVs = 256;
    static const uint MaxDSVs = 256;
    static const uint DescriptorHeapSize = 65536 * 4;
    static const uint DescriptorHeapRing = 65536 * 3;

    namespace backend
    {
        DeviceState::DeviceState(Adapter adapter_, ComPtr<ID3D12Device> pDevice, std::shared_ptr<backend::ShaderLoader> pShaderLoader)
        {

            adapter       = std::move(adapter_);
            device        = std::move(pDevice);
            shaderLoader  = std::move(pShaderLoader);

            {
                D3D12_COMMAND_QUEUE_DESC desc ={};
                desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
                desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
                desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
                desc.NodeMask = 0;
                XOR_CHECK_HR(device->CreateCommandQueue(
                    &desc,
                    __uuidof(ID3D12CommandQueue),
                    &graphicsQueue));
            }

            XOR_INTERNAL_DEBUG_NAME(graphicsQueue);

            uploadHeap = std::make_shared<UploadHeap>(device.Get());

            rtvs = ViewHeap(device.Get(),
                            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                            "rtvs",
                            MaxRTVs);
            dsvs = ViewHeap(device.Get(),
                            D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                            "dsvs",
                            MaxDSVs);
            shaderViews = ViewHeap(device.Get(),
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                   "shaderViews",
                                   DescriptorHeapSize, DescriptorHeapRing);
        }

        DeviceState::~DeviceState()
        {
            progress.waitUntilDrained();
#if 0
            ComPtr<ID3D12DebugDevice> debug;
            XOR_CHECK_HR(device.As(&debug));
            debug->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);
#endif
        }

        void GPUProgressTracking::executeCommandList(CommandList && cmd)
        {
            newestExecuted = std::max(newestExecuted, cmd.number());
            executedCommandLists.emplace_back(std::move(cmd));
        }

        void GPUProgressTracking::retireCommandLists()
        {
            uint completedLists = 0;

            for (auto &cmd : executedCommandLists)
            {
                if (cmd.hasCompleted())
                    ++completedLists;
                else
                    break;
            }

            for (uint i = 0; i < completedLists; ++i)
            {
                auto &cmd = executedCommandLists[i];
                commandListSequence.complete(cmd.number());
            }

            // This will also return the command list states to the pool
            executedCommandLists.erase(executedCommandLists.begin(),
                                       executedCommandLists.begin() + completedLists);

            while (!completionCallbacks.empty())
            {
                auto &top = completionCallbacks.top();
                if (commandListSequence.hasCompleted(top.seqNum))
                {
                    top.f();
                    completionCallbacks.pop();
                }
                else
                {
                    break;
                }
            }
        }

        void GPUProgressTracking::waitUntilCompleted(SeqNum seqNum)
        {
            while (!hasCompleted(seqNum))
            {
                auto &executed = executedCommandLists;
                XOR_CHECK(!executed.empty(), "Nothing to wait for, deadlock!");
                executed.front().waitUntilCompleted();
            }
        }
    }
}
