#ifndef XBOXINTERNALS_ENUMTRAITS_H
#define XBOXINTERNALS_ENUMTRAITS_H

// Enums read from files specialize this next to their definition. ReadEnum()
// uses it, so a raw value can never be turned into an unchecked enumerator.
template <typename E>
struct EnumTraits;

#endif // XBOXINTERNALS_ENUMTRAITS_H
