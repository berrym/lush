/**
 * @file csi_decoder.h
 * @brief Pure CSI / SS3 sequence -> SPECIAL_KEY decoder for the live
 *        LLE input path.
 *
 * The byte-reading half of `lle_unix_interface_read_event` (read from
 * terminal_fd, accumulate params + final byte) is unit-test hostile
 * because it owns a real file descriptor. These two helpers carry the
 * decoding half -- pure data in, event out -- so every CSI / SS3 form
 * the live reader emits is reachable by a unit test.
 *
 * Both helpers populate the SPECIAL_KEY fields of the event (`.key`,
 * `.modifiers`, `.keycode`); the caller is responsible for setting
 * `.type` (always LLE_INPUT_TYPE_SPECIAL_KEY) and `.timestamp`.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef LLE_CSI_DECODER_H
#define LLE_CSI_DECODER_H

#include "lle/terminal_abstraction.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Decode a CSI sequence (bytes between "ESC[" and the final byte)
 *        into a SPECIAL_KEY event.
 *
 * Expected CSI form: `<num>[;<mod>]<final>`, where `<mod>` is the xterm
 * `1 + bitmask` modifier code (Shift=1, Alt=2, Ctrl=4, so mod=2 means
 * Shift, mod=5 means Ctrl, mod=8 means Ctrl+Alt+Shift, etc.).
 *
 * Tilde-final keys are keyed by `<num>`:
 *   1, 7 -> HOME    2 -> INSERT    3 -> DELETE    4, 8 -> END
 *   5 -> PAGE_UP   6 -> PAGE_DOWN
 *   15 -> F5  17 -> F6  18 -> F7  19 -> F8
 *   20 -> F9  21 -> F10 23 -> F11 24 -> F12
 *
 * Letter-final keys are keyed by `<final>`:
 *   A -> UP  B -> DOWN  C -> RIGHT  D -> LEFT  H -> HOME  F -> END
 *
 * On success, `event->data.special_key.key` is set to the mapped key,
 * `.modifiers` to the parsed modifier bits, and `.keycode` to 0. On a
 * shape that scans but does not map to a known key, returns false with
 * `.key = LLE_KEY_UNKNOWN`, `.modifiers` populated, `.keycode = 0`.
 *
 * @param params       Parameter bytes between "ESC[" and the final byte.
 *                     May be NUL-padded; only the first `params_len`
 *                     bytes are scanned.
 * @param params_len   Length of `params` in bytes (0 is valid, meaning
 *                     no numeric parameter -- treat as `num = 0`).
 * @param final_byte   The CSI final byte (0x40-0x7E range).
 * @param event        Output event; the special_key sub-struct is
 *                     populated. Caller sets `.type` and `.timestamp`.
 * @return true if the (params, final) tuple mapped to a known key,
 *         false otherwise.
 */
bool lle_csi_decode(const char *params, size_t params_len, char final_byte,
                    lle_input_event_t *event);

/**
 * @brief Decode an SS3 sequence (the single byte after "ESC O") into
 *        a SPECIAL_KEY event.
 *
 * SS3 covers the alternate keypad encodings some terminals use:
 *   H -> HOME  F -> END
 *   P -> F1    Q -> F2    R -> F3    S -> F4
 *
 * SS3 has no modifier parameter in this dialect; `.modifiers` is set
 * to 0 unconditionally.
 *
 * @param final_byte   The byte after `ESC O`.
 * @param event        Output event; the special_key sub-struct is
 *                     populated. Caller sets `.type` and `.timestamp`.
 * @return true if `final_byte` mapped to a known key, false otherwise.
 */
bool lle_ss3_decode(char final_byte, lle_input_event_t *event);

#endif /* LLE_CSI_DECODER_H */
