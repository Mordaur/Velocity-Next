/* libFuzzer target for the XEX LZX decoder.
 *
 * The decoder is the newest parser in the tree and takes arbitrary attacker
 * bytes (a compressed XEX). The contract under test is the same as the smoke
 * target's: return or throw std::string, never crash, overrun or loop.
 */

#include <XboxInternals/Xex/XexLzxDecoder.h>

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    XexLzxDecoder decoder;
    u8 output[0x400] = {};

    try
    {
        decoder.DecodeChunk(reinterpret_cast<const u8 *>(data), size, output, sizeof(output));
    }
    catch (const std::string &)
    {
    }
    catch (const std::exception &)
    {
    }
    catch (...)
    {
    }

    return 0;
}
