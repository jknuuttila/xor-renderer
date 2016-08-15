#pragma once

#include "Core/Core.hpp"
#include "Xor/Image.hpp"
#include "Xor/Mesh.hpp"
#include "Xor/Shaders.h"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <vector>
#include <initializer_list>
#include <queue>
#include <memory>

// TODO: Unify all Info classes
// TODO: More convenience subclasses for SharedState to allow easy access to stuff
// TODO: Split into multiple headers and cpps
// TODO: viewHeap() is maybe needless, have a smarter way to locate descriptor heaps

namespace xor
{
    class Device;
    class CommandList;
    class Buffer;
    class Texture;

    namespace backend
    {
        class DeviceChild;
        struct Descriptor;
        class ViewHeap;
        struct ShaderLoader;
        struct DeviceState;
        struct CommandListState;
        struct SwapChainState;
        struct ResourceState;
        struct DescriptorViewState;
        struct PipelineState;
        struct BufferState;
        struct GPUProgressTracking;
        struct RootSignature;

        template <typename T>
        class SharedState
        {
        protected:
            using State    = T;
            using StatePtr = std::shared_ptr<T>;

            StatePtr m_state;
        public:

            template <typename... Ts>
            T &makeState(Ts &&... ts)
            {
                m_state = std::make_shared<T>(std::forward<Ts>(ts)...);
                return S();
            }

            const T &S() const { return *m_state; }
            T &S() { return *m_state; }

            bool valid() const { return !!m_state; }
        };

        class Resource : private backend::SharedState<backend::ResourceState>
        {
            friend class Device;
            friend class CommandList;
        public:
            Resource() = default;

            explicit operator bool() const { return valid(); }
            ID3D12Resource *get() const;
        };

        template <typename ResourceInfoBuilder>
        class ResourceWithInfo : public Resource
        {
            friend class Device;
            friend class CommandList;
        public:
            using Info    = typename ResourceInfoBuilder::Info;
            using Builder = ResourceInfoBuilder;

            const Info &info() const { return *m_info; }
            const Info *operator->() const { return m_info.get(); }

        protected:
            std::shared_ptr<Info> m_info;
            Info &makeInfo()
            {
                m_info = std::make_shared<Info>();
                return *m_info;
            }
        };

        struct HeapBlock
        {
            ID3D12Resource *heap = nullptr;
            Block block;
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
            using Info = BufferInfo;

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
            using Info = BufferViewInfo;

            BufferViewInfoBuilder() = default;
            BufferViewInfoBuilder(const BufferViewInfo &info) : BufferViewInfo(info) {}

            BufferViewInfoBuilder &firstElement(size_t index) { BufferViewInfo::firstElement = index; return *this; }
            BufferViewInfoBuilder &numElements(uint count) { BufferViewInfo::numElements = count; return *this; }
            BufferViewInfoBuilder &format(Format format) { BufferViewInfo::format = format; return *this; }
        };

        class TextureInfo
        {
        protected:
            std::function<void(CommandList &cmd, Texture &tex)> m_initializer;
            friend class Device;
        public:
            uint2 size;
            Format format;

            TextureInfo() = default;
            TextureInfo(uint2 size, Format format)
                : size(size)
                , format(format)
            {}
            TextureInfo(const Image &image, Format fmt = Format());
            TextureInfo(ID3D12Resource *texture);
        };

        class TextureInfoBuilder : public TextureInfo
        {
        public:
            using Info = TextureInfo;

            TextureInfoBuilder() = default;
            TextureInfoBuilder(const TextureInfo &info) : TextureInfo(info) {}
        };

        class TextureViewInfo
        {
        public:
            Format format;

            TextureViewInfo() = default;
            TextureViewInfo(Format format) : format(format) {}

            TextureViewInfo defaults(const TextureInfo &textureInfo) const;
        };

        class TextureViewInfoBuilder : public TextureViewInfo 
        {
        public:
            using Info = TextureViewInfo;

            TextureViewInfoBuilder() = default;
            TextureViewInfoBuilder(const TextureViewInfo &info) : TextureViewInfo(info) {}
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

    class GraphicsPipeline : private backend::SharedState<backend::PipelineState>
    {
        friend class Device;
        friend class CommandList;
    public:
        GraphicsPipeline() = default;

        class Info : private D3D12_GRAPHICS_PIPELINE_STATE_DESC
        {
            friend class Device;
            friend class GraphicsPipeline;
            friend struct backend::PipelineState;
            String                                 m_vs;
            String                                 m_ps;
            std::shared_ptr<info::InputLayoutInfo> m_inputLayout;
        public:
            Info();

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc() const;

            Info &vertexShader(const String &vsName);
            Info &pixelShader(const String &psName);
            Info &renderTargetFormats(std::initializer_list<DXGI_FORMAT> formats);
            Info &inputLayout(const info::InputLayoutInfo &ilInfo);
            Info &multisampling(uint samples, uint quality = 0);
            Info &topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type);
            Info &fill(D3D12_FILL_MODE fillMode);
            Info &cull(D3D12_CULL_MODE cullMode);
        };
    };

    class Buffer : public backend::ResourceWithInfo<info::BufferInfoBuilder>
    {
    public:
        Buffer() = default;
    };

    class Texture : public backend::ResourceWithInfo<info::TextureInfoBuilder>
    {
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

    class TextureView : public DescriptorView
    {
        friend class Device;
        friend class CommandList;
        Texture m_texture;
    public:
        using Info    = info::TextureViewInfo;
        using Builder = info::TextureViewInfoBuilder;

        Texture texture();
    };

    class TextureRTV : public TextureView
    {
    public:
    };

    class TextureSRV : public TextureView
    {
    public:
    };

    class BufferVBV
    {
        friend class Device;
        friend class CommandList;
        mutable Buffer           m_buffer;
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
        mutable Buffer          m_buffer;
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
        friend class backend::DeviceChild;
        friend struct backend::DeviceState;
        friend struct backend::CommandListState;
        friend struct backend::SwapChainState;
        friend struct backend::ResourceState;
        friend struct backend::DescriptorViewState;
        friend struct backend::PipelineState;

        ID3D12Device *device();

        CommandList initializerCommandList();

        backend::RootSignature collectRootSignature(const D3D12_SHADER_BYTECODE &shader);

        backend::HeapBlock uploadBytes(Span<const uint8_t> bytes, SeqNum cmdListNumber, uint alignment);

        Device(StatePtr state);
    public:
        Device() = default;

        explicit operator bool() const { return valid(); }

        SwapChain createSwapChain(Window &window);
        GraphicsPipeline createGraphicsPipeline(const GraphicsPipeline::Info &info);

        Buffer    createBuffer(const Buffer::Info &info);
        BufferVBV createBufferVBV(Buffer buffer                 , const BufferVBV::Info &viewInfo = BufferVBV::Info());
        BufferVBV createBufferVBV(const Buffer::Info &bufferInfo, const BufferVBV::Info &viewInfo = BufferVBV::Info());
        BufferIBV createBufferIBV(Buffer buffer                 , const BufferIBV::Info &viewInfo = BufferIBV::Info());
        BufferIBV createBufferIBV(const Buffer::Info &bufferInfo, const BufferIBV::Info &viewInfo = BufferIBV::Info());

        Texture    createTexture(const Texture::Info &info);
        TextureSRV createTextureSRV(Texture texture                 , const TextureSRV::Info &viewInfo = TextureSRV::Info());
        TextureSRV createTextureSRV(const Texture::Info &textureInfo, const TextureSRV::Info &viewInfo = TextureSRV::Info());

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

    class SwapChain : private backend::SharedState<backend::SwapChainState>
    {
        friend class Device;
        uint currentIndex();

    public:
        SwapChain() = default;

        TextureRTV backbuffer();
    };

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
        backend::HeapBlock uploadBytes(Span<const uint8_t> bytes, uint alignment);
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

        void clearRTV(TextureRTV &rtv, float4 color = 0);

        void setRenderTargets();
        void setRenderTargets(TextureRTV &rtv);

        void setVBV(const BufferVBV &vbv);
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
                           uint2 pos = 0,
                           Subresource sr = 0);

        void copyTexture(Texture &dst,       ImageRect dstPos,
                         const Texture &src, ImageRect srcArea = {});
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

