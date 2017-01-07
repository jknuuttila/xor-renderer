#include "Xor/XorResources.hpp"
#include "Xor/XorDevice.hpp"
#include "Xor/XorCommandList.hpp"

#define XOR_LOG_SHADER_COMPILES

namespace xor
{
    static const char ShaderFileExtension[] = ".cso";

    using namespace xor::backend;

    namespace info
    {
        BufferViewInfo BufferViewInfo::defaults(const BufferInfo & bufferInfo) const
        {
            BufferViewInfo info = *this;

            if (!info.format)
                info.format = bufferInfo.format;

            if (info.numElements == 0)
                info.numElements = static_cast<uint>(bufferInfo.size);

            return info;
        }

        uint BufferViewInfo::sizeBytes() const
        {
            return numElements * format.size();
        }

        BufferInfo BufferInfo::fromBytes(Span<const uint8_t> data, Format format)
        {
            XOR_ASSERT(data.size() % format.size() == 0,
                       "Initializer data size is not a multiple of the element type size.");

            auto numElements = data.size() / format.size();

            BufferInfo info(numElements, format);

            info.m_initializer = ResourceInitializer<Buffer>([data](Device &dev, Buffer &buf)
            {
                dev.initializeBufferWith(buf, data);
            });

            return info;
        }

        D3D12_INPUT_LAYOUT_DESC InputLayoutInfo::desc() const
        {
            D3D12_INPUT_LAYOUT_DESC d;
            d.NumElements        = static_cast<UINT>(m_elements.size());
            d.pInputElementDescs = m_elements.data();
            return d;
        }

        void InputLayoutInfo::hash(Hash & h) const
        {
            // FIXME: Hashes semantic names as pointers
            for (auto &&e : m_elements)
                h.pod(e);
        }

        TextureInfo::TextureInfo(const Image & image, Format fmt)
        {
            size = image.size();

            if (fmt)
                format = fmt;
            else
                format = image.format();

            mipLevels = image.mipLevels();

            m_initializer = ResourceInitializer<Texture>([&image](Device &dev, Texture &tex)
            {
                dev.initializeTextureWith(tex, image.allSubresources());
            });
        }

        TextureInfo::TextureInfo(const ImageData & data, Format fmt)
        {
            size = data.size;
            if (fmt)
                format = fmt;
            else
                format = data.format;

            m_initializer = ResourceInitializer<Texture>([data](Device &dev, Texture &tex)
            {
                dev.initializeTextureWith(tex, makeSpan(&data));
            });
        }

        TextureInfo::TextureInfo(ID3D12Resource * texture)
        {
            auto desc = texture->GetDesc();
            XOR_CHECK(desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                      "Expected a texture");
            size   = { static_cast<uint>(desc.Width), desc.Height };
            format = desc.Format;
        }

        size_t TextureInfo::sizeBytes() const
        {
            size_t total = 0;
            for (uint i = 0; i < 1; ++i)
            {
                uint2 mipSize = size;

                for (uint m = 0; m < mipLevels; ++m)
                {
                    total += format.areaSizeBytes(mipSize);
                    mipSize = max(1, mipSize / 2);
                }
            }

            return total;
        }

        TextureViewInfo TextureViewInfo::defaults(const TextureInfo & textureInfo,
                                                  bool srv) const
        {
            TextureViewInfo info = *this;

            if (!info.format)
                info.format = textureInfo.format;

            if (srv)
                info.format = info.format.readFormat();

            return info;
        }

        GraphicsPipelineInfo::GraphicsPipelineInfo()
        {
            zero(static_cast<D3D12_GRAPHICS_PIPELINE_STATE_DESC &>(*this));

            RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
            RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
            RasterizerState.FrontCounterClockwise = TRUE;
            RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            RasterizerState.DepthClipEnable       = TRUE;
            RasterizerState.DepthBias             = 0;
            RasterizerState.SlopeScaledDepthBias  = 0;
            RasterizerState.DepthBiasClamp        = 0;

            DepthStencilState.DepthEnable    = FALSE;
            DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

            PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

            // Depth disabled by default

            multisampling(1, 0);

            SampleMask = ~0u;
            for (auto &rt : BlendState.RenderTarget)
                rt.RenderTargetWriteMask = 0xf;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC GraphicsPipelineInfo::desc() const
        {
            return *this;
        }

        PipelineKey GraphicsPipelineInfo::key() const
        {
            Hash hash;

            hash.pod(desc());
            m_vs.hash(hash);
            m_ps.hash(hash);

            if (m_inputLayout)
                m_inputLayout->hash(hash);

            return hash.done();
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::vertexShader(const String & vsName, Span<const ShaderDefine> defines)
        {
            m_vs.shader = vsName;
            return vertexShader(SameShader {}, defines);
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::vertexShader(SameShader, Span<const ShaderDefine> defines)
        {
            m_vs.defines.clear();
            m_vs.defines.insert(m_vs.defines.begin(), defines.begin(), defines.end());
            return *this;
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::pixelShader(const String & psName, Span<const ShaderDefine> defines)
        {
            m_ps.shader = psName;
            return pixelShader(SameShader {}, defines);
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::pixelShader(SameShader, Span<const ShaderDefine> defines)
        {
            m_ps.defines.clear();
            m_ps.defines.insert(m_ps.defines.begin(), defines.begin(), defines.end());
            return *this;
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::renderTargetFormats(Format format)
        {
            NumRenderTargets = 1;
            RTVFormats[0] = format;
            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::renderTargetFormats(DXGI_FORMAT format)
        {
            return renderTargetFormats(Format(format));
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::renderTargetFormats(Span<const Format> formats)
        {
            NumRenderTargets = static_cast<uint>(formats.size());
            for (uint i = 0; i < NumRenderTargets; ++i)
                RTVFormats[i] = formats[i];
            return *this;
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::renderTargetFormats(Span<const DXGI_FORMAT> formats)
        {
            NumRenderTargets = static_cast<uint>(formats.size());
            for (uint i = 0; i < NumRenderTargets; ++i)
                RTVFormats[i] = formats[i];
            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::depthFormat(Format format)
        {
            DSVFormat = format;
            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::depthMode(info::DepthMode mode)
        {
            switch (mode)
            {
            case info::DepthMode::Disabled:
            default:
                DepthStencilState.DepthEnable    = FALSE;
                DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                break;
            case info::DepthMode::ReadOnly:
                DepthStencilState.DepthEnable    = TRUE;
                DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            case info::DepthMode::Write:
                DepthStencilState.DepthEnable    = TRUE;
                DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            }

            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::depthFunction(D3D12_COMPARISON_FUNC testFunction)
        {
            DepthStencilState.DepthFunc = testFunction;
            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::depthBias(int bias, float slopeScaled, float clamp)
        {
            RasterizerState.DepthBias            = bias;
            RasterizerState.SlopeScaledDepthBias = slopeScaled;
            RasterizerState.DepthBiasClamp       = clamp;
            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::inputLayout(const info::InputLayoutInfo & ilInfo)
        {
            // Put the input layout info object behind a pointer so the element addresses
            // do not change even if the pipeline info object is copied.
            m_inputLayout = std::make_shared<info::InputLayoutInfo>(ilInfo);
            InputLayout   = m_inputLayout->desc();
            return *this;
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::multisampling(uint samples, uint quality)
        {
            SampleDesc.Count   = samples;
            SampleDesc.Quality = quality;
            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
        {
            PrimitiveTopologyType = type;
            return *this;
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::fill(D3D12_FILL_MODE fillMode)
        {
            RasterizerState.FillMode = fillMode;
            return *this;
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::cull(D3D12_CULL_MODE cullMode)
        {
            RasterizerState.CullMode = cullMode;
            return *this;
        }

        GraphicsPipelineInfo &GraphicsPipelineInfo::winding(bool counterClockWise)
        {
            RasterizerState.FrontCounterClockwise = static_cast<BOOL>(counterClockWise);
            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::blend(uint renderTarget, bool enabled, D3D12_BLEND src, D3D12_BLEND dst, D3D12_BLEND_OP op)
        {
            XOR_ASSERT(renderTarget < 8, "Invalid render target index");
            auto &rt = BlendState.RenderTarget[renderTarget];
            rt.BlendEnable    = static_cast<BOOL>(enabled);
            rt.LogicOpEnable  = FALSE;
            rt.BlendOp        = op;
            rt.SrcBlend       = src;
            rt.DestBlend      = dst;
            rt.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha  = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            return *this;
        }

        GraphicsPipelineInfo & GraphicsPipelineInfo::antialiasedLine(bool lineAA)
        {
            RasterizerState.AntialiasedLineEnable = static_cast<BOOL>(lineAA);
            return *this;
        }

        void ShaderDesc::hash(Hash & h) const
        {
            h.bytes(asBytes(shader));
            for (auto &&d : defines)
            {
                h.bytes(asBytes(d.define));
                h.bytes(asBytes(d.value));
            }
        }

        String ShaderDesc::path() const
        {
            if (shader.empty())
                return String();
            else if (defines.empty())
                return basePath();

            Hash h;
            hash(h);

            return String::format("%s.%llx%s",
                                  shader.cStr(),
                                  static_cast<llu>(h.done()),
                                  ShaderFileExtension);
        }

        String ShaderDesc::basePath() const
        {
            if (shader.empty())
                return String();
            else
                return shader + ShaderFileExtension;
        }

        ComputePipelineInfo::ComputePipelineInfo()
        {} 

        ComputePipelineInfo::ComputePipelineInfo(const String & csName, Span<const ShaderDefine> defines)
        {
            computeShader(csName, defines);
        }

        ComputePipelineInfo & ComputePipelineInfo::computeShader(const String & csName, Span<const ShaderDefine> defines)
        {
            m_cs.shader = csName;
            return computeShader(SameShader{}, defines);
        }

        ComputePipelineInfo & ComputePipelineInfo::computeShader(SameShader, Span<const ShaderDefine> defines)
        {
            m_cs.defines.clear();
            m_cs.defines.insert(m_cs.defines.begin(), defines.begin(), defines.end());
            return *this;
        }

        PipelineKey ComputePipelineInfo::key() const
        {
            Hash h;
            m_cs.hash(h);
            return h.done();
        }
    }

    namespace backend
    {
        static bool compileShader(const BuildInfo &shaderBuildInfo,
                                  const String &outputFile,
                                  Span<const info::ShaderDefine> defines)
        {
            log("Pipeline", "Compiling shader %s\n", outputFile.cStr());

            String output;
            String errors;

            // Find the part with the original output filename, and replace it
            // with the actual one.
            int outputLocation = shaderBuildInfo.buildArgs.lower().find(shaderBuildInfo.target.lower());
            XOR_CHECK(outputLocation >= 0, "Could not detect output filename position in compilation arguments");

            auto buildArgs = shaderBuildInfo.buildArgs.replace(outputLocation, outputLocation + shaderBuildInfo.target.length(),
                                                               outputFile);
            std::vector<String> extraDefines;
            extraDefines.reserve(defines.size());
            for (auto &d : defines)
            {
                if (d.value)
                    extraDefines.emplace_back(String::format(" /D%s=\"%s\"", d.define.cStr(), d.value.cStr()));
                else
                    extraDefines.emplace_back(String::format(" /D%s", d.define.cStr()));
            }

            buildArgs = buildArgs + String::join(extraDefines, "");

#if defined(XOR_LOG_SHADER_COMPILES)
			log("Pipeline", "%s %s\n", shaderBuildInfo.buildExe.cStr(), buildArgs.cStr());
#endif

            int returnCode = shellCommand(
                shaderBuildInfo.buildExe,
                buildArgs,
                &output,
                &errors);

            if (output) print("%s", output.cStr());
            if (errors) print("%s", errors.cStr());

            return returnCode == 0;
        }

        struct ShaderBinary : public D3D12_SHADER_BYTECODE
        {
            DynamicBuffer<uint8_t> bytecode;

            ShaderBinary()
            {
                pShaderBytecode = nullptr;
                BytecodeLength  = 0;
            }

            ShaderBinary(const String &filename)
            {
                bytecode = File(filename).read();
                pShaderBytecode = bytecode.data();
                BytecodeLength  = bytecode.size();
            }
        };


        void ShaderLoader::scanChangedSources()
        {
            if (shaderScanQueue.empty())
                return;

            shaderScanIndex = (shaderScanIndex + 1) % shaderScanQueue.size();
            auto &shader = shaderScanQueue[shaderScanIndex];
            auto it = shaderData.find(shader);
            if (it != shaderData.end())
            {
                auto &data = *it->second;
                if (data.isOutOfDate())
                {
                    log("ShaderLoader",
                        "%s is out of date.\n",
                        data.buildInfo->target.cStr());

                    data.rebuildPipelines();
                }
            }
        }

        void ShaderLoader::registerBuildInfo(std::shared_ptr<const BuildInfo> buildInfo)
        {
            auto &shaderPath = buildInfo->target;
            auto &data = shaderData[shaderPath];

            if (!data)
            {
                data = std::make_shared<ShaderLoader::ShaderData>();
                shaderScanQueue.emplace_back(shaderPath);
                data->buildInfo = buildInfo;
                data->timestamp = data->buildInfo->targetTimestamp();
                log("ShaderLoader", "Registering shader %s for tracking.\n", shaderPath.cStr());
            }
        }

        void ShaderLoader::registerShaderTlog(StringView projectName, StringView shaderTlogPath)
        {
            for (auto &buildInfo : scanBuildInfos(shaderTlogPath, ShaderFileExtension))
                registerBuildInfo(buildInfo);
        }

        void ShaderLoader::ShaderData::rebuildPipelines()
        {
            std::vector<std::shared_ptr<PipelineState>> pipelinesToRebuild;
            pipelinesToRebuild.reserve(users.size());
            for (auto &kv : users)
            {
                if (auto p = kv.second.lock())
                    pipelinesToRebuild.emplace_back(std::move(p));
            }

            users.clear();
            for (auto &p : pipelinesToRebuild)
            {
                p->reload();
                users[p.get()] = p;
            }
        }

        ResourceState::~ResourceState()
        {
            // Actually release the resource once every command list that could possibly have
            // referenced it has retired.

            // Queue up a no-op lambda, that holds the resource ComPtr by value.
            // When the Device has executed it, it will get destroyed, freeing the last reference.
            if (auto dev = device())
                dev.whenCompleted([resource = std::move(resource)]{});
        }

        DescriptorViewState::~DescriptorViewState()
        {
            if (auto dev = device())
            {
                dev.whenCompleted([dev, descriptor = descriptor]() mutable
                {
                    dev.releaseDescriptor(descriptor);
                });
            }
        }

        ShaderBinary PipelineState::loadShader(ShaderLoader & loader, const ShaderDesc &shader)
        {
            if (!shader)
                return ShaderBinary();

            String shaderPath = File::canonicalize(shader.path(), true);
            String basePath   = File::canonicalize(shader.basePath(), true);

            auto data = loader.shaderData[basePath];
            XOR_CHECK(!!data, "Could not find shader data for shader %s", shaderPath.cStr());

            uint64_t timestamp       = File::lastWritten(shaderPath);
            uint64_t sourceTimestamp = data->buildInfo->sourceTimestamp();
            data->timestamp          = std::max(data->timestamp, timestamp);

            if (timestamp < sourceTimestamp)
            {
                compileShader(*data->buildInfo, shaderPath, shader.defines);
                data->timestamp = sourceTimestamp;
            }
            else
            {
                log("Pipeline", "Shader has not been modified since last compile.\n");
            }

            data->users[this] = shared_from_this();

            log("Pipeline", "Loading shader %s\n", shaderPath.cStr());
            return ShaderBinary(shaderPath);
        }

        void PipelineState::reload()
        {
            auto dev = device();

            XOR_CHECK(static_cast<int>(!!graphicsInfo) + static_cast<int>(!!computeInfo) == 1,
                      "Pipeline must be either a GraphicsPipeline or a ComputePipeline");

            if (graphicsInfo)
            {
                log("Pipeline", "Rebuilding Graphics PSO.\n");

                D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = graphicsInfo->desc();

                ShaderBinary vs = loadShader(dev.shaderLoader(), graphicsInfo->m_vs);
                ShaderBinary ps = loadShader(dev.shaderLoader(), graphicsInfo->m_ps);

                if (graphicsInfo->m_vs)
                {
                    rootSignature = dev.collectRootSignature(vs);
                    desc.VS = vs;
                }
                else
                {
                    zero(desc.VS);
                }

                if (graphicsInfo->m_ps)
                {
                    rootSignature = dev.collectRootSignature(vs);
                    desc.PS = ps;
                }
                else
                {
                    zero(desc.PS);
                }

                releasePSO();

                XOR_CHECK_HR(dev.device()->CreateGraphicsPipelineState(
                    &desc,
                    __uuidof(ID3D12PipelineState),
                    &pso));
            }
            else if (computeInfo)
            {
                log("Pipeline", "Rebuilding Compute PSO.\n");

                D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
                zero(desc);

                ShaderBinary cs = loadShader(dev.shaderLoader(), computeInfo->m_cs);

                if (computeInfo->m_cs)
                {
                    rootSignature = dev.collectRootSignature(cs);
                    desc.CS = cs;
                }
                else
                {
                    zero(desc.CS);
                }

                releasePSO();

                XOR_CHECK_HR(dev.device()->CreateComputePipelineState(
                    &desc,
                    __uuidof(ID3D12PipelineState),
                    &pso));
            }
        }

        void PipelineState::releasePSO()
        {
            if (!pso)
                return;

            if (auto dev = device())
                dev.whenCompleted([pso = std::move(pso)]{});
        }

        PipelineState::~PipelineState()
        {
            releasePSO();
        }
    }

    ID3D12Resource *Resource::get() const
    {
        return m_state ? S().resource.Get() : nullptr;
    }

    GraphicsPipeline::Info GraphicsPipeline::variant() const
    {
        return *S().graphicsInfo;
    }

    ComputePipeline::Info ComputePipeline::variant() const
    {
        return *S().computeInfo;
    }
}
