#pragma once

#include "Core/Core.hpp"
#include "Core/TLog.hpp"

#include "Xor/XorBackend.hpp"
#include "Xor/Image.hpp"

#include <unordered_map>

namespace xor
{
    class GraphicsPipeline;
    class ComputePipeline;

    namespace info
    {
        struct SameShader {};

        enum class DepthMode
        {
            Disabled,
            ReadOnly,
            Write,
        };

        template <typename Resource>
        class ResourceInitializer
        {
            friend class xor::Device;
            std::function<void(Device &device, Resource &res)>   m_withDevice;
            std::function<void(CommandList &cmd, Resource &res)> m_withCommandList;
        public:
            ResourceInitializer() = default;
            ResourceInitializer(std::function<void(Device &device, Resource &res)> initWithDevice)
                : m_withDevice(std::move(initWithDevice))
            {}
            ResourceInitializer(std::function<void(CommandList &cmd, Resource &res)> initWithCommandList)
                : m_withCommandList(std::move(initWithCommandList))
            {}

            explicit operator bool() const
            {
                return static_cast<bool>(m_withDevice) ||
                    static_cast<bool>(m_withCommandList);
            }
        };

        class BufferInfo
        {
        protected:
            ResourceInitializer<Buffer> m_initializer;
            friend class Device;

            void initializeWith(Span<const uint8_t> data);
        public:
            size_t size = 0;
            Format format;
            bool allowUAV = false;

            BufferInfo() = default;
            BufferInfo(size_t size, Format format)
                : size(size)
                , format(format)
            {}

            static BufferInfo fromBytes(Span<const uint8_t> data, Format format);

            template <typename T>
            static BufferInfo fromSpan(T &&span, Format format = Format::structure<ElementType<T>>())
            {
                return fromBytes(asBytes(span), format);
            }

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
            BufferInfoBuilder &rawBuffer(size_t sizeInBytes) { size(sizeInBytes / sizeof(uint32_t)); return format(DXGI_FORMAT_R32_TYPELESS); }
            BufferInfoBuilder &initialData(Span<const uint8_t> data) { BufferInfo::initializeWith(data); return *this; }
            BufferInfoBuilder &allowUAV(bool allowUAV = true) { BufferInfo::allowUAV = allowUAV; return *this; }
        };

        class BufferViewInfo
        {
        public:
            size_t firstElement = 0;
            uint   numElements  = 0;
            Format format;

            BufferViewInfo defaults(const BufferInfo &bufferInfo,
                                    bool shaderView = false) const;
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
            BufferViewInfoBuilder &raw() { BufferViewInfo::format = DXGI_FORMAT_R32_TYPELESS; return *this; }
        };

        class TextureInfo
        {
        protected:
            ResourceInitializer<Texture> m_initializer;
            friend class Device;
        public:
            uint2 size;
            Format format;
            uint mipLevels = 1;
            bool allowRenderTarget = false;
            bool allowDepthStencil = false;
            bool allowUAV          = false;

            TextureInfo() = default;
            TextureInfo(uint2 size, Format format)
                : size(size)
                , format(format)
            {}
            TextureInfo(const Image &image, Format fmt = Format());
            TextureInfo(const ImageData &image, Format fmt = Format());
            TextureInfo(ID3D12Resource *texture);

            size_t sizeBytes() const;

            float2 sizeFloat() const { return float2(size); }
        };

        class TextureInfoBuilder : public TextureInfo
        {
        public:
            using Info = TextureInfo;

            TextureInfoBuilder() = default;
            TextureInfoBuilder(const TextureInfo &info) : TextureInfo(info) {}

            TextureInfoBuilder &size(uint2 sz)    { TextureInfo::size = sz; return *this; }
            TextureInfoBuilder &format(Format fmt) { TextureInfo::format = fmt; return *this; }
            TextureInfoBuilder &mipLevels(uint mips) { TextureInfo::mipLevels = mips; return *this; }
            TextureInfoBuilder &allowRenderTarget(bool allowRTV = true) { TextureInfo::allowRenderTarget = allowRTV; return *this; }
            TextureInfoBuilder &allowDepthStencil(bool allowDSV = true) { TextureInfo::allowDepthStencil = allowDSV; return *this; }
            TextureInfoBuilder &allowUAV(bool allowUAV = true) { TextureInfo::allowUAV = allowUAV; return *this; }
        };

        class TextureViewInfo
        {
        public:
            Format format;

            TextureViewInfo() = default;
            TextureViewInfo(Format format) : format(format) {}

            TextureViewInfo defaults(const TextureInfo &textureInfo,
                                     bool shaderView = false) const;
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
            D3D12_INPUT_ELEMENT_DESC operator[](size_t i) const { return m_elements[i]; }
            const D3D12_INPUT_ELEMENT_DESC *begin() const { return m_elements.data(); }
            const D3D12_INPUT_ELEMENT_DESC *end()   const { return m_elements.data() + m_elements.size(); }
            void hash(Hash &h) const;
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

        using PipelineKey = uint64_t;

        struct ShaderDefine
        {
            String define;
            String value;

            ShaderDefine(String define = {}, String value = {})
                : define(std::move(define))
                , value(std::move(value))
            {}

            ShaderDefine(String define, int intValue)
                : define(std::move(define))
                , value(String::format("%d", intValue))
            {}
        };

        struct ShaderDesc
        {
            String shader;
            std::vector<ShaderDefine> defines;

            explicit operator bool() const { return static_cast<bool>(shader); }
            void hash(Hash &h) const;
            String path() const;
            String basePath() const;
        };

        class GraphicsPipelineInfo : private D3D12_GRAPHICS_PIPELINE_STATE_DESC
        {
            friend class Device;
            friend class xor::GraphicsPipeline;
            friend struct backend::PipelineState;
            ShaderDesc                             m_vs;
            ShaderDesc                             m_ps;
            std::shared_ptr<info::InputLayoutInfo> m_inputLayout;
        public:
            GraphicsPipelineInfo();

            GraphicsPipelineInfo &vertexShader(const String &vsName, Span<const ShaderDefine> defines = {});
            GraphicsPipelineInfo &vertexShader(SameShader, Span<const ShaderDefine> defines = {});
            GraphicsPipelineInfo &pixelShader();
            GraphicsPipelineInfo &pixelShader(const String &psName, Span<const ShaderDefine> defines = {});
            GraphicsPipelineInfo &pixelShader(SameShader, Span<const ShaderDefine> defines);
            GraphicsPipelineInfo &renderTargetFormat();
            GraphicsPipelineInfo &renderTargetFormat(Format format);
            GraphicsPipelineInfo &renderTargetFormat(DXGI_FORMAT format);
            GraphicsPipelineInfo &renderTargetFormats(Span<const Format> formats);
            GraphicsPipelineInfo &renderTargetFormats(std::initializer_list<DXGI_FORMAT> formats);
            GraphicsPipelineInfo &depthFormat(Format format);
            GraphicsPipelineInfo &depthMode(info::DepthMode mode);
            GraphicsPipelineInfo &depthFunction(D3D12_COMPARISON_FUNC testFunction);
            GraphicsPipelineInfo &depthBias(int bias, float slopeScaled = 0, float clamp = 0);
            GraphicsPipelineInfo &inputLayout(const info::InputLayoutInfo &ilInfo);
            GraphicsPipelineInfo &multisampling(uint samples, uint quality = 0);
            GraphicsPipelineInfo &topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type);
            GraphicsPipelineInfo &fill(D3D12_FILL_MODE fillMode);
            GraphicsPipelineInfo &cull(D3D12_CULL_MODE cullMode);
            GraphicsPipelineInfo &winding(bool counterClockWise);
            GraphicsPipelineInfo &blend(uint renderTarget,
                                        bool enabled = false,
                                        D3D12_BLEND src = D3D12_BLEND_ONE,
                                        D3D12_BLEND dst = D3D12_BLEND_INV_SRC_ALPHA,
                                        D3D12_BLEND_OP op = D3D12_BLEND_OP_ADD);
            GraphicsPipelineInfo &antialiasedLine(bool lineAA);

            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc() const;
            PipelineKey key() const;
        };

        class ComputePipelineInfo
        {
            friend class Device;
            friend class xor::ComputePipeline;
            friend struct backend::PipelineState;
            ShaderDesc                             m_cs;
        public:
            ComputePipelineInfo();
            ComputePipelineInfo(const String &csName, Span<const ShaderDefine> defines = {});

            ComputePipelineInfo &computeShader(const String &csName, Span<const ShaderDefine> defines = {});
            ComputePipelineInfo &computeShader(SameShader, Span<const ShaderDefine> defines = {});

            PipelineKey key() const;
        };
    }

    namespace backend
    {
        struct ShaderLoader
        {
            struct ShaderData
            {
                std::shared_ptr<const BuildInfo> buildInfo;
                std::unordered_map<PipelineState *, std::weak_ptr<PipelineState>> users;
                uint64_t timestamp = 0;

                void rebuildPipelines();
                bool isOutOfDate() const
                {
                    return timestamp < buildInfo->sourceTimestamp();
                }
            };

            std::unordered_map<String, std::shared_ptr<ShaderData>> shaderData;
            std::vector<String> shaderScanQueue;
            size_t shaderScanIndex = 0;

            void scanChangedSources();
            void registerBuildInfo(std::shared_ptr<const BuildInfo> buildInfo);

            void registerShaderTlog(StringView projectName,
                                    StringView shaderTlogPath);
        };

        struct ResourceState : DeviceChild
        {
            ComPtr<ID3D12Resource> resource;
            mutable D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

            ~ResourceState();
        };

        struct DescriptorViewState : DeviceChild
        {
            Descriptor descriptor;

            ~DescriptorViewState();
        };

        struct RootSignature
        {
            ComPtr<ID3D12RootSignature> rs;
            unsigned numCBVs = 0;
            unsigned numSRVs = 0;
            unsigned numUAVs = 0;
        };

        struct ShaderBinary;
        using info::ShaderDesc;

        struct PipelineState
            : std::enable_shared_from_this<PipelineState>
            , DeviceChild
        {
            std::shared_ptr<info::GraphicsPipelineInfo> graphicsInfo;
            std::shared_ptr<info::ComputePipelineInfo> computeInfo;
            ComPtr<ID3D12PipelineState> pso;
            RootSignature rootSignature;

            backend::ShaderBinary loadShader(ShaderLoader &loader, const ShaderDesc &shader);

            void reload();
            void releasePSO();

            ~PipelineState();
        };

    }

    class GraphicsPipeline : private backend::SharedState<backend::PipelineState>
    {
        friend class Device;
        friend class CommandList;
    public:
        GraphicsPipeline() = default;

        using Info = info::GraphicsPipelineInfo;

        Info variant() const;
    };

    class ComputePipeline : private backend::SharedState<backend::PipelineState>
    {
        friend class Device;
        friend class CommandList;
    public:
        ComputePipeline() = default;

        using Info = info::ComputePipelineInfo;

        Info variant() const;
    };

    class Buffer : public backend::ResourceWithInfo<info::BufferInfoBuilder>
    {
    public:
        Buffer() = default;

        using Info    = info::BufferInfo;
        using Builder = info::BufferInfoBuilder;
    };

    class Texture : public backend::ResourceWithInfo<info::TextureInfoBuilder>
    {
    public:
        Texture() = default;

        using Info    = info::TextureInfo;
        using Builder = info::TextureInfoBuilder;
    };

    class DescriptorView : private backend::SharedState<backend::DescriptorViewState>
    {
        friend class Device;
        friend class CommandList;
    public:
        DescriptorView() = default;

        bool valid() const { return backend::SharedState<backend::DescriptorViewState>::valid(); }
        explicit operator bool() const { return valid(); }
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

    class TextureDSV : public TextureView
    {
    public:
    };

    class TextureSRV : public TextureView
    {
    public:
    };

    class TextureUAV : public TextureView
    {
    public:
    };

    class BufferView : public DescriptorView
    {
        friend class Device;
        friend class CommandList;
        Buffer m_buffer;
    public:
        using Info    = info::BufferViewInfo;
        using Builder = info::BufferViewInfoBuilder;

        Buffer buffer();
    };

    class BufferSRV : public BufferView
    {
    public:
    };

    class BufferUAV : public BufferView
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

    struct RWTexture
    {
        TextureSRV srv;
        TextureUAV uav;
        TextureRTV rtv;
        TextureDSV dsv;

        RWTexture() = default;
        RWTexture(Device &device,
                  const info::TextureInfo &info,
                  const info::TextureViewInfo &viewInfo = info::TextureViewInfo());

        bool valid() const { return srv.valid(); }
        explicit operator bool() const { return valid(); }

        Texture texture() { return srv.texture(); }
    };
}
