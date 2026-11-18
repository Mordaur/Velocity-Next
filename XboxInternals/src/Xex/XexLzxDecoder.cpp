#include <XboxInternals/Xex/XexLzxDecoder.h>

#include <algorithm>
#include <cstring>
#include <string>

/* Ported from the validated prototype in tmp/lzx/xexlzx.cpp (see
 * tmp/lzx/REPORT.md for the byte-level evidence and the kernel dump
 * references). The algorithm is plain LZX; the XEX specifics are the per-chunk
 * bit reader reset, the fixed 32 KiB window and the E8 pass applied to the
 * chunk output. */

namespace
{
    constexpr DWORD kMinMatch = 2;
    constexpr DWORD kMaxMatch = 257;

    constexpr BYTE ExtraBits(int slot)
    {
        // extra_bits[] = 0,0,0,0,1,1,2,2,3,3,4,4,...,17
        return slot < 4 ? 0 : (slot < 36 ? static_cast<BYTE>(slot / 2 - 1) : 17);
    }

    // position_base[slot] = sum(1 << extra_bits[i]) for i < slot; the kernel
    // precomputes base - 2 in its context (ctx+3016, see sub_801589F8).
    struct PositionBaseTable
    {
        DWORD base[XexLzxDecoder::NumPositionSlots];

        constexpr PositionBaseTable() : base()
        {
            DWORD sum = 0;

            for (DWORD i = 0; i < XexLzxDecoder::NumPositionSlots; i++)
            {
                base[i] = sum;
                sum += 1u << ExtraBits(static_cast<int>(i));
            }
        }
    };

    constexpr PositionBaseTable kPositionBase;

    [[noreturn]] void Throw(const char *message)
    {
        throw std::string("XEX LZX: ") + message;
    }
}

// ---------------------------------------------------------------------------
// Bit reader: 16-bit little-endian words, bits taken MSB-first (the XEX engine
// keeps the valid bits at the top of a 32-bit register and shifts in one word
// at a time, sub_80159260 / sub_801592C8).
// ---------------------------------------------------------------------------
XexLzxDecoder::BitReader::BitReader(const BYTE *data, size_t length) :
    data(data), length(length), position(0), buffer(0), bitCount(0)
{
}

void XexLzxDecoder::BitReader::Ensure(int bits)
{
    while (bitCount < bits)
    {
        DWORD word = 0;

        if (position + 1 < length)
        {
            word = static_cast<DWORD>(data[position]) |
                   (static_cast<DWORD>(data[position + 1]) << 8);
            position += 2;
        }
        else if (position < length)
        {
            // defensive: a trailing odd byte is used as the high half
            word = static_cast<DWORD>(data[position]) << 8;
            position += 1;
        }

        buffer = ((buffer << 16) | word) & 0xFFFFFFFFFFFFull;
        bitCount += 16;
    }
}

DWORD XexLzxDecoder::BitReader::Get(int bits)
{
    if (bits <= 0)
        return 0;

    Ensure(bits);
    bitCount -= bits;

    return static_cast<DWORD>((buffer >> bitCount) & ((1ull << bits) - 1));
}

void XexLzxDecoder::BitReader::AlignToWord()
{
    // Discard the residual bits of the current 16-bit word; `position` already
    // points past the last word that was fetched, which is the alignment point
    // the kernel uses for stored blocks.
    buffer = 0;
    bitCount = 0;
}

void XexLzxDecoder::BitReader::Skip(size_t bytes)
{
    if (bytes > length - position)
        Throw("stored block runs past the end of the chunk");

    position += bytes;
}

void XexLzxDecoder::BitReader::ReadRaw(BYTE *outBuffer, size_t bytes)
{
    if (bytes > length - position)
        Throw("stored block header runs past the end of the chunk");

    std::memcpy(outBuffer, data + position, bytes);
    position += bytes;
}

int XexLzxDecoder::BitReader::GetLeftoverBits() const
{
    return bitCount;
}

size_t XexLzxDecoder::BitReader::GetConsumedBytes() const
{
    return position;
}

// ---------------------------------------------------------------------------
// Canonical MSB-first Huffman decoding (the kernel's tables are 10/8/3 bits
// wide and fall back to a bit-by-bit walk for longer codes).
// ---------------------------------------------------------------------------
XexLzxDecoder::Huffman::Huffman() :
    count(), symbol(), empty(true)
{
}

bool XexLzxDecoder::Huffman::Build(const BYTE *lengths, int symbolCount)
{
    for (int i = 0; i <= MaxCodeLength; i++)
        count[i] = 0;

    for (int i = 0; i < symbolCount; i++)
        count[lengths[i]]++;

    int total = 0;
    for (int i = 1; i <= MaxCodeLength; i++)
        total += count[i];

    if (total == 0)
    {
        // an empty tree is legal for the length tree (no matches in the block)
        empty = true;
        return true;
    }

    empty = false;

    int left = 1;
    for (int length = 1; length <= MaxCodeLength; length++)
    {
        left <<= 1;
        left -= count[length];

        if (left < 0)
            return false;
    }

    int offsets[MaxCodeLength + 1] = {};
    for (int length = 1; length < MaxCodeLength; length++)
        offsets[length + 1] = offsets[length] + count[length];

    for (int i = 0; i < symbolCount; i++)
    {
        if (lengths[i] != 0)
            symbol[offsets[lengths[i]]++] = i;
    }

    return true;
}

int XexLzxDecoder::Huffman::Decode(BitReader &bits) const
{
    int code = 0;
    int first = 0;
    int index = 0;

    for (int length = 1; length <= MaxCodeLength; length++)
    {
        code |= static_cast<int>(bits.Get(1));

        const int currentCount = count[length];
        if (code - first < currentCount)
            return symbol[index + code - first];

        index += currentCount;
        first = (first + currentCount) << 1;
        code <<= 1;
    }

    return -1;
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------
XexLzxDecoder::XexLzxDecoder() :
    window(), windowPosition(0), totalOutput(0), repeatOffsets(), mainCodeLengths(),
    lengthCodeLengths(), alignedCodeLengths(), mainTree(), lengthTree(), alignedTree(),
    blockType(0), previousBlockType(0), blockLength(0), blockRemaining(0), previousBlockLength(0),
    headerRead(false), intelStarted(false), intelFileSize(0), chunkCount(0),
    restoreRepeatOffsets(true), e8Translation(true), lastConsumedBytes(0), lastLeftoverBits(0)
{
    repeatOffsets[0] = 1;
    repeatOffsets[1] = 1;
    repeatOffsets[2] = 1;
}

void XexLzxDecoder::Reset()
{
    window.fill(0);
    windowPosition = 0;
    totalOutput = 0;

    repeatOffsets[0] = 1;
    repeatOffsets[1] = 1;
    repeatOffsets[2] = 1;

    mainCodeLengths.fill(0);
    lengthCodeLengths.fill(0);
    alignedCodeLengths.fill(0);

    mainTree = Huffman();
    lengthTree = Huffman();
    alignedTree = Huffman();

    blockType = 0;
    previousBlockType = 0;
    blockLength = 0;
    blockRemaining = 0;
    previousBlockLength = 0;

    headerRead = false;
    intelStarted = false;
    intelFileSize = 0;
    chunkCount = 0;

    lastConsumedBytes = 0;
    lastLeftoverBits = 0;
}

void XexLzxDecoder::SetRestoreRepeatOffsets(bool restore)
{
    restoreRepeatOffsets = restore;
}

void XexLzxDecoder::SetE8Translation(bool enable)
{
    e8Translation = enable;
}

bool XexLzxDecoder::IsE8TranslationEnabled() const
{
    return e8Translation;
}

DWORD XexLzxDecoder::GetIntelFileSize() const
{
    return intelFileSize;
}

size_t XexLzxDecoder::GetConsumedBytes() const
{
    return lastConsumedBytes;
}

int XexLzxDecoder::GetLeftoverBits() const
{
    return lastLeftoverBits;
}

void XexLzxDecoder::DecodeChunk(const BYTE *data, size_t dataLength, BYTE *outBuffer, size_t outLength)
{
    if (outLength > FrameSize)
        Throw("chunk output is larger than one frame");

    if (data == nullptr || outBuffer == nullptr)
        Throw("null buffer");

    BitReader bits(data, dataLength);
    const DWORD chunkStart = windowPosition;
    size_t produced = 0;

    while (produced < outLength)
    {
        if (blockRemaining == 0)
            ReadBlockHeader(bits);

        const size_t toProduce = std::min<size_t>(blockRemaining, outLength - produced);
        if (toProduce == 0)
            Throw("zero-length block");

        if (blockType == 3)
        {
            if (windowPosition + toProduce > WindowSize)
                Throw("stored block crosses the frame boundary");

            const size_t sourceOffset = bits.GetConsumedBytes();
            bits.Skip(toProduce);
            std::memcpy(window.data() + windowPosition, data + sourceOffset, toProduce);

            windowPosition += static_cast<DWORD>(toProduce);
        }
        else
        {
            DecodeSymbols(bits, produced + toProduce, produced);
        }

        blockRemaining -= static_cast<DWORD>(toProduce);
    }

    if (windowPosition - chunkStart > outLength)
        Throw("decoder produced more than the chunk's expected output");

    if (windowPosition - chunkStart < outLength)
        Throw("chunk produced fewer bytes than expected");

    std::memcpy(outBuffer, window.data() + chunkStart, outLength);

    totalOutput += outLength;
    ApplyE8Translation(outBuffer, outLength);
    chunkCount++;

    lastConsumedBytes = bits.GetConsumedBytes();
    lastLeftoverBits = bits.GetLeftoverBits();

    if (windowPosition == WindowSize)
        windowPosition = 0;
}

void XexLzxDecoder::ReadBlockHeader(BitReader &bits)
{
    if (previousBlockType == 3 && (previousBlockLength & 1) != 0)
    {
        // odd-length stored block: one pad byte up to the next 16-bit boundary
        bits.AlignToWord();
        bits.Skip(1);
    }

    if (!headerRead)
    {
        headerRead = true;

        if (bits.Get(1) != 0)
        {
            const DWORD high = bits.Get(16);
            const DWORD low = bits.Get(16);
            intelFileSize = (high << 16) | low;
        }
        else
        {
            intelFileSize = 0;
        }
    }

    blockType = static_cast<int>(bits.Get(3));

    // separate statements: the evaluation order of several Get() calls inside
    // one expression is unspecified
    const DWORD lengthHigh = bits.Get(8);
    const DWORD lengthMiddle = bits.Get(8);
    const DWORD lengthLow = bits.Get(8);
    blockLength = (lengthHigh << 16) | (lengthMiddle << 8) | lengthLow;
    blockRemaining = blockLength;

    if (blockLength == 0)
        Throw("zero block length");

    if (blockType == 2)
    {
        for (size_t i = 0; i < alignedCodeLengths.size(); i++)
            alignedCodeLengths[i] = static_cast<BYTE>(bits.Get(3));

        if (!alignedTree.Build(alignedCodeLengths.data(), static_cast<int>(alignedCodeLengths.size())))
            Throw("invalid aligned-offset tree");
    }

    if (blockType == 1 || blockType == 2)
    {
        ReadCodeLengths(bits, mainCodeLengths.data(), 0, 256);
        ReadCodeLengths(bits, mainCodeLengths.data(), 256, static_cast<int>(mainCodeLengths.size()));

        if (!mainTree.Build(mainCodeLengths.data(), static_cast<int>(mainCodeLengths.size())))
            Throw("invalid main tree");

        if (mainTree.empty)
            Throw("empty main tree");

        if (mainCodeLengths[0xE8] != 0)
            intelStarted = true;

        ReadCodeLengths(bits, lengthCodeLengths.data(), 0, static_cast<int>(lengthCodeLengths.size()));

        if (!lengthTree.Build(lengthCodeLengths.data(), static_cast<int>(lengthCodeLengths.size())))
            Throw("invalid length tree");
    }
    else if (blockType == 3)
    {
        intelStarted = true;
        bits.AlignToWord();

        // stored blocks start with 12 bytes of big-endian R0/R1/R2
        BYTE repeatHeader[12];
        bits.ReadRaw(repeatHeader, sizeof(repeatHeader));

        if (restoreRepeatOffsets)
        {
            for (int i = 0; i < 3; i++)
            {
                const BYTE *entry = repeatHeader + i * 4;

                repeatOffsets[i] = (static_cast<DWORD>(entry[0]) << 24) |
                                   (static_cast<DWORD>(entry[1]) << 16) |
                                   (static_cast<DWORD>(entry[2]) << 8) |
                                   static_cast<DWORD>(entry[3]);

                if (repeatOffsets[i] > WindowSize)
                    Throw("stored block repeated offset out of range");
            }
        }
    }
    else
    {
        throw std::string("XEX LZX: invalid block type ") + std::to_string(blockType);
    }

    previousBlockType = blockType;
    previousBlockLength = blockLength;
}

void XexLzxDecoder::ReadCodeLengths(BitReader &bits, BYTE *lengths, int first, int last)
{
    BYTE preTreeLengths[20];

    for (size_t i = 0; i < sizeof(preTreeLengths); i++)
        preTreeLengths[i] = static_cast<BYTE>(bits.Get(4));

    Huffman preTree;
    if (!preTree.Build(preTreeLengths, static_cast<int>(sizeof(preTreeLengths))))
        Throw("invalid pretree");

    int index = first;

    while (index < last)
    {
        const int symbol = preTree.Decode(bits);

        if (symbol < 0)
            Throw("invalid pretree symbol");

        if (symbol == 17)
        {
            int runLength = static_cast<int>(bits.Get(4)) + 4;

            while (runLength-- > 0 && index < last)
                lengths[index++] = 0;
        }
        else if (symbol == 18)
        {
            int runLength = static_cast<int>(bits.Get(5)) + 20;

            while (runLength-- > 0 && index < last)
                lengths[index++] = 0;
        }
        else if (symbol == 19)
        {
            int runLength = static_cast<int>(bits.Get(1)) + 4;
            const int delta = preTree.Decode(bits);

            if (delta < 0)
                Throw("invalid pretree symbol");

            int value = lengths[index] - delta;
            if (value < 0)
                value += 17;

            while (runLength-- > 0 && index < last)
                lengths[index++] = static_cast<BYTE>(value);
        }
        else
        {
            int value = lengths[index] - symbol;
            if (value < 0)
                value += 17;

            lengths[index++] = static_cast<BYTE>(value);
        }
    }
}

void XexLzxDecoder::DecodeSymbols(BitReader &bits, size_t end, size_t &produced)
{
    while (produced < end)
    {
        const int symbol = mainTree.Decode(bits);

        if (symbol < 0)
            Throw("invalid main-tree symbol");

        if (symbol < 256)
        {
            if (windowPosition >= WindowSize)
                Throw("literal past the frame boundary");

            window[windowPosition++] = static_cast<BYTE>(symbol);
            produced++;
            continue;
        }

        const int element = symbol - 256;

        DWORD matchLength = static_cast<DWORD>(element) & 7;
        if (matchLength == 7)
        {
            if (lengthTree.empty)
                Throw("length tree needed but empty");

            const int footer = lengthTree.Decode(bits);
            if (footer < 0)
                Throw("invalid length-tree symbol");

            matchLength += static_cast<DWORD>(footer);
        }

        matchLength += kMinMatch;

        if (matchLength > kMaxMatch)
            Throw("match length out of range");

        const int slot = element >> 3;
        if (slot >= static_cast<int>(NumPositionSlots))
            Throw("position slot out of range");

        DWORD matchOffset;

        if (slot == 0)
        {
            matchOffset = repeatOffsets[0];
        }
        else if (slot == 1)
        {
            matchOffset = repeatOffsets[1];
            repeatOffsets[1] = repeatOffsets[0];
            repeatOffsets[0] = matchOffset;
        }
        else if (slot == 2)
        {
            matchOffset = repeatOffsets[2];
            repeatOffsets[2] = repeatOffsets[0];
            repeatOffsets[0] = matchOffset;
        }
        else
        {
            const DWORD extra = ExtraBits(slot);
            matchOffset = kPositionBase.base[slot] - 2;

            if (extra >= 3 && blockType == 2)
            {
                if (extra > 3)
                    matchOffset += bits.Get(static_cast<int>(extra - 3)) << 3;

                const int alignedSymbol = alignedTree.Decode(bits);
                if (alignedSymbol < 0)
                    Throw("invalid aligned-offset symbol");

                matchOffset += static_cast<DWORD>(alignedSymbol);
            }
            else if (extra != 0)
            {
                matchOffset += bits.Get(static_cast<int>(extra));
            }

            repeatOffsets[2] = repeatOffsets[1];
            repeatOffsets[1] = repeatOffsets[0];
            repeatOffsets[0] = matchOffset;
        }

        if (matchOffset == 0 || matchOffset > WindowSize)
            Throw("match offset out of range");

        if (windowPosition + matchLength > WindowSize)
            Throw("match crosses the frame boundary");

        // Overlapping copies are byte-by-byte; the source is masked per byte,
        // which is the equivalent of the kernel's 261-byte window mirror.
        const DWORD source = (windowPosition - matchOffset) & (WindowSize - 1);

        for (DWORD i = 0; i < matchLength; i++)
        {
            window[(windowPosition + i) & (WindowSize - 1)] =
                window[(source + i) & (WindowSize - 1)];
        }

        windowPosition += matchLength;
        produced += matchLength;
    }
}

void XexLzxDecoder::ApplyE8Translation(BYTE *data, size_t length)
{
    if (!e8Translation || intelFileSize == 0 || !intelStarted || length <= 10)
        return;

    // the kernel uses a per-call counter and stops after 0x8000 calls
    if (chunkCount >= 0x8000)
        return;

    const INT32 fileSize = static_cast<INT32>(intelFileSize);
    INT32 currentPosition = static_cast<INT32>(totalOutput - length);
    const size_t limit = length - 10; // the last 6 bytes are never touched
    size_t i = 0;

    while (i < limit)
    {
        if (data[i] != 0xE8)
        {
            i++;
            currentPosition++;
            continue;
        }

        const INT32 absoluteOffset = static_cast<INT32>(
            static_cast<DWORD>(data[i + 1]) |
            (static_cast<DWORD>(data[i + 2]) << 8) |
            (static_cast<DWORD>(data[i + 3]) << 16) |
            (static_cast<DWORD>(data[i + 4]) << 24));

        if (absoluteOffset >= -currentPosition && absoluteOffset < fileSize)
        {
            const INT32 relativeOffset = (absoluteOffset >= 0)
                ? absoluteOffset - currentPosition
                : absoluteOffset + fileSize;

            data[i + 1] = static_cast<BYTE>(relativeOffset);
            data[i + 2] = static_cast<BYTE>(relativeOffset >> 8);
            data[i + 3] = static_cast<BYTE>(relativeOffset >> 16);
            data[i + 4] = static_cast<BYTE>(relativeOffset >> 24);
        }

        i += 5;
        currentPosition += 5;
    }
}
