#pragma once

#include "Core/Core.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <vector>
#include <initializer_list>
#include <queue>
#include <memory>

namespace xor
{
    class Device;

    class Format
    {
        uint16_t m_dxgiFormat  = static_cast<uint16_t>(DXGI_FORMAT_UNKNOWN);
        uint16_t m_elementSize = 0;
    public:
        Format(DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN)
            : m_dxgiFormat(static_cast<uint16_t>(format))
        {}

        static Format structure(size_t structSize)
        {
            Format f;
            XOR_ASSERT(structSize <= std::numeric_limits<uint16_t>::max(),
                       "Struct sizes above 64k not supported.");
            f.m_elementSize = static_cast<uint16_t>(structSize);
            return f;
        }
        template <typename T>
        static Format structure() { return structure(sizeof(T)); }

        DXGI_FORMAT dxgiFormat() const
        {
            return static_cast<DXGI_FORMAT>(m_dxgiFormat);
        }

        explicit operator bool() const
        {
            return (dxgiFormat() != DXGI_FORMAT_UNKNOWN) || m_elementSize;
        }

        operator DXGI_FORMAT() const { return dxgiFormat(); }
    };

    namespace backend
    {
        struct Descriptor;
        struct ViewHeap;
        struct ShaderLoader;
        struct DeviceState;
        struct CommandListState;
        struct SwapChainState;
        struct ResourceState;
        struct DescriptorViewState;
        struct PipelineState;
        struct BufferState;

        template <typename T>
        struct SharedState
        {
            using Weak = std::weak_ptr<T>;

            std::shared_ptr<T> m_state;
            void makeState() { m_state = std::make_shared<T>(); }
            Weak weak() { return m_state; }
            T &S() { return *m_state; }
        };

        class BufferViewInfo
        {
        public:
            size_t firstElement = 0;
            size_t numElements  = 0;
            Format format;
        };

        class BufferViewInfoBuilder : public BufferViewInfo 
        {
        public:
            BufferViewInfoBuilder() = default;
            BufferViewInfoBuilder(const BufferViewInfo &info) : BufferViewInfo(info) {}

            BufferViewInfoBuilder &firstElement(size_t index) { BufferViewInfo::firstElement = index; return *this; }
            BufferViewInfoBuilder &numElements(size_t count) { BufferViewInfo::numElements = count; return *this; }
            BufferViewInfoBuilder &format(Format format) { BufferViewInfo::format = format; return *this; }
        };
    }

    class Adapter
    {
        friend class Xor;

        ComPtr<IDXGIAdapter3>                  m_adapter;
        std::shared_ptr<backend::ShaderLoader> m_shaderLoader;
        String                                 m_description;
        bool                                   m_debug = false;
    public:
        Device createDevice();
    };

    class SwapChain;
    class CommandList;

    class Pipeline : private backend::SharedState<backend::PipelineState>
    {
        friend class Device;
        friend class CommandList;
    public:
        Pipeline() = default;

        class Graphics : public D3D12_GRAPHICS_PIPELINE_STATE_DESC
        {
            friend class Device;
            friend class Pipeline;
            friend struct backend::PipelineState;
            String m_vs;
            String m_ps;
        public:
            Graphics();

            Graphics &vertexShader(const String &vsName);
            Graphics &pixelShader(const String &psName);
            Graphics &renderTargetFormats(std::initializer_list<DXGI_FORMAT> formats);
            Graphics &multisampling(uint samples, uint quality = 0);
            Graphics &topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type);
            Graphics &fill(D3D12_FILL_MODE fillMode);
            Graphics &cull(D3D12_CULL_MODE cullMode);
        };
    };

    class Resource : private backend::SharedState<backend::ResourceState>
    {
        friend class Device;
        friend class CommandList;
    public:
        Resource() = default;

        D3D12_RESOURCE_DESC desc() const;
        ID3D12Resource *get();
    };

    class Buffer : public Resource
    {
        friend class Device;
        friend class CommandList;
    public:
        Buffer() = default;

        class Info
        {
            std::function<void(CommandList &cmd)> m_initializer;
            D3D12_RESOURCE_DESC get();
        public:
            size_t size = 0;
            Format format;

            Info() = default;
            Info(size_t sizeBytes) : size(sizeBytes) {}
            Info(size_t size, Format format)
                : size(size)
                , format(format)
            {}
            Info(span<const uint8_t> data, Format format);

            template <typename T>
            Info(span<const T> data, Format format = Format::structure<T>())
                : Info(as_bytes(data), format)
            {}
        };

        class Builder : public Info
        {
        public:
            Builder() = default;
            Builder(const Info &info) : Info(info) {}
            Builder &size(size_t sz)    { Info::size = sz; return *this; }
            Builder &format(Format fmt) { Info::format = fmt; return *this; }
        };

        const Info *operator->() const { return m_info.get(); }
    private:
        std::shared_ptr<const Info> m_info;
    };

    class Texture : public Resource
    {
        friend class Device;
        friend class CommandList;
    public:
        Texture() = default;
    };

    class DescriptorView : private backend::SharedState<backend::DescriptorViewState>
    {
        friend class Device;
        friend class CommandList;
    public:
        DescriptorView() = default;
    };

    class RTV : public DescriptorView
    {
        friend class Device;
        friend class CommandList;
        Texture m_texture;
    public:
        Texture texture();
    };

    class BufferVBV
    {
        friend class Device;
        friend class CommandList;
        Buffer                   m_buffer;
        D3D12_VERTEX_BUFFER_VIEW m_vbv;
    public:
        BufferVBV() = default;
        Buffer buffer();

        using Info    = backend::BufferViewInfo;
        using Builder = backend::BufferViewInfoBuilder;
    };

    class Device : private backend::SharedState<backend::DeviceState>
    {
        friend class Adapter;
        friend class CommandList;
        friend class DescriptorView;
        friend class SwapChain;
        friend struct backend::DeviceState;
        friend struct backend::CommandListState;
        friend struct backend::SwapChainState;
        friend struct backend::ResourceState;
        friend struct backend::DescriptorViewState;
        friend struct backend::PipelineState;

        static Device parent(Weak &parentDevice);

        ID3D12Device *device();
        std::shared_ptr<backend::CommandListState> createCommandList();
        void collectRootSignature(const D3D12_SHADER_BYTECODE &shader);

        Device(ComPtr<IDXGIAdapter3> adapter,
               ComPtr<ID3D12Device> device,
               std::shared_ptr<backend::ShaderLoader> shaderLoader);
    public:
        Device() = default;

        explicit operator bool() const { return static_cast<bool>(m_state); }

        SwapChain createSwapChain(Window &window);
        Pipeline createGraphicsPipeline(const Pipeline::Graphics &info);

        Buffer    createBuffer(const Buffer::Info &info);
        BufferVBV createBufferVBV(Buffer buffer                 , const BufferVBV::Info &viewInfo = BufferVBV::Info());
        BufferVBV createBufferVBV(const Buffer::Info &bufferInfo, const BufferVBV::Info &viewInfo = BufferVBV::Info());

        CommandList graphicsCommandList();

        void execute(CommandList &cmd);
        void present(SwapChain &swapChain, bool vsync = true);

        SeqNum now();
        void whenCompleted(std::function<void()> f);
        void whenCompleted(std::function<void()> f, SeqNum seqNum);
        bool hasCompleted(SeqNum seqNum);
        void waitUntilCompleted(SeqNum seqNum);
        void waitUntilDrained();
    };

    class Barrier : public D3D12_RESOURCE_BARRIER
    {
    public:
    };

    Barrier transition(Resource &resource,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after,
                       uint subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    class SwapChain : private backend::SharedState<backend::SwapChainState>
    {
        friend class Device;
        uint currentIndex();

    public:
        SwapChain() = default;

        RTV backbuffer();
    };

    class CommandList : private backend::SharedState<backend::CommandListState>
    {
        friend class Device;
        friend struct backend::DeviceState;

        ID3D12GraphicsCommandList *cmd();
        void close();
        void reset();

        CommandList(Device &device);
        bool hasCompleted();
        void waitUntilCompleted(DWORD timeout = INFINITE);

    public:
        CommandList() {}
        ~CommandList();

        CommandList(CommandList &&) = default;
        CommandList& operator=(CommandList &&) = default;

        CommandList(const CommandList &) = delete;
        CommandList& operator=(const CommandList &) = delete;

        void bind(Pipeline &pipeline);

        void clearRTV(RTV &rtv, float4 color = 0);
        void setRenderTargets();
        void setRenderTargets(RTV &rtv);

        void barrier(std::initializer_list<Barrier> barriers);

        void draw(uint vertices, uint startVertex = 0);
    };

    // Global initialization and deinitialization of the Xor renderer.
    class Xor
    {
    public:
        enum class DebugLayer
        {
            Enabled,
            Disabled,
        };

    private:
        std::vector<Adapter>                   m_adapters;
        std::shared_ptr<backend::ShaderLoader> m_shaderLoader;

    public:

        Xor(DebugLayer debugLayer = DebugLayer::Enabled);
        ~Xor();

        span<Adapter> adapters();
        Adapter &defaultAdapter();
        Device defaultDevice();

        void registerShaderTlog(StringView projectName,
                                StringView shaderTlogPath);
    };
}

