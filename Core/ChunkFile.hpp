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
    public:
        class Chunk
        {
            friend class ChunkFile;

            ChunkFile *                                        m_file = nullptr;
            Block                                              m_block;
            std::unordered_map<String, std::unique_ptr<Chunk>> m_chunks;
            Block                                              m_dataBlock;
            DynamicBuffer<uint8_t>                             m_data;

            Chunk(ChunkFile &file);
            Chunk(ChunkFile &file, serialization::FileBlock block);
        public:
            Chunk() = default;

            Chunk *maybeChunk(StringView name);
            const Chunk *maybeChunk(StringView name) const;
            Chunk &chunk(StringView name);
            const Chunk &chunk(StringView name) const;
            Chunk &setChunk(StringView name);

            std::vector<std::pair<String, Chunk *>> allChunks();
            std::vector<std::pair<String, const Chunk *>> allChunks() const;

            auto writer(size_t sizeEstimate = 0) { return makeWriter(m_data, sizeEstimate); }
            Reader reader() const;

            void read();
            void write();
        };

    private:
        String                    m_path;
        VirtualBuffer<uint8_t>    m_contents;
        OffsetHeap                m_allocator;
        std::unique_ptr<Chunk>    m_mainChunk;

        Span<uint8_t> span(Block block);
        Span<const uint8_t> span(Block block) const;
        void obtainBlock(Block &block, size_t bytes);
        void writeToContents(Block &block, Span<const uint8_t> bytes);
    public:
        ChunkFile() = default;
        ChunkFile(String path);

        const String &path() const { return m_path; }

        Chunk &mainChunk();
        const Chunk &mainChunk() const;

        void read();
        void write();
    };
}
