#include "Xor/Material.hpp"

namespace xor
{
    Material::Material(String name)
    {
        m_state = std::make_shared<State>();
        m_state->name = std::move(name);
    }

    void Material::load(Device &device, StringView basePath)
    {
        if (valid())
        {
            Timer time;
            size_t loadedBytes = 0;

            loadedBytes += m_state->diffuse.load(device, basePath);

            log("Material", "Loaded material \"%s\" in %.2f ms (%.2f MB / s)\n",
                m_state->name.cStr(),
                time.milliseconds(),
                time.bandwidthMB(loadedBytes));
        }
    }

    size_t MaterialLayer::load(Device &device, StringView basePath)
    {
        if (!filename)
            return 0;

        Timer time;
        auto path = basePath ? (basePath + "/" + filename) : filename;

        if (File::exists(path))
        {
            texture = device.createTextureSRV(Texture::Info(Image(path)));

            auto bytes = texture.texture()->sizeBytes();

            log("Material", "Loaded texture \"%s\" in %.2f ms (%.2f MB / s)\n",
                filename.cStr(),
                time.milliseconds(),
                time.bandwidthMB(bytes));

            return bytes;
        }
        else
        {
            log("Material", "Could not find texture \"%s\", skipping\n",
                filename.cStr());

            return 0;
        }
    }
}
