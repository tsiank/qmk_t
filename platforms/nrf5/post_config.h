// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The Nordic backend supplies its own endpoint map. Keep the generic QMK
// descriptor capacity check usable without a keyboard-specific workaround.
#ifndef MAX_ENDPOINTS
#    define MAX_ENDPOINTS 8
#endif
