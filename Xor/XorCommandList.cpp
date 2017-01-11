#include "Xor/XorBackend.hpp"
#include "Xor/XorDevice.hpp"
#include "Xor/XorDeviceState.hpp"
#include "Xor/XorCommandList.hpp"

#include "ImguiRenderer.sig.h"

namespace xor
{
    namespace backend
    {
        CommandListState::CommandListState(Device & dev)
        {
            setParent(&dev);

            XOR_CHECK_HR(dev.device()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator),
                &allocator));
            XOR_INTERNAL_DEBUG_NAME(allocator);

            XOR_CHECK_HR(dev.device()->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                __uuidof(ID3D12GraphicsCommandList),
                &cmd));
            XOR_INTERNAL_DEBUG_NAME(cmd);

            XOR_CHECK_HR(dev.device()->CreateFence(
                0,
                D3D12_FENCE_FLAG_NONE,
                __uuidof(ID3D12Fence),
                &timesCompleted));
            XOR_INTERNAL_DEBUG_NAME(timesCompleted);

            completedEvent = CreateEventExA(nullptr, nullptr, 0,
                                            EVENT_ALL_ACCESS);
            XOR_CHECK(!!completedEvent, "Failed to create completion event.");

			queryHeap = dev.S().queryHeap;
        }
    }

    ID3D12GraphicsCommandList *CommandList::cmd()
    {
        return S().cmd.Get();
    }

    void CommandList::close()
    {
        if (!S().closed)
        {
			S().cmdListEvent.done();

			if (S().firstProfilingEvent >= 0)
				S().queryHeap->resolve(cmd(), S().firstProfilingEvent, S().lastProfilingEvent);

            XOR_CHECK_HR(cmd()->Close());
            S().closed             = true;
            S().activeRenderTarget = Texture();
        }
    }

    void CommandList::reset()
    {
        if (S().closed)
        {
            XOR_CHECK_HR(cmd()->Reset(S().allocator.Get(), nullptr));
            S().closed             = false;
            S().activeRenderTarget = Texture();

			S().firstProfilingEvent = -1;
			S().lastProfilingEvent  = -1;
        }
    }

    bool CommandList::hasCompleted()
    {
        auto completed = S().timesCompleted->GetCompletedValue();

        XOR_ASSERT(completed <= S().timesStarted,
                   "Command list completion count out of sync.");

        return completed == S().timesStarted;
    }

    void CommandList::waitUntilCompleted(DWORD timeout)
    {
        while (!hasCompleted())
        {
            XOR_CHECK_HR(S().timesCompleted->SetEventOnCompletion(
                S().timesStarted,
                S().completedEvent.get()));
            WaitForSingleObject(S().completedEvent.get(), timeout);
        }
    }

    CommandList::CommandList(StatePtr state)
    {
        m_state = std::move(state);
    }

    void CommandList::release()
    {
        // FIXME: This is a race condition :(
        if (m_state)
        {
            if (m_state.unique())
            {
                auto dev = device();
                dev.releaseCommandList(std::move(m_state));
            }
            else
            {
                m_state.reset();
            }
        }
    }

    // FIXME: This is horribly inefficient and bad
    void CommandList::transition(const backend::Resource & resource, D3D12_RESOURCE_STATES newState)
    {
        if (!resource)
            return;

        auto &s = resource.S().state;

        if (s == newState)
        {
            if (newState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            {
                D3D12_RESOURCE_BARRIER barrier;

                barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.UAV.pResource          = resource.get();

                cmd()->ResourceBarrier(1, &barrier);
            }

            return;
        }

        D3D12_RESOURCE_BARRIER barrier;

        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = resource.get();
        barrier.Transition.StateBefore = s;
        barrier.Transition.StateAfter  = newState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        cmd()->ResourceBarrier(1, &barrier);

        s = newState;
    }

    void CommandList::setupRootArguments(bool compute)
    {
        auto &cbvs = S().cbvs;
        auto &srvs = S().srvs;
        auto &uavs = S().uavs;
        auto numCBVs = cbvs.size();
        auto numSRVs = srvs.size();
        auto numUAVs = uavs.size();
        auto totalDescriptors = numCBVs + numSRVs + numUAVs;

        if (totalDescriptors > 0)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE table;

            auto dev = S().device();
            auto &heap = dev.S().viewHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            auto start = heap.allocateFromRing(dev.S().progress, totalDescriptors, number());

            auto &srcs = S().viewDescriptorSrcs;

            srcs.clear();
            srcs.reserve(totalDescriptors);

            table = heap.descriptorAtOffset(start).gpu;
            for (size_t c = 0; c < numCBVs; ++c)
            {
                dev.device()->CreateConstantBufferView(
                    &cbvs[c],
                    heap.descriptorAtOffset(start + c).cpu);
            }

            for (size_t s = 0; s < numSRVs; ++s)
                srcs.emplace_back(srvs[s]);

            for (size_t u = 0; u < numUAVs; ++u)
                srcs.emplace_back(uavs[u]);

            if (!srcs.empty())
            {
                auto dst = heap.descriptorAtOffset(start + numCBVs);
                uint amount = static_cast<uint>(srcs.size());
                uint dstAmounts[1] = { amount };

                auto &srcAmounts = S().viewDescriptorAmounts;
                srcAmounts.clear();
                srcAmounts.resize(srcs.size(), 1 );

                dev.device()->CopyDescriptors(
                    1,      &dst.cpu,    dstAmounts,
                    amount, srcs.data(), srcAmounts.data(),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }

            if (compute)
                cmd()->SetComputeRootDescriptorTable(0, table);
            else
                cmd()->SetGraphicsRootDescriptorTable(0, table);
        }
    }

    backend::HeapBlock CommandList::uploadBytes(Span<const uint8_t> bytes, uint alignment)
    {
        return device().uploadBytes(bytes, number(), alignment);
    }

    CommandList::~CommandList()
    {
        release();
    }

    CommandList::CommandList(CommandList && c)
    {
        m_state = std::move(c.m_state);
    }

    CommandList & CommandList::operator=(CommandList && c)
    {
        if (this != &c)
        {
            release();
            m_state = std::move(c.m_state);
        }
        return *this;
    }

    SeqNum CommandList::number() const
    {
        return S().seqNum;
    }

    Device CommandList::device()
    {
        return S().device();
    }

    void CommandList::bind(GraphicsPipeline &pipeline)
    {
        cmd()->SetGraphicsRootSignature(pipeline.S().rootSignature.rs.Get());
        cmd()->SetPipelineState(pipeline.S().pso.Get());

        auto &rs = pipeline.S().rootSignature;
        auto &cbvs = S().cbvs;
        auto &srvs = S().srvs;
        auto &uavs = S().uavs;

        // TODO: Better check: Leave if the new RS is the same as the old RS

        // If the newly bound pipeline has exactly the same amounts of views,
        // we leave the previously bound stuff still bound. Otherwise we unbind.
        if (rs.numCBVs != cbvs.size()
            || rs.numSRVs != srvs.size()
            || rs.numUAVs != uavs.size())
        {
            S().cbvs.clear();
            S().srvs.clear();
            S().uavs.clear();

            S().cbvs.resize(pipeline.S().rootSignature.numCBVs);
            S().srvs.resize(pipeline.S().rootSignature.numSRVs);
            S().uavs.resize(pipeline.S().rootSignature.numUAVs);
        }

        // FIXME: This does not issue UAV barriers if UAVs are left bound
    }

    void CommandList::bind(const info::GraphicsPipelineInfo & pipelineInfo)
    {
        GraphicsPipeline pipeline = device().createGraphicsPipeline(pipelineInfo);
        bind(pipeline);
    }

    void CommandList::bind(ComputePipeline & pipeline)
    {
        cmd()->SetComputeRootSignature(pipeline.S().rootSignature.rs.Get());
        cmd()->SetPipelineState(pipeline.S().pso.Get());

        auto &rs = pipeline.S().rootSignature;
        auto &cbvs = S().cbvs;
        auto &srvs = S().srvs;
        auto &uavs = S().uavs;

        // If the newly bound pipeline has exactly the same amounts of views,
        // we leave the previously bound stuff still bound. Otherwise we unbind.
        if (rs.numCBVs != cbvs.size()
            || rs.numSRVs != srvs.size()
            || rs.numUAVs != uavs.size())
        {
            S().cbvs.clear();
            S().srvs.clear();
            S().uavs.clear();

            S().cbvs.resize(pipeline.S().rootSignature.numCBVs);
            S().srvs.resize(pipeline.S().rootSignature.numSRVs);
            S().uavs.resize(pipeline.S().rootSignature.numUAVs);
        }
    }

    void CommandList::bind(const info::ComputePipelineInfo & pipelineInfo)
    {
        ComputePipeline pipeline = device().createComputePipeline(pipelineInfo);
        bind(pipeline);
    }

    void CommandList::clearRTV(TextureRTV &rtv, float4 color)
    {
        transition(rtv.m_texture, D3D12_RESOURCE_STATE_RENDER_TARGET);

        cmd()->ClearRenderTargetView(rtv.S().descriptor.cpu,
                                     color.data(),
                                     0, nullptr);
    }

    void CommandList::clearDSV(TextureDSV & dsv, float depth)
    {
        transition(dsv.m_texture, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        cmd()->ClearDepthStencilView(dsv.S().descriptor.cpu,
                                     D3D12_CLEAR_FLAG_DEPTH,
                                     depth,
                                     0,
                                     0, nullptr);
    }

    void CommandList::clearUAV(TextureUAV & uav, uint4 clearValue)
    {
        transition(uav.m_texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmd()->ClearUnorderedAccessViewUint(uav.S().descriptor.gpu,
                                            uav.S().descriptor.staging,
                                            uav.m_texture.get(),
                                            clearValue.data(),
                                            0, nullptr);
    }

    void CommandList::clearUAV(TextureUAV & uav, float4 clearValue)
    {
        transition(uav.m_texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmd()->ClearUnorderedAccessViewFloat(uav.S().descriptor.gpu,
                                             uav.S().descriptor.staging,
                                             uav.m_texture.get(),
                                             clearValue.data(),
                                             0, nullptr);
    }

    void CommandList::setViewport(uint2 size)
    {
        setViewport(size, { 0, int2(size) });
    }

    void CommandList::setViewport(uint2 size, Rect scissor)
    {
        D3D12_VIEWPORT viewport = {};

        viewport.Width    = static_cast<float>(size.x);
        viewport.Height   = static_cast<float>(size.y);
        viewport.MinDepth = D3D12_MIN_DEPTH;
        viewport.MaxDepth = D3D12_MAX_DEPTH;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;

        cmd()->RSSetViewports(1, &viewport);

        setScissor(scissor);
    }

    void CommandList::setScissor(Rect scissor)
    {
        D3D12_RECT scissorRect = {};

        scissorRect.left   = static_cast<LONG>(scissor.leftTop.x);
        scissorRect.top    = static_cast<LONG>(scissor.leftTop.y);
        scissorRect.right  = static_cast<LONG>(scissor.rightBottom.x);
        scissorRect.bottom = static_cast<LONG>(scissor.rightBottom.y);

        cmd()->RSSetScissorRects(1, &scissorRect);
    }

    void CommandList::setRenderTargets()
    {
        S().activeRenderTarget = Texture();

        cmd()->OMSetRenderTargets(0,
                                  nullptr,
                                  false,
                                  nullptr);
    }

    void CommandList::setRenderTargets(TextureRTV &rtv)
    {
        transition(rtv.m_texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
        S().activeRenderTarget = rtv.m_texture;

        cmd()->OMSetRenderTargets(1,
                                  &rtv.S().descriptor.cpu,
                                  FALSE,
                                  nullptr);

        setViewport(rtv.texture()->size);
    }

    void CommandList::setRenderTargets(TextureRTV & rtv, TextureDSV & dsv)
    {
        transition(rtv.m_texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition(dsv.m_texture, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        S().activeRenderTarget = rtv.m_texture;

        cmd()->OMSetRenderTargets(1,
                                  &rtv.S().descriptor.cpu,
                                  FALSE,
                                  &dsv.S().descriptor.cpu);

        setViewport(rtv.texture()->size);
    }

    void CommandList::setRenderTargets(TextureDSV & dsv)
    {
        transition(dsv.m_texture, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        S().activeRenderTarget = Texture();

        cmd()->OMSetRenderTargets(0,
                                  nullptr,
                                  FALSE,
                                  &dsv.S().descriptor.cpu);

        setViewport(dsv.texture()->size);
    }

    BufferVBV CommandList::dynamicBufferVBV(Span<const uint8_t> bytes, uint stride)
    {
        auto block = uploadBytes(bytes);

        BufferVBV vbv;
        vbv.m_vbv.BufferLocation  = block.heap->GetGPUVirtualAddress();
        vbv.m_vbv.BufferLocation += block.block.begin;
        vbv.m_vbv.SizeInBytes     = static_cast<uint>(bytes.size());
        vbv.m_vbv.StrideInBytes   = stride;
        return vbv;
    }

    BufferIBV CommandList::dynamicBufferIBV(Span<const uint8_t> bytes, Format format)
    {
        auto block = uploadBytes(bytes);

        BufferIBV ibv;
        ibv.m_ibv.BufferLocation  = block.heap->GetGPUVirtualAddress();
        ibv.m_ibv.BufferLocation += block.block.begin;
        ibv.m_ibv.SizeInBytes     = static_cast<uint>(bytes.size());
        ibv.m_ibv.Format          = format;
        return ibv;
    }

    void CommandList::setVBV(const BufferVBV & vbv, uint index)
    {
        transition(vbv.m_buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        cmd()->IASetVertexBuffers(index, 1, &vbv.m_vbv);
    }

    void CommandList::setVBVs(Span<const BufferVBV> vbvs)
    {
        uint slot = 0;
        for (auto &vbv : vbvs)
        {
            setVBV(vbv, slot);
            ++slot;
        }
    }

    void CommandList::setIBV(const BufferIBV & ibv)
    {
        transition(ibv.m_buffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        cmd()->IASetIndexBuffer(&ibv.m_ibv);
    }

    void CommandList::setShaderView(unsigned slot, const TextureSRV & srv)
    {
        transition(srv.m_texture,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        auto &srvs = S().srvs;
        if (srv)
            srvs[slot] = srv.S().descriptor.staging;
        else
            srvs[slot] = device().S().nullTextureSRV.staging;
    }

    void CommandList::setShaderView(unsigned slot, TextureUAV & uav)
    {
        transition(uav.m_texture,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        auto &uavs = S().uavs;
        if (uav)
            uavs[slot] = uav.S().descriptor.staging;
        else
            uavs[slot] = device().S().nullTextureUAV.staging;
    }

    void CommandList::setShaderViewNullTextureSRV(unsigned slot)
    {
        S().srvs[slot] = device().S().nullTextureSRV.staging;
    }

    void CommandList::setShaderViewNullTextureUAV(unsigned slot)
    {
        S().uavs[slot] = device().S().nullTextureUAV.staging;
    }

    void CommandList::setConstantBuffer(unsigned slot, Span<const uint8_t> bytes)
    {
        auto block = uploadBytes(bytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        auto &cbvs = S().cbvs;
        cbvs[slot].BufferLocation = block.heap->GetGPUVirtualAddress() + block.block.begin;
        cbvs[slot].SizeInBytes    = roundUpToMultiple<uint>(
            static_cast<uint>(bytes.sizeBytes()),
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    }

    void CommandList::setTopology(D3D_PRIMITIVE_TOPOLOGY topology)
    {
        cmd()->IASetPrimitiveTopology(topology);
    }

    void CommandList::draw(uint vertices, uint startVertex)
    {
        transition(S().activeRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        setupRootArguments(false);
        cmd()->DrawInstanced(vertices, 1, startVertex, 0);
    }

    void CommandList::drawIndexed(uint indices, uint startIndex)
    {
        transition(S().activeRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        setupRootArguments(false);
        cmd()->DrawIndexedInstanced(indices, 1, startIndex, 0, 0); 
    }

    void CommandList::dispatch(uint3 threadGroups)
    {
        threadGroups = max(uint3(1), threadGroups);
        setupRootArguments(true);
        cmd()->Dispatch(threadGroups.x, threadGroups.y, threadGroups.z);
    }

    void CommandList::updateBuffer(Buffer & buffer,
                                   Span<const uint8_t> data,
                                   size_t offset)
    {
        auto block = uploadBytes(data, 1);

        transition(buffer, D3D12_RESOURCE_STATE_COPY_DEST);
        S().cmd->CopyBufferRegion(
            buffer.get(),
            offset,
            block.heap,
            static_cast<UINT64>(block.block.begin),
            block.block.size());
    }

    void CommandList::updateTexture(Texture & texture, ImageData data, ImageRect dstPos)
    {
        auto block = uploadBytes(data.data, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource                   = texture.get();
        dst.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex            = dstPos.subresource.index(texture->mipLevels);

        D3D12_TEXTURE_COPY_LOCATION src        = {};
        src.pResource                          = block.heap;
        src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset             = static_cast<UINT64>(block.block.begin);
        src.PlacedFootprint.Footprint.Format   = data.format;
        src.PlacedFootprint.Footprint.Width    = data.size.x;
        src.PlacedFootprint.Footprint.Height   = data.size.y;
        src.PlacedFootprint.Footprint.Depth    = 1;
        src.PlacedFootprint.Footprint.RowPitch = data.pitch;

        transition(texture, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd()->CopyTextureRegion(
            &dst,
            dstPos.leftTop.x, dstPos.leftTop.y, 0,
            &src,
            nullptr);
    }

    void CommandList::copyTexture(Texture & dst, ImageRect dstPos,
                                  const Texture & src, ImageRect srcRect)
    {
        transition(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(dst, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource                   = dst.get();
        dstLocation.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex            = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource                   = src.get();
        srcLocation.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex            = 0;

        D3D12_BOX srcBox = {};
        srcBox.left      = srcRect.leftTop.x;
        srcBox.right     = srcRect.rightBottom.x;
        srcBox.top       = srcRect.leftTop.y;
        srcBox.bottom    = srcRect.rightBottom.y;
        srcBox.front     = 0;
        srcBox.back      = 1;

        cmd()->CopyTextureRegion(
            &dstLocation, dstPos.leftTop.x, dstPos.leftTop.y, 0,
            &srcLocation, srcRect.empty() ? nullptr : &srcBox);
    }

    void CommandList::imguiBeginFrame(SwapChain &swapChain, double deltaTime)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = float2(swapChain.backbuffer(false).m_texture->size);
        io.DeltaTime   = static_cast<float>(deltaTime);

        static HCURSOR arrow = LoadCursorA(nullptr, IDC_ARROW);

        if (io.MouseDrawCursor)
            SetCursor(nullptr);
        else
            SetCursor(arrow);

        ImGui::NewFrame();

		device().processProfilingEvents();
    }

    void CommandList::imguiEndFrame(SwapChain &swapChain)
    {
        auto rtv = swapChain.backbuffer(false);

        ImGui::Render();

        auto drawData = ImGui::GetDrawData();
        XOR_ASSERT(drawData->Valid, "ImGui draw data is invalid!");

        auto &imgui = device().S().imgui;

        setRenderTargets(rtv);
        bind(imgui.imguiRenderer);

        uint2 resolution = rtv.texture()->size;
        int4 prevClipRect = -1;

        ImguiRenderer::Constants constants;
        constants.reciprocalResolution = 1.f / float2(resolution);

        for (int i = 0; i < drawData->CmdListsCount; ++i)
        {
            auto list = drawData->CmdLists[i];
            auto vbv = dynamicBufferVBV(asConstSpan(list->VtxBuffer));
            auto ibv = dynamicBufferIBV(asConstSpan(list->IdxBuffer));
            setVBV(vbv);
            setIBV(ibv);
            setTopology();

            uint indexOffset  = 0;
            for (auto &d : list->CmdBuffer)
            {
				if (d.UserCallback)
				{
					d.UserCallback(list, &d);
				}
				else
				{
					int4 clipRect = int4(float4(d.ClipRect));
					// TODO: swizzle
					if (any(clipRect != prevClipRect))
						setScissor({ int2(clipRect.x, clipRect.y), int2(clipRect.z, clipRect.w) });

					setConstants(constants);
					setShaderView(ImguiRenderer::tex, imgui.fontAtlas);
					drawIndexed(d.ElemCount, indexOffset);
				}

                indexOffset += d.ElemCount;
            }
        }

        setRenderTargets();
    }

    ProfilingEvent CommandList::profilingEventInternal(const char * name, bool print)
    {
        ProfilingEvent e;
		e.m_cmd       = cmd();
		e.m_queryHeap = S().queryHeap.get();
		e.m_offset    = e.m_queryHeap->beginEvent(e.m_cmd, name, print, number());

		if (S().firstProfilingEvent < 0)
			S().firstProfilingEvent = e.m_offset;

		S().lastProfilingEvent = e.m_offset;

		return e;
    }

    ProfilingEvent CommandList::profilingEvent(const char * name)
    {
        return profilingEventInternal(name, false);
    }

    ProfilingEvent CommandList::profilingEventPrint(const char * name)
    {
        return profilingEventInternal(name, true);
    }

	void ProfilingEvent::done()
	{
		if (m_queryHeap)
		{
			XOR_ASSERT(static_cast<bool>(m_offset), "No valid event offset");
			m_queryHeap->endEvent(m_cmd.get(), m_offset);
			m_queryHeap = nullptr;
		}
	}
}
