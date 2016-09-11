#pragma once

#include "Core/Utils.hpp"
#include "Core/Allocators.hpp"
#include "Core/String.hpp"
#include "Core/File.hpp"
#include "Core/Serialization.hpp"

namespace xor
{
    namespace serialization
    {
        struct FileBlock;
    }

    class ChunkFile
    {
        friend class Chunk;
        friend class KeyValue;
    public:
        class Chunk
        {
            friend class ChunkFile;
            friend class KeyValue;

            ChunkFile *            m_file = nullptr;
            Block                  m_block;
            DynamicBuffer<uint8_t> m_writeData;

            Chunk(ChunkFile &file);
            Chunk(ChunkFile &file, serialization::FileBlock block);
        public:
            Chunk() = default;

            auto writer(size_t sizeEstimate = 0) { return makeWriter(m_writeData, sizeEstimate); }
            Reader reader() const;

            void write();
        };

        class KeyValue
        {
            friend class ChunkFile;
            ChunkFile *                                        m_file = nullptr;
            Block                                              m_block;
            std::unordered_map<String, std::unique_ptr<Chunk>> m_kv;
            KeyValue(ChunkFile &file);
            KeyValue(ChunkFile &file, serialization::FileBlock block);

            void read();
        public:
            KeyValue() = default;

            Chunk *chunk(StringView name);
            const Chunk *chunk(StringView name) const;
            Chunk &setChunk(StringView name);

            void write();
        };

    private:
        String                    m_path;
        VirtualBuffer<uint8_t>    m_contents;
        OffsetHeap                m_allocator;
        std::unique_ptr<KeyValue> m_toc;

        Span<uint8_t> span(Block block);
        Span<const uint8_t> span(Block block) const;
        void obtainBlock(Block &block, size_t bytes);
        void writeToContents(Block &block, Span<const uint8_t> bytes);
    public:
        ChunkFile() = default;
        ChunkFile(String path);

        const String &path() const { return m_path; }

        KeyValue &toc();
        const KeyValue &toc() const;

        void read();
        void write();
    };
}
