/* libFuzzer target for the XEX header parser.
 *
 * Constructs Xex over a MemoryIO so the optional-header table, section table,
 * rating bytes and resource tables are parsed from arbitrary input. Bounds
 * violations surface as std::string throws from MemoryIO; the contract is that
 * nothing crashes and no out-of-range enum value is produced.
 */

#include <XboxInternals/IO/MemoryIO.h>
#include <XboxInternals/Xex/Xex.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    std::vector<u8> buffer(data, data + size);
    MemoryIO io(buffer.data(), buffer.size());

    try
    {
        Xex xex(&io);
        // Touch the accessors that compare/translate enum members, so out-of-range
        // ratings and image flags are exercised too.
        (void)xex.GetEsrbRatingText();
        (void)xex.GetPegiRatingText();
        (void)xex.GetOflcAURatingText();
        (void)xex.GetOflcNZRatingText();
        (void)xex.GetImageFlags();
        (void)xex.GetModuleFlags();
        (void)xex.GetOriginalPEImageName();
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
