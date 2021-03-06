#pragma once

#include "Core/Core.hpp"

#include "Xor/Shaders.h"

#include "Xor/XorResources.hpp"
#include "Xor/XorBackend.hpp"

namespace Xor
{
    class Adapter
    {
        friend class XorLibrary;

        ComPtr<IDXGIAdapter3>                  m_adapter;
        std::shared_ptr<backend::ShaderLoader> m_shaderLoader;
        String                                 m_description;
        bool                                   m_debug = false;
    public:
        Device createDevice();
    };

    namespace backend
    {
        struct ProfilingEventData;
        class GPUTransientChunk;
    }

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
        friend class info::BufferInfo;
        friend class info::TextureInfo;
    public:
        Device() = default;

        explicit operator bool() const { return valid(); }

        SwapChain createSwapChain(Window &window);
        GraphicsPipeline createGraphicsPipeline(const GraphicsPipeline::Info &info);
        ComputePipeline createComputePipeline(const ComputePipeline::Info &info);

        Buffer    createBuffer(const Buffer::Info &info);
        BufferVBV createBufferVBV(Buffer buffer                 , const BufferVBV::Info &viewInfo = BufferVBV::Info());
        BufferVBV createBufferVBV(const Buffer::Info &bufferInfo, const BufferVBV::Info &viewInfo = BufferVBV::Info());
        BufferIBV createBufferIBV(Buffer buffer                 , const BufferIBV::Info &viewInfo = BufferIBV::Info());
        BufferIBV createBufferIBV(const Buffer::Info &bufferInfo, const BufferIBV::Info &viewInfo = BufferIBV::Info());
        BufferSRV createBufferSRV(Buffer buffer                 , const BufferSRV::Info &viewInfo = BufferSRV::Info());
        BufferSRV createBufferSRV(const Buffer::Info &bufferInfo, const BufferSRV::Info &viewInfo = BufferSRV::Info());
        BufferUAV createBufferUAV(Buffer buffer                 , const BufferUAV::Info &viewInfo = BufferUAV::Info());
        BufferUAV createBufferUAV(const Buffer::Info &bufferInfo, const BufferUAV::Info &viewInfo = BufferUAV::Info());

        Texture    createTexture(const Texture::Info &info);
        TextureSRV createTextureSRV(Texture texture                 , const TextureSRV::Info &viewInfo = TextureSRV::Info());
        TextureSRV createTextureSRV(const Texture::Info &textureInfo, const TextureSRV::Info &viewInfo = TextureSRV::Info());
        TextureRTV createTextureRTV(Texture texture                 , const TextureRTV::Info &viewInfo = TextureRTV::Info());
        TextureRTV createTextureRTV(const Texture::Info &textureInfo, const TextureRTV::Info &viewInfo = TextureRTV::Info());
        TextureDSV createTextureDSV(Texture texture                 , const TextureDSV::Info &viewInfo = TextureDSV::Info());
        TextureDSV createTextureDSV(const Texture::Info &textureInfo, const TextureDSV::Info &viewInfo = TextureDSV::Info());
        TextureUAV createTextureUAV(Texture texture                 , const TextureUAV::Info &viewInfo = TextureUAV::Info());
        TextureUAV createTextureUAV(const Texture::Info &textureInfo, const TextureUAV::Info &viewInfo = TextureUAV::Info());

        CommandList graphicsCommandList(const char *cmdListName = nullptr);

        void execute(CommandList &cmd);
        void present(SwapChain &swapChain, bool vsync = true);
        void resetFrameNumber(uint64_t newFrameNumber = 0);
        uint64_t frameNumber() const;

        struct ImguiInput
        {
            bool wantsMouse    = false;
            bool wantsKeyboard = false;
            bool wantsText     = false;
        };
        ImguiInput imguiInput(const Input &input);
        int2 debugMouseCursor() const;

        size_t debugFeedback(Span<uint8_t> dst);
        template <typename T>
        T debugFeedback()
        {
            T value = T();
            debugFeedback(asRWBytes(makeSpan(&value)));
            return value;
        }

        SeqNum now();
        void whenCompleted(std::function<void()> f);
        void whenCompleted(std::function<void()> f, SeqNum seqNum);
        bool hasCompleted(SeqNum seqNum);
        void waitUntilCompleted(SeqNum seqNum);
        void waitUntilDrained();

    private:
        ID3D12Device *device();

        backend::ShaderLoader &shaderLoader();
        void releaseDescriptor(backend::Descriptor descriptor);
        void releaseCommandList(std::shared_ptr<backend::CommandListState> cmdList);

        CommandList initializerCommandList();
        void initializeBufferWith(Buffer &buffer, Span<const uint8_t> bytes);
        void initializeTextureWith(Texture &texture, Span<const ImageData> subresources);

        backend::RootSignature collectRootSignature(const D3D12_SHADER_BYTECODE &shader);

        backend::HeapBlock uploadBytes(Span<const uint8_t> bytes,
                                       SeqNum cmdListNumber, backend::GPUTransientChunk &chunk,
                                       uint alignment);

        backend::ProfilingEventData *profilingEventData(const char *name,
                                                        uint64_t uniqueId,
                                                        backend::ProfilingEventData *parent = nullptr);
        void processProfilingEvents();

        void retireCommandLists();

        Device(Adapter adapter,
               ComPtr<ID3D12Device> device,
               std::shared_ptr<backend::ShaderLoader> shaderLoader);
        Device(StatePtr state);
    };

    class SwapChain : private backend::SharedState<backend::SwapChainState>
    {
        friend class Device;
        uint currentIndex();

    public:
        SwapChain() = default;

        TextureRTV backbuffer(bool sRGB = true);
    };

    // Global initialization and deinitialization of the Xor renderer.
    class XorLibrary
    {
    public:
        enum class DebugLayer
        {
            Default,
            Enabled,
            GPUBasedValidation,
            Disabled,
        };

    private:
        std::vector<Adapter>                   m_adapters;
        std::shared_ptr<backend::ShaderLoader> m_shaderLoader;

    public:

        XorLibrary(DebugLayer debugLayer = DebugLayer::Default);
        ~XorLibrary();

        Span<Adapter> adapters();
        Adapter &defaultAdapter();
        Device defaultDevice(bool createWarpDevice = false);
        Device warpDevice();

        void registerShaderTlog(StringView projectName,
                                StringView shaderTlogPath);
    };
}
