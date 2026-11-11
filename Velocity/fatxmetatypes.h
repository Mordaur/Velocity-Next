#ifndef FATXMETATYPES_H
#define FATXMETATYPES_H

#include <QMetaType>

#include <XboxInternals/Fatx/FatxConstants.h>
#include <XboxInternals/Fatx/FatxDrive.h>

Q_DECLARE_METATYPE(FatxFileEntry*)
Q_DECLARE_METATYPE(FatxDrive*)
Q_DECLARE_METATYPE(Partition*)

#endif // FATXMETATYPES_H
