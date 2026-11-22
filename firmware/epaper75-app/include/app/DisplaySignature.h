#pragma once

#include <stdint.h>

#include "app/Models.h"

namespace app {

// Hash of exactly what the given page shows, quantized the same way the
// renderer formats it. A wake only spends a ~20 s full refresh when this
// differs from the signature of the image already on the panel. Things that
// change every wake but carry no information (fetch minute, uptime, next
// wake countdown) are deliberately excluded.
uint32_t visibleSignature(const UiModel& model);

}  // namespace app
