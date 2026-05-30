/**
 * @file csi_decoder.c
 * @brief Implementation of the pure CSI / SS3 decoder.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "lle/csi_decoder.h"

#include <string.h>

/// Scan a single decimal integer starting at `*p`, advancing `*p` past
/// the digits. Treats no-digits as 0. Bounded by `end`.
static int scan_uint(const char **p, const char *end) {
    int n = 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        n = n * 10 + (**p - '0');
        (*p)++;
    }
    return n;
}

/// Convert the xterm modifier param (1 + bitmask) into LLE_MOD_* flags.
/// mod_param <= 1 means no modifiers.
static uint32_t modifier_bits(int mod_param) {
    if (mod_param <= 1) {
        return 0;
    }
    int bits = mod_param - 1;
    uint32_t out = 0;
    if (bits & 1) {
        out |= LLE_MOD_SHIFT;
    }
    if (bits & 2) {
        out |= LLE_MOD_ALT;
    }
    if (bits & 4) {
        out |= LLE_MOD_CTRL;
    }
    return out;
}

bool lle_csi_decode(const char *params, size_t params_len, char final_byte,
                    lle_input_event_t *event) {
    if (!event) {
        return false;
    }

    event->data.special_key.key = LLE_KEY_UNKNOWN;
    event->data.special_key.keycode = 0;
    event->data.special_key.modifiers = 0;

    /// Parse a leading numeric parameter and an optional `;<mod>`.
    int num = 0;
    int mod_param = 0;
    if (params && params_len > 0) {
        const char *p = params;
        const char *end = params + params_len;
        num = scan_uint(&p, end);
        if (p < end && *p == ';') {
            p++;
            mod_param = scan_uint(&p, end);
        }
    }

    event->data.special_key.modifiers = modifier_bits(mod_param);

    if (final_byte == '~') {
        /// Tilde-form keys are keyed by the numeric parameter. xterm
        /// function-key numbering: F5=15, F6=17, F7=18, F8=19, F9=20,
        /// F10=21, F11=23, F12=24 (16 and 22 are skipped in xterm).
        switch (num) {
        case 1:
        case 7:
            event->data.special_key.key = LLE_KEY_HOME;
            return true;
        case 2:
            event->data.special_key.key = LLE_KEY_INSERT;
            return true;
        case 3:
            event->data.special_key.key = LLE_KEY_DELETE;
            return true;
        case 4:
        case 8:
            event->data.special_key.key = LLE_KEY_END;
            return true;
        case 5:
            event->data.special_key.key = LLE_KEY_PAGE_UP;
            return true;
        case 6:
            event->data.special_key.key = LLE_KEY_PAGE_DOWN;
            return true;
        case 15:
            event->data.special_key.key = LLE_KEY_F5;
            return true;
        case 17:
            event->data.special_key.key = LLE_KEY_F6;
            return true;
        case 18:
            event->data.special_key.key = LLE_KEY_F7;
            return true;
        case 19:
            event->data.special_key.key = LLE_KEY_F8;
            return true;
        case 20:
            event->data.special_key.key = LLE_KEY_F9;
            return true;
        case 21:
            event->data.special_key.key = LLE_KEY_F10;
            return true;
        case 23:
            event->data.special_key.key = LLE_KEY_F11;
            return true;
        case 24:
            event->data.special_key.key = LLE_KEY_F12;
            return true;
        default:
            return false;
        }
    }

    /// Letter-final keys (optionally `ESC[1;<mod>` prefixed for modified
    /// nav).
    switch (final_byte) {
    case 'A':
        event->data.special_key.key = LLE_KEY_UP;
        return true;
    case 'B':
        event->data.special_key.key = LLE_KEY_DOWN;
        return true;
    case 'C':
        event->data.special_key.key = LLE_KEY_RIGHT;
        return true;
    case 'D':
        event->data.special_key.key = LLE_KEY_LEFT;
        return true;
    case 'H':
        event->data.special_key.key = LLE_KEY_HOME;
        return true;
    case 'F':
        event->data.special_key.key = LLE_KEY_END;
        return true;
    default:
        return false;
    }
}

bool lle_ss3_decode(char final_byte, lle_input_event_t *event) {
    if (!event) {
        return false;
    }

    event->data.special_key.key = LLE_KEY_UNKNOWN;
    event->data.special_key.keycode = 0;
    event->data.special_key.modifiers = 0;

    switch (final_byte) {
    case 'H':
        event->data.special_key.key = LLE_KEY_HOME;
        return true;
    case 'F':
        event->data.special_key.key = LLE_KEY_END;
        return true;
    case 'P':
        event->data.special_key.key = LLE_KEY_F1;
        return true;
    case 'Q':
        event->data.special_key.key = LLE_KEY_F2;
        return true;
    case 'R':
        event->data.special_key.key = LLE_KEY_F3;
        return true;
    case 'S':
        event->data.special_key.key = LLE_KEY_F4;
        return true;
    default:
        return false;
    }
}
