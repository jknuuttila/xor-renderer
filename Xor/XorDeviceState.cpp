#include "Xor/XorDeviceState.hpp"
#include "Xor/XorCommandList.hpp"

namespace xor
{
    static const uint MaxRTVs = 256;
    static const uint MaxDSVs = 256;
    static const uint DescriptorHeapSize = 65536 * 4;
    static const uint DescriptorHeapRing = 65536 * 3;
    static const uint QueryHeapSize = 65536;

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

			queryHeap = std::make_shared<QueryHeap>(device.Get(), QueryHeapSize);

            nullTextureSRV = shaderViews.allocateFromHeap();
            nullTextureUAV = shaderViews.allocateFromHeap();

            {
                D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
                desc.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
                desc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                desc.Texture2D.MipLevels           = static_cast<UINT>(-1);
                desc.Texture2D.MostDetailedMip     = 0;
                desc.Texture2D.PlaneSlice          = 0;
                desc.Texture2D.ResourceMinLODClamp = 0;
                device->CreateShaderResourceView(
                    nullptr,
                    &desc,
                    nullTextureSRV.cpu);
            }

            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
                desc.Format                           = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipSlice               = 0;
                desc.Texture2D.PlaneSlice             = 0;
                device->CreateUnorderedAccessView(
                    nullptr, nullptr,
                    &desc,
                    nullTextureUAV.cpu);
            }
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

		QueryHeap::QueryHeap(ID3D12Device * device, size_t size)
		{
			size_t numTimestamps = 2 * size;

			D3D12_HEAP_PROPERTIES heapDesc ={};
			heapDesc.Type                 = D3D12_HEAP_TYPE_READBACK;
			heapDesc.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heapDesc.CreationNodeMask     = 0;
			heapDesc.VisibleNodeMask      = 0;

			D3D12_RESOURCE_DESC desc ={};
			desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			desc.Width              = numTimestamps * sizeof(uint64_t);
			desc.Height             = 1;
			desc.DepthOrArraySize   = 1;
			desc.MipLevels          = 1;
			desc.Format             = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count   = 1;
			desc.SampleDesc.Quality = 0;
			desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

			XOR_CHECK_HR(device->CreateCommittedResource(
				&heapDesc,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				__uuidof(ID3D12Resource),
				&readback));
			setName(readback, "QueryHeap readback");

			D3D12_QUERY_HEAP_DESC timestampDesc = {};
			timestampDesc.Type                  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			timestampDesc.Count                 = static_cast<UINT>(size * 2);
			timestampDesc.NodeMask              = 0;

			XOR_CHECK_HR(device->CreateQueryHeap(
				&timestampDesc,
				__uuidof(ID3D12QueryHeap),
				&timestamps));

			ringbuffer = OffsetRing(size);
			metadata.resize(size);
		}

		void QueryHeap::resolve(ID3D12GraphicsCommandList * cmdList, int64_t first, int64_t last)
		{
			XOR_CHECK(last >= first, "TODO: implement fully");

			UINT start = static_cast<UINT>(first * 2);
			UINT count = static_cast<UINT>((last - first + 1) * 2);

			cmdList->ResolveQueryData(
				timestamps.Get(),
				D3D12_QUERY_TYPE_TIMESTAMP,
				start, count,
				readback.Get(),
				start * sizeof(uint64_t));
		}

		int64_t QueryHeap::beginEvent(ID3D12GraphicsCommandList * cmdList,
                                      const char * name, bool print,
                                      SeqNum cmdListNumber)
		{
			int64_t offset = ringbuffer.allocate();
			XOR_CHECK(offset >= 0, "Out of ringbuffer space");
			auto &m         = metadata[offset];
			m.name          = name;
			m.cmdListNumber = cmdListNumber;
			m.parent        = top;
            m.print         = print;

			cmdList->EndQuery(timestamps.Get(),
							  D3D12_QUERY_TYPE_TIMESTAMP,
							  static_cast<UINT>(offset * 2));

			top = offset;

			return offset;
		}

		void QueryHeap::endEvent(ID3D12GraphicsCommandList * cmdList, int64_t eventOffset)
		{
			XOR_CHECK(eventOffset >= 0, "Invalid event");
			auto &m = metadata[eventOffset];

			cmdList->EndQuery(timestamps.Get(),
							  D3D12_QUERY_TYPE_TIMESTAMP,
							  static_cast<UINT>(eventOffset * 2 + 1));

			top = m.parent;
		}
}
}
