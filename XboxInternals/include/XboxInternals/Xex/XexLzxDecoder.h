#ifndef XEXLZXDECODER_H
#define XEXLZXDECODER_H

#include <XboxInternals/TypeDefinitions.h>
#include <XboxInternals/Export.h>

#include <array>
#include <cstddef>

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
    static constexpr DWORD WindowSize = 0x8000;
    static constexpr DWORD FrameSize = 0x8000;
    static constexpr DWORD NumPositionSlots = 30;

    XexLzxDecoder();

    // Forget all decode state (start of a new image).
    void Reset();

    // Decode exactly outLength bytes of the next chunk into outBuffer.
    // Throws std::string on malformed input.
    void DecodeChunk(const BYTE *data, size_t dataLength, BYTE *outBuffer, size_t outLength);

    // Stored (uncompressed) LZX blocks carry a 12-byte big-endian R0/R1/R2
    // header; disable only if a stream turns out not to have one.
    void SetRestoreRepeatOffsets(bool restore);

    // LZX "Intel E8" call translation, applied to the chunk output (no-op
    // unless the stream announces a file size).
    void SetE8Translation(bool enable);
    bool IsE8TranslationEnabled() const;

    // Diagnostics for the last DecodeChunk() call.
    DWORD GetIntelFileSize() const;
    size_t GetConsumedBytes() const;
    int GetLeftoverBits() const;

private:
    struct BitReader
    {
        BitReader(const BYTE *data, size_t length);

        void Ensure(int bits);
        DWORD Get(int bits);
        void AlignToWord();
        void Skip(size_t bytes);
        void ReadRaw(BYTE *outBuffer, size_t bytes);
        int GetLeftoverBits() const;
        size_t GetConsumedBytes() const;

        const BYTE *data;
        size_t length;
        size_t position;
        UINT64 buffer;
        int bitCount;
    };

    struct Huffman
    {
        static constexpr int MaxCodeLength = 16;
        static constexpr int MaxSymbols = 496;

        Huffman();

        // Build a canonical MSB-first decode table. Returns false when the
        // code lengths are over-subscribed.
        bool Build(const BYTE *lengths, int symbolCount);
        int Decode(BitReader &bits) const;

        int count[MaxCodeLength + 1];
        int symbol[MaxSymbols];
        bool empty;
    };

    void ReadBlockHeader(BitReader &bits);
    void ReadCodeLengths(BitReader &bits, BYTE *lengths, int first, int last);
    void DecodeSymbols(BitReader &bits, size_t end, size_t &produced);
    void ApplyE8Translation(BYTE *data, size_t length);

    std::array<BYTE, WindowSize> window;
    DWORD windowPosition;
    UINT64 totalOutput;

    DWORD repeatOffsets[3];

    std::array<BYTE, Huffman::MaxSymbols> mainCodeLengths;
    std::array<BYTE, 249> lengthCodeLengths;
    std::array<BYTE, 8> alignedCodeLengths;

    Huffman mainTree;
    Huffman lengthTree;
    Huffman alignedTree;

    int blockType;
    int previousBlockType;
    DWORD blockLength;
    DWORD blockRemaining;
    DWORD previousBlockLength;

    bool headerRead;
    bool intelStarted;
    DWORD intelFileSize;
    UINT64 chunkCount;

    bool restoreRepeatOffsets;
    bool e8Translation;

    size_t lastConsumedBytes;
    int lastLeftoverBits;
};

#endif // XEXLZXDECODER_H
