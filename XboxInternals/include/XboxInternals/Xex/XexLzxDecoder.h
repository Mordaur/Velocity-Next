#ifndef XEXLZXDECODER_H
#define XEXLZXDECODER_H

#include <XboxInternals/Types.h>
#include <XboxInternals/Export.h>

#include <array>
#include <cstddef>
#include <vector>

/* LZX decoder for XEX compression type 2 ("XCompress"/"basic" compression,
 * XexpLoadCompressedImage 0x800A5938).
 *
 * The kernel creates one engine per image and calls XCompressDecompress() once
 * per container chunk: the bit reader is re-primed at the start of every chunk
 * while all decode state (window/history, Huffman trees, repeated offsets,
 * remaining block length, E8 state) persists. DecodeChunk() has exactly that
 * contract - it consumes one chunk and must produce exactly outLength bytes.
 *
 * Parameters (verified against xboxkrnl 16547.0 and the corpus files
 * tests/corpus/raw/xex/{default_xell.xex,xbox.XEX}):
 *   window 32 KiB (window_bits 15), 30 position slots, 496 main-tree symbols,
 *   249 length-tree symbols, 8 aligned-offset symbols, bit stream = 16-bit
 *   little-endian words read MSB-first, code lengths up to 16 bits.
 *
 * Kernel references: sub_80158FD0 (block loop), sub_80159260/sub_801592C8
 * (bit reader), sub_80159E78 (symbol loop), sub_8015A2D8 (code lengths),
 * sub_80159470 (stored block header), sub_80158EA0 (E8 translation).
 */
class XBOXINTERNALS_EXPORT XexLzxDecoder
{
public:
    static constexpr u32 DefaultWindowSize = 0x8000;
    static constexpr u32 MaxWindowSize = 0x200000; // LZX limit: 21 window bits
    static constexpr u32 FrameSize = 0x8000;
    static constexpr u32 MaxNumPositionSlots = 50;
    static constexpr int MaxMainSymbols = 256 + 8 * static_cast<int>(MaxNumPositionSlots);

    XexLzxDecoder();

    // Set the LZX window size from the XEX base-file descriptor's blockSize.
    // The frame size stays 0x8000; only the history window and the position
    // slot count change (16 -> 32 slots, 20 -> 42 slots, ...). Call before the
    // first DecodeChunk(); a different size after that is a programming error.
    void SetWindowSize(u32 bytes);
    u32 GetWindowSize() const;

    // Forget all decode state (start of a new image).
    void Reset();

    // Decode exactly outLength bytes of the next chunk into outBuffer.
    // Throws std::string on malformed input.
    void DecodeChunk(const u8 *data, size_t dataLength, u8 *outBuffer, size_t outLength);

    // Stored (uncompressed) LZX blocks carry a 12-byte big-endian R0/R1/R2
    // header; disable only if a stream turns out not to have one.
    void SetRestoreRepeatOffsets(bool restore);

    // LZX "Intel E8" call translation, applied to the chunk output (no-op
    // unless the stream announces a file size).
    void SetE8Translation(bool enable);
    bool IsE8TranslationEnabled() const;

    // Diagnostics for the last DecodeChunk() call.
    u32 GetIntelFileSize() const;
    size_t GetConsumedBytes() const;
    int GetLeftoverBits() const;

private:
    struct BitReader
    {
        BitReader(const u8 *data, size_t length);

        void Ensure(int bits);
        u32 Get(int bits);
        void AlignToWord();
        void Skip(size_t bytes);
        void ReadRaw(u8 *outBuffer, size_t bytes);
        int GetLeftoverBits() const;
        size_t GetConsumedBytes() const;

        const u8 *data;
        size_t length;
        size_t position;
        u64 buffer;
        int bitCount;
    };

    struct Huffman
    {
        static constexpr int MaxCodeLength = 16;
        static constexpr int MaxSymbols = MaxMainSymbols;

        Huffman();

        // Build a canonical MSB-first decode table. Returns false when the
        // code lengths are over-subscribed.
        bool Build(const u8 *lengths, int symbolCount);
        int Decode(BitReader &bits) const;

        int count[MaxCodeLength + 1];
        int symbol[MaxSymbols];
        bool empty;
    };

    void ReadBlockHeader(BitReader &bits);
    void ReadCodeLengths(BitReader &bits, u8 *lengths, int first, int last);
    void DecodeSymbols(BitReader &bits, size_t end, size_t &produced);
    void ApplyE8Translation(u8 *data, size_t length);

    std::vector<u8> window;
    u32 windowSize;
    u32 windowMask;
    u32 numPositionSlots;
    u32 mainSymbols;
    u32 windowPosition;
    u64 totalOutput;

    u32 repeatOffsets[3];

    std::array<u8, MaxMainSymbols> mainCodeLengths;
    std::array<u8, 249> lengthCodeLengths;
    std::array<u8, 8> alignedCodeLengths;

    Huffman mainTree;
    Huffman lengthTree;
    Huffman alignedTree;

    int blockType;
    int previousBlockType;
    u32 blockLength;
    u32 blockRemaining;
    u32 previousBlockLength;

    bool headerRead;
    bool intelStarted;
    u32 intelFileSize;
    u64 chunkCount;

    bool restoreRepeatOffsets;
    bool e8Translation;

    size_t lastConsumedBytes;
    int lastLeftoverBits;
};

#endif // XEXLZXDECODER_H
