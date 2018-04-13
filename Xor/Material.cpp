#include "Xor/Material.hpp"

#include "Core/Compression.hpp"

namespace Xor
{
    Material::Material(String name)
    {
        m_state = std::make_shared<State>();
        m_state->name = std::move(name);
    }

    struct MaterialFileHeader
    {
        static const uint VersionNumber = 1;
    };

    void Material::import(ChunkFile &materialFile, const info::MaterialInfo &info)
    {
        log("Material", "Importing material \"%s\" into \"%s\"\n",
            name().cStr(),
            materialFile.path().cStr());

        Timer time;

        size_t bytes = 0;
        materialFile.mainChunk().writer().writeStruct(MaterialFileHeader {});
        bytes += albedo().import(materialFile.mainChunk(), info);
        materialFile.write();

        log("Material", "Imported material \"%s\" in %.2f ms (%.2f MB / s)\n",
            m_state->name.cStr(),
            time.milliseconds(),
            time.bandwidthMB(bytes));
    }

    void Material::load(Device &device, const Info &info)
    {
        if (valid())
        {
            Timer time;
            size_t loadedBytes = 0;

            bool loadedImported = false;
            if (info.import)
            {
                String materialPath = "materials/" + name() + ".xmat";
                ChunkFile materialFile(materialPath);

                for (;;)
                {

                    try
                    {
                        loadedBytes = 0;
                        materialFile.read();
                        materialFile.mainChunk().reader().readStruct<MaterialFileHeader>();
                        loadedBytes += albedo().load(device, info, &materialFile.mainChunk());
                        loadedImported = true;
                        break;
                    }
                    catch (const Exception &) {}

                    // If we got here, reading the material failed. Attempt to import it.
                    try
                    {
                        import(materialFile, info);
                        // If the import succeeds, try loading it again.
                        loadedBytes = 0;
                        materialFile.read();
                        materialFile.mainChunk().reader().readStruct<MaterialFileHeader>();
                        loadedBytes += albedo().load(device, info, &materialFile.mainChunk());
                        loadedImported = true;
                    }
                    catch (const Exception &)
                    {
                        // If the import fails, bail out.
                    }
                    break;
                }
            }

            // If the material hasn't been loaded from an imported material
            // file, load it the old-fashioned way.
            if (!loadedImported)
            {
                loadedBytes += m_state->albedo.load(device, info);
            }

            log("Material", "Loaded material \"%s\" in %.2f ms (%.2f MB / s)\n",
                m_state->name.cStr(),
                time.milliseconds(),
                time.bandwidthMB(loadedBytes));
        }
    }

    struct MaterialLayerHeader
    {
        static const uint VersionNumber = 1;

        uint64_t importedTime     = 0;
        uint64_t decompressedSize = 0;
    };

    size_t MaterialLayer::load(Device &device,
                               const info::MaterialInfo &info,
                               const ChunkFile::Chunk *chunk)
    {
        if (!filename)
            return 0;

        Timer time;
        auto path = this->path(info);

        if (File::exists(path))
        {
            if (info.import && chunk)
            {
                Reader reader     = chunk->chunk(path).reader();
                auto fileTime     = File::lastWritten(path);
                auto header       = reader.readStruct<MaterialLayerHeader>();
                XOR_THROW(fileTime <= header.importedTime, MaterialException, "Imported texture out of date");

                auto compressed   = reader.readBlob();
                auto decompressed = decompressZstd(header.decompressedSize, compressed);

                texture = device.createTextureSRV(
                    Texture::Info(
                        Image(Image::Builder().blob(decompressed))));
            }
            else
            {
                texture = device.createTextureSRV(
                    Texture::Info(
                        Image(Image::Builder()
                              .filename(path)
                              .generateMipmaps())));

            }

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

    String MaterialLayer::path(const info::MaterialInfo & info)
    {
        return info.basePath ? (info.basePath + "/" + filename) : filename;
    }

    size_t MaterialLayer::import(ChunkFile::Chunk & chunk,
                                 const info::MaterialInfo &info)
    {
        if (!filename)
            return 0;

        Timer time;
        auto path = this->path(info);

        if (File::exists(path))
        {
            Timer blockCompressionTime;
            auto blockCompressed = Image(Image::Builder()
                                         .filename(path)
                                         .generateMipmaps()
                                         .compress())
                .serialize();
            log("Material", "    Block compression: %.2f ms\n", blockCompressionTime.milliseconds());

            auto compressed = compressZstd(blockCompressed);

            MaterialLayerHeader header;
            header.importedTime     = File::lastWritten(path);
            header.decompressedSize = blockCompressed.sizeBytes();

            auto writer = chunk.setChunk(path).writer(
                sizeof(header) + 16 + compressed.sizeBytes());
            writer.writeStruct(header);
            writer.writeBlob(compressed);

            log("Material", "Imported texture \"%s\" in %.2f ms (%.2f MB / s)\n",
                filename.cStr(),
                time.milliseconds(),
                time.bandwidthMB(blockCompressed.sizeBytes()));

            return blockCompressed.sizeBytes();
        }
        else
        {
            log("Material", "Could not find texture \"%s\", skipping\n",
                filename.cStr());

            return 0;
        }
    }
}
