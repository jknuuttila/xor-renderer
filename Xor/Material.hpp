#pragma once

#include "Xor/Xor.hpp"
#include "Core/ChunkFile.hpp"

namespace xor
{
    namespace info
    {
        class MaterialInfo
        {
        public:
            StringView basePath;
            bool import = false;

            MaterialInfo() = default;
            MaterialInfo(StringView basePath) : basePath(basePath) {}
        };

        class MaterialInfoBuilder : public MaterialInfo
        {
        public:
            MaterialInfoBuilder &basePath(StringView basePath) { MaterialInfo::basePath = basePath; return *this; }
            MaterialInfoBuilder &import(bool import = true) { MaterialInfo::import = import; return *this; }
        };
    }

    XOR_EXCEPTION_TYPE(MaterialException)

    class MaterialLayer
    {
        friend class Material;
    public:
        String filename;
        TextureSRV texture;

    private:
        String path(const info::MaterialInfo &info);
        size_t import(ChunkFile::KeyValue &kv,
                      const info::MaterialInfo &info);
        size_t load(Device &device,
                    const info::MaterialInfo &info,
                    const ChunkFile::KeyValue *kv = nullptr);
    };

    class Material
    {
        struct State
        {
            String name;
            MaterialLayer albedo;
        };
        std::shared_ptr<State> m_state;

        void import(ChunkFile &materialFile, const info::MaterialInfo &info);
    public:
        using Info    = info::MaterialInfo;
        using Builder = info::MaterialInfoBuilder;

        Material() = default;
        Material(String name);

        bool valid() const { return !!m_state; }
        explicit operator bool() const { return valid(); }

        void load(Device &device, const Info &info = Info());

        const String &name() const { return m_state->name; }
        MaterialLayer &albedo() { return m_state->albedo; }
    };
}

