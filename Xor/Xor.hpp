#pragma once

#include "Core/Core.hpp"
#include "Xor/Image.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <vector>
#include <initializer_list>
#include <queue>
#include <memory>

// TODO: Unify all Info classes
// TODO: makeState() should support proper constructors
// TODO: DeviceChild subclass for SharedState to get rid of some boilerplate
// TODO: More convenience subclasses for SharedState to allow easy access to stuff
// TODO: Split into multiple headers and cpps

namespace xor
{
    class Device;
    class CommandList;
    class Buffer;

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

        uint size() const;
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
        struct GPUProgressTracking;

        template <typename T>
        struct SharedState
        {
            using Weak = std::weak_ptr<T>;

            std::shared_ptr<T> m_state;
            void makeState() { m_state = std::make_shared<T>(); }
            Weak weak() { return m_state; }
            T &S() { return *m_state; }
        };
    }

    namespace info
    {
        class BufferInfo
        {
        protected:
            std::function<void(CommandList &cmd, Buffer &buf)> m_initializer;
            friend class Device;
        public:
            size_t size = 0;
            Format format;

            BufferInfo() = default;
            BufferInfo(size_t size, Format format)
                : size(size)
                , format(format)
            {}

            static BufferInfo fromBytes(Span<const uint8_t> data, Format format);

            template <typename T>
            BufferInfo(Span<const T> data, Format format = Format::structure<T>())
            {
                *this = fromBytes(asBytes(data), format);
            }

            template <typename T>
            BufferInfo(std::initializer_list<T> data, Format format = Format::structure<T>())
                : BufferInfo(asConstSpan(data), format)
            {}

            size_t sizeBytes() const { return size * format.size(); }
        };

        class BufferInfoBuilder : public BufferInfo
        {
        public:
            BufferInfoBuilder() = default;
            BufferInfoBuilder(const BufferInfo &info) : BufferInfo(info) {}
            BufferInfoBuilder &size(size_t sz)    { BufferInfo::size = sz; return *this; }
            BufferInfoBuilder &format(Format fmt) { BufferInfo::format = fmt; return *this; }
        };

        class BufferViewInfo
        {
        public:
            size_t firstElement = 0;
            uint   numElements  = 0;
            Format format;

            BufferViewInfo defaults(const BufferInfo &bufferInfo) const;
            uint sizeBytes() const;
        };

        class BufferViewInfoBuilder : public BufferViewInfo 
        {
        public:
            BufferViewInfoBuilder() = default;
            BufferViewInfoBuilder(const BufferViewInfo &info) : BufferViewInfo(info) {}

            BufferViewInfoBuilder &firstElement(size_t index) { BufferViewInfo::firstElement = index; return *this; }
            BufferViewInfoBuilder &numElements(uint count) { BufferViewInfo::numElements = count; return *this; }
            BufferViewInfoBuilder &format(Format format) { BufferViewInfo::format = format; return *this; }
        };

        class InputLayoutInfo
        {
        protected:
            std::vector<D3D12_INPUT_ELEMENT_DESC> m_elements;
            friend class Device;
        public:
            D3D12_INPUT_LAYOUT_DESC desc() const;
        };

        class InputLayoutInfoBuilder : public InputLayoutInfo
        {
        public:
            InputLayoutInfoBuilder &element(const char *semantic, uint index,
                                            Format format,
                                            uint inputSlot = 0,
                                            uint offset = D3D12_APPEND_ALIGNED_ELEMENT)
            {
                m_elements.emplace_back();
                auto &e                = m_elements.back();
                e.SemanticName         = semantic;
                e.SemanticIndex        = index;
                e.Format               = format;
                e.InputSlot            = inputSlot;
                e.AlignedByteOffset    = offset;
                e.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                e.InstanceDataStepRate = 0;
                return *this;
            }
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

        class Graphics : private D3D12_GRAPHICS_PIPELINE_STATE_DESC
        {
            friend class Device;
            friend class Pipeline;
            friend struct backend::PipelineState;
            String                                 m_vs;
            String                                 m_ps;
            std::shared_ptr<info::InputLayoutInfo> m_inputLayout;
        public:
            Graphics();

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc() const;

            Graphics &vertexShader(const String &vsName);
            Graphics &pixelShader(const String &psName);
            Graphics &renderTargetFormats(std::initializer_list<DXGI_FORMAT> formats);
            Graphics &inputLayout(const info::InputLayoutInfo &ilInfo);
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

        using Info    = info::BufferInfo;
        using Builder = info::BufferInfoBuilder;

        const Info &info() const { return *m_info; }
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
        Buffer buffer() { return m_buffer; }

        using Info    = info::BufferViewInfo;
        using Builder = info::BufferViewInfoBuilder;
    };

    class BufferIBV
    {
        friend class Device;
        friend class CommandList;
        Buffer                  m_buffer;
        D3D12_INDEX_BUFFER_VIEW m_ibv;
    public:
        BufferIBV() = default;
        Buffer buffer() { return m_buffer; }

        using Info    = info::BufferViewInfo;
        using Builder = info::BufferViewInfoBuilder;
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

        CommandList initializerCommandList();

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
        BufferIBV createBufferIBV(Buffer buffer                 , const BufferIBV::Info &viewInfo = BufferIBV::Info());
        BufferIBV createBufferIBV(const Buffer::Info &bufferInfo, const BufferIBV::Info &viewInfo = BufferIBV::Info());

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
        friend struct backend::GPUProgressTracking;

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

        SeqNum number() const;

        void bind(Pipeline &pipeline);

        void clearRTV(RTV &rtv, float4 color = 0);
        void setRenderTargets();
        void setRenderTargets(RTV &rtv);
        void setVBV(const BufferVBV &vbv);
        void setIBV(const BufferIBV &ibv);
        void setTopology(D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        void barrier(Span<const Barrier> barriers);

        void draw(uint vertices, uint startVertex = 0);
        void drawIndexed(uint indices, uint startIndex = 0);

        void updateBuffer(Buffer &buffer,
                          Span<const uint8_t> data,
                          size_t offset = 0);
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

        Span<Adapter> adapters();
        Adapter &defaultAdapter();
        Device defaultDevice();

        void registerShaderTlog(StringView projectName,
                                StringView shaderTlogPath);
    };
}

