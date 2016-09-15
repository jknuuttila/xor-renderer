#include "Core/ChunkFile.hpp"
#include "Core/Utils.hpp"

namespace xor
{
    namespace serialization
    {
        struct FileBlock
        {
            int32_t begin = -1;
            int32_t end   = -1;

            FileBlock() = default;

            FileBlock(int32_t begin, int32_t end)
                : begin(begin)
                , end(end)
            {}

            FileBlock(Block b)
                : begin(static_cast<int32_t>(b.begin))
                , end(static_cast<int32_t>(b.end))
            {}

            Block block() const
            {
                return Block(begin, end);
            }

            explicit operator bool() const { return begin >= 0; }
            operator Block() const { return block(); }
        };
    }

    using namespace xor::serialization;

    struct ChunkFileHeader
    {
        static const uint VersionNumber = 1;
        FourCC fourCC;
        FileBlock mainChunk;
    };

    static const FourCC ChunkFileFourCC { "XORC" };
    static const size_t ChunkFileMaxSize = 1024ULL * 1024ULL * 1024ULL;

    ChunkFile::Chunk *ChunkFile::Chunk::maybeChunk(StringView name)
    {
        auto it = m_chunks.find(name);
        if (it == m_chunks.end())
            return nullptr;
        else
            return it->second.get();
    }

    const ChunkFile::Chunk * ChunkFile::Chunk::maybeChunk(StringView name) const
    {
        auto it = m_chunks.find(name);
        if (it == m_chunks.end())
            return nullptr;
        else
            return it->second.get();
    }

    ChunkFile::Chunk & ChunkFile::Chunk::chunk(StringView name)
    {
        auto c = maybeChunk(name);
        XOR_THROW(c, SerializationException, "Chunk \"%s\" missing", name.str().cStr());
        return *c;
    }

    const ChunkFile::Chunk & ChunkFile::Chunk::chunk(StringView name) const
    {
        auto c = maybeChunk(name);
        XOR_THROW(c, SerializationException, "Chunk \"%s\" missing", name.str().cStr());
        return *c;
    }

    ChunkFile::Chunk & ChunkFile::Chunk::setChunk(StringView name)
    {
        m_chunks[name].reset(new Chunk(*m_file));
        return *m_chunks[name];
    }

    std::vector<std::pair<String, ChunkFile::Chunk *>> ChunkFile::Chunk::allChunks()
    {
        std::vector<std::pair<String, Chunk *>> chunks;
        for (auto &c : m_chunks)
            chunks.emplace_back(c.first, c.second.get());
        return chunks;
    }

    std::vector<std::pair<String, const ChunkFile::Chunk *>> ChunkFile::Chunk::allChunks() const
    {
        std::vector<std::pair<String, const Chunk *>> chunks;
        for (auto &c : m_chunks)
            chunks.emplace_back(c.first, c.second.get());
        return chunks;
    }

    Span<uint8_t> ChunkFile::span(Block block)
    {
        return makeSpan(m_contents.data() + block.begin, block.size());
    }

    Span<const uint8_t> ChunkFile::span(Block block) const
    {
        return makeSpan(m_contents.data() + block.begin, block.size());
    }

    void ChunkFile::obtainBlock(Block & block, size_t bytes)
    {
        if (!block || block.size() != bytes)
        {
            if (block)
                m_allocator.release(block);

            block = m_allocator.allocate(bytes);

            if (static_cast<size_t>(block.end) > m_contents.size())
                m_contents.resize(static_cast<size_t>(block.end));
        }
    }

    ChunkFile::ChunkFile(String path)
        : m_path(std::move(path))
        , m_contents(ChunkFileMaxSize)
    {
        XOR_CHECK(m_allocator.resize(ChunkFileMaxSize),
                  "Could not resize allocator to desired size");
        XOR_CHECK(m_allocator.markAsAllocated(
            Block(0, static_cast<int64_t>(serializedStructSize<ChunkFileHeader>()))),
            "Failed to mark header as allocated");
    }

    ChunkFile::Chunk & ChunkFile::mainChunk()
    {
        if (!m_mainChunk)
            m_mainChunk.reset(new Chunk(*this));

        return *m_mainChunk;
    }

    const ChunkFile::Chunk & ChunkFile::mainChunk() const
    {
        XOR_THROW(m_mainChunk, SerializationException, "Main chunk missing");
        return *m_mainChunk;
    }

    void ChunkFile::read()
    {
        File f(m_path);
        XOR_THROW(!!f, SerializationException, "Failed to open file");
        m_contents.resize(f.size());
        XOR_THROW_HR(f.read(m_contents), SerializationException);

        auto header = Reader(m_contents).readStruct<ChunkFileHeader>();
        XOR_THROW(header.fourCC.asUint() == ChunkFileFourCC.asUint(), SerializationException, "Wrong 4CC");

        m_mainChunk.reset(new Chunk(*this, header.mainChunk));
        m_mainChunk->read();
    }

    void ChunkFile::write() 
    {
        mainChunk().write();

        ChunkFileHeader header;
        header.fourCC    = ChunkFileFourCC;
        header.mainChunk = mainChunk().m_block;

        makeWriter(m_contents).writeStruct(header);

        File::ensureDirectoryExists(m_path);
        File f(m_path, File::Mode::ReadWrite, File::Create::CreateAlways);
        XOR_THROW(!!f, SerializationException, "Failed to open file");
        f.write(m_contents);
    }

    ChunkFile::Chunk::Chunk(ChunkFile & file)
        : m_file(&file)
    {}

    ChunkFile::Chunk::Chunk(ChunkFile & file, serialization::FileBlock block)
        : m_file(&file)
        , m_block(block)
    {}

    Reader ChunkFile::Chunk::reader() const { return Reader(m_file->span(m_dataBlock)); }

    void ChunkFile::Chunk::write()
    {
        DynamicBuffer<uint8_t> buffer;
        auto writer = makeWriter(buffer, 1024);
        writer.writeLength(static_cast<uint>(m_chunks.size()));
        writer.writeLength(static_cast<uint>(m_data.sizeBytes()));

        for (auto &&kv : m_chunks)
        {
            kv.second->write();

            writer.writeString(kv.first);
            writer.write(FileBlock(kv.second->m_block));
        }

        auto totalBytes = buffer.sizeBytes() + m_data.sizeBytes();
        m_file->obtainBlock(m_block, totalBytes);
        m_dataBlock.begin = m_block.end - static_cast<int64_t>(m_data.sizeBytes());
        m_dataBlock.end   = m_block.end;

        auto dst = m_file->span(m_block);
        memcpy(dst.data(), buffer.data(), buffer.sizeBytes());
        memcpy(dst.data() + buffer.sizeBytes(), m_data.data(), m_data.sizeBytes());
    }

    void ChunkFile::Chunk::read()
    {
        auto reader  = Reader(m_file->span(m_block));
        uint numChunks = reader.readLength();
        uint dataBytes = reader.readLength();

        m_dataBlock.begin = m_block.end - dataBytes;
        m_dataBlock.end   = m_block.end;

        for (uint i = 0; i < numChunks; ++i)
        {
            auto key  = reader.readString();
            auto val  = reader.read<FileBlock>();
            m_chunks[key].reset(new Chunk(*m_file, val));
        }
    }
}
