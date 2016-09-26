#pragma once

#include "Core/Core.hpp"

#include "Xor/Shaders.h"
#include "Xor/XorBackend.hpp"
#include "Xor/XorResources.hpp"

namespace xor
{
    namespace backend
    {
        struct CommandListState : DeviceChild
        {
            ComPtr<ID3D12CommandAllocator>    allocator;
            ComPtr<ID3D12GraphicsCommandList> cmd;

            uint64_t            timesStarted = 0;
            ComPtr<ID3D12Fence> timesCompleted;
            Handle              completedEvent;

            SeqNum seqNum = 0;
            bool closed = false;

            Texture activeRenderTarget;

            std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> cbvs;
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvs;
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> uavs;
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> viewDescriptorSrcs;

            CommandListState(Device &dev);
        };
    }

    class CommandList : private backend::SharedState<backend::CommandListState>
    {
        friend class Device;
        friend struct backend::DeviceState;
        friend struct backend::GPUProgressTracking;

        ID3D12GraphicsCommandList *cmd();

        void close();
        void reset();

        bool hasCompleted();
        void waitUntilCompleted(DWORD timeout = INFINITE);

        CommandList(StatePtr state);
        void release();

        // FIXME: This is horribly inefficient and bad
        void transition(const backend::Resource &resource, D3D12_RESOURCE_STATES state);
        void setupRootArguments();
        backend::HeapBlock uploadBytes(Span<const uint8_t> bytes, uint alignment = DefaultAlignment);
    public:
        CommandList() = default;

        ~CommandList();
        CommandList(CommandList &&c);
        CommandList& operator=(CommandList &&c);

        CommandList(const CommandList &) = delete;
        CommandList& operator=(const CommandList &) = delete;

        explicit operator bool() const { return valid(); }
        SeqNum number() const;

        Device device();

        void bind(GraphicsPipeline &pipeline);
        void bind(const info::GraphicsPipelineInfo &pipelineInfo);

        void clearRTV(TextureRTV &rtv, float4 color = 0);
        void clearDSV(TextureDSV &dsv, float depth = 0);

        void setViewport(uint2 size);
        void setViewport(uint2 size, Rect scissor);
        void setScissor(Rect scissor);
        void setRenderTargets();
        void setRenderTargets(TextureRTV &rtv);
        void setRenderTargets(TextureRTV &rtv, TextureDSV &dsv);

        template <typename T>
        inline BufferVBV dynamicBufferVBV(Span<const T> vertices);
        BufferVBV dynamicBufferVBV(Span<const uint8_t> bytes, uint stride);

        template <typename T>
        inline BufferIBV dynamicBufferIBV(Span<const T> indices);
        BufferIBV dynamicBufferIBV(Span<const uint8_t> bytes, Format format);

        void setVBV(const BufferVBV &vbv, uint index = 0);
        void setVBVs(Span<const BufferVBV> vbvs);
        void setIBV(const BufferIBV &ibv);

        void setShaderView(unsigned slot, const TextureSRV &srv);

        void setConstantBuffer(unsigned slot, Span<const uint8_t> bytes);
        template <typename T>
        void setConstants(unsigned slot, const T &t)
        {
            setConstantBuffer(slot, Span<const uint8_t>(
                reinterpret_cast<const uint8_t *>(&t), sizeof(t)));
        }

        template <typename T, unsigned Slot>
        void setConstants(const backend::ShaderCBuffer<T, Slot> &constants)
        {
            setConstants(Slot, static_cast<const T &>(constants));
        }

        void setTopology(D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        void draw(uint vertices, uint startVertex = 0);
        void drawIndexed(uint indices, uint startIndex = 0);

        void updateBuffer(Buffer &buffer,
                          Span<const uint8_t> data,
                          size_t offset = 0);
        void updateTexture(Texture &texture,
                           ImageData data,
                           ImageRect dstPos = {});

        void copyTexture(Texture &dst,       ImageRect dstPos,
                         const Texture &src, ImageRect srcArea = {});

        void imguiBeginFrame(SwapChain &swapChain, double deltaTime);
        void imguiEndFrame(SwapChain &swapChain);
    };

    template<typename T>
    inline BufferVBV CommandList::dynamicBufferVBV(Span<const T> vertices)
    {
        return dynamicBufferVBV(asBytes(vertices), sizeof(T));
    }

    template<typename T>
    inline BufferIBV CommandList::dynamicBufferIBV(Span<const T> indices)
    {
        if (sizeof(T) == 2)
        {
            return dynamicBufferIBV(asBytes(indices), DXGI_FORMAT_R16_UINT);
        }
        else if (sizeof(T) == 4)
        {
            return dynamicBufferIBV(asBytes(indices), DXGI_FORMAT_R32_UINT);
        }
        else
        {
            XOR_CHECK(false, "Invalid index size");
            __assume(0);
        }
    }
}
