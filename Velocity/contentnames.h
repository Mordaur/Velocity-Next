#ifndef CONTENTNAMES_H
#define CONTENTNAMES_H

#include <QString>

#include <string>

#include <XboxInternals/Fatx/FatxConstants.h>

// Turns raw FATX names (title IDs, content-type folders, system directories)
// into readable labels. Purely a presentation concern: callers keep using the
// original name for paths and device operations.
namespace ContentNames
{
    // True when the name looks like an 8-digit hex title ID (not a content-type
    // folder, which also uses 8 hex digits but starts with 0).
    bool IsTitleId(const std::string &name);

    // Label for a name, e.g. "584109B6" -> "Unknown Game", "000D0000" ->
    // "Arcade Game", "FFFE07D1" -> "Profiles". Returns an empty string when the
    // name has no known meaning.
    QString Recognize(const std::string &name, bool isDirectory);

    // Display text for a tree entry: "Arcade Game (000D0000)" when friendly
    // names are enabled, the raw name otherwise.
    QString Display(const std::string &name, bool isDirectory, bool friendly);
}

#endif // CONTENTNAMES_H
