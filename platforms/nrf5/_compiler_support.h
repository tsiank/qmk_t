// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The SDK owns STATIC_ASSERT, MIN/MAX, CEILING and ARRAY_SIZE. Load it before
// QMK's guarded fallbacks, regardless of which QMK header is included first.
#include "app_util.h"

// QMK uses an object-like packing attribute. Keep the historical Nordic QMK
// meaning here rather than branching on PROTOCOL_NRF5 in quantum/util.h.
#ifdef PACKED
#    undef PACKED
#endif
#ifdef __GNUC__
#    define PACKED __attribute__((packed))
#else
#    define PACKED
#endif
