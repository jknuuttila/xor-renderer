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
        FileBlock toc;
    };

    static const FourCC ChunkFileFourCC { "XORC" };
    static const size_t ChunkFileMaxSize = 1024ULL * 1024ULL * 1024ULL;

    ChunkFile::Chunk *ChunkFile::KeyValue::chunk(StringView name)
    {
        auto it = m_kv.find(name);
        if (it == m_kv.end())
            return nullptr;
        else
            return it->second.get();
    }

    const ChunkFile::Chunk * ChunkFile::KeyValue::chunk(StringView name) const
    {
        auto it = m_kv.find(name);
        if (it == m_kv.end())
            return nullptr;
        else
            return it->second.get();
    }

    ChunkFile::Chunk & ChunkFile::KeyValue::setChunk(StringView name)
    {
        m_kv[name].reset(new Chunk(*m_file));
        return *m_kv[name];
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
        }
    }

    void ChunkFile::writeToContents(Block & block, Span<const uint8_t> bytes)
    {
        obtainBlock(block, bytes.sizeBytes());

        if (static_cast<size_t>(block.end) > m_contents.size())
            m_contents.resize(static_cast<size_t>(block.end));

        auto dst = span(block);
        memcpy(dst.data(), bytes.data(), bytes.sizeBytes());
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

    ChunkFile::KeyValue & ChunkFile::toc()
    {
        if (!m_toc)
            m_toc.reset(new KeyValue(*this));

        return *m_toc;
    }

    const ChunkFile::KeyValue & ChunkFile::toc() const
    {
        return *m_toc;
    }

    void ChunkFile::read()
    {
        File f(m_path);
        XOR_THROW(!!f, SerializationException, "Failed to open file");
        m_contents.resize(f.size());
        XOR_THROW_HR(f.read(m_contents), SerializationException);

        auto header = Reader(m_contents).readStruct<ChunkFileHeader>();
        XOR_THROW(header.fourCC.asUint() == ChunkFileFourCC.asUint(), SerializationException, "Wrong 4CC");

        m_toc.reset(new KeyValue(*this, header.toc));
        m_toc->read();
    }

    void ChunkFile::write() 
    {
        toc().write();

        ChunkFileHeader header;
        header.fourCC = ChunkFileFourCC;
        header.toc    = toc().m_block;

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

    Reader ChunkFile::Chunk::reader() const { return Reader(m_file->span(m_block)); }

    void ChunkFile::Chunk::write()
    {
        m_file->writeToContents(m_block, m_writeData);
    }

    ChunkFile::KeyValue::KeyValue(ChunkFile & file)
        : m_file(&file)
    {}

    ChunkFile::KeyValue::KeyValue(ChunkFile & file, serialization::FileBlock block)
        : m_file(&file)
        , m_block(block)
    {}

    void ChunkFile::KeyValue::read()
    {
        auto kvReader = Reader(m_file->span(m_block));
        uint length   = kvReader.readLength();

        for (uint i = 0; i < length; ++i)
        {
            auto key  = kvReader.readString();
            auto val  = kvReader.read<FileBlock>();
            m_kv[key].reset(new Chunk(*m_file, val));
        }
    }

    void ChunkFile::KeyValue::write()
    {
        DynamicBuffer<uint8_t> buffer;
        auto writer = makeWriter(buffer, 1024);
        writer.writeLength(static_cast<uint>(m_kv.size()));

        for (auto &&kv : m_kv)
        {
            kv.second->write();

            writer.writeString(kv.first);
            writer.write(FileBlock(kv.second->m_block));
        }

        m_file->writeToContents(m_block, buffer);
    }

}
