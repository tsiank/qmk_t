// Copyright 2025 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef PROTOCOL_NRF5
// Load the SDK's STATIC_ASSERT before defining QMK's fallback.
#    include "app_util.h"
#endif

/**
 * @brief Perfom an assertion at compile time.
 *
 * `_Static_assert` is C<23, while `static_assert` is C++/C23.
 */
#if !defined(STATIC_ASSERT)
#    ifdef __cplusplus
#        define STATIC_ASSERT static_assert
#    else
#        define STATIC_ASSERT _Static_assert
#    endif
#endif
