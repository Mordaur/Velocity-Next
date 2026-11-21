#ifndef XBOXINTERNALS_TYPES_H
#define XBOXINTERNALS_TYPES_H

#include <cstdint>

// Fixed-width integer names used throughout XboxInternals. They are the same
// types on every platform; Win32 names (DWORD, BYTE, ...) belong only in the
// private OS adapters that include <windows.h>.
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8,
        "unsigned fixed-width aliases have the wrong size");
static_assert(sizeof(i16) == 2 && sizeof(i32) == 4 && sizeof(i64) == 8,
        "signed fixed-width aliases have the wrong size");

#endif // XBOXINTERNALS_TYPES_H
