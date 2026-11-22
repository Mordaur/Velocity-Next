#ifndef XBOXINTERNALS_DISC_ASF_H
#define XBOXINTERNALS_DISC_ASF_H

#include <XboxInternals/Export.h>
#include <XboxInternals/IO/BaseIO.h>

namespace XboxInternals::Asf
{
    // Description: true when the stream begins with an ASF Header Object.
    // Restores the stream position.
    XBOXINTERNALS_EXPORT bool IsAsf(BaseIO *io);

    // Description: true when the ASF header declares content encryption
    // (Windows Media DRM or PlayReady), which no media backend can decode.
    // Restores the stream position.
    XBOXINTERNALS_EXPORT bool IsDrmProtected(BaseIO *io);
}

#endif // XBOXINTERNALS_DISC_ASF_H
