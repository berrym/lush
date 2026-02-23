/**
 * @file powerline.h
 * @brief LLE Powerline Renderer - Colored block segments with arrow separators
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Renders prompt segments as colored blocks with powerline arrow
 * separators between them. Each segment gets foreground and background
 * colors, and the separator between adjacent segments uses the previous
 * segment's background as its foreground.
 */

#ifndef LLE_PROMPT_POWERLINE_H
#define LLE_PROMPT_POWERLINE_H

#include "lle/error_handling.h"
#include "lle/prompt/segment.h"
#include "lle/prompt/theme.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Direction for powerline rendering
 */
typedef enum lle_powerline_direction {
    LLE_POWERLINE_LEFT_TO_RIGHT, /**< PS1: right-pointing arrows */
    LLE_POWERLINE_RIGHT_TO_LEFT  /**< RPROMPT: left-pointing arrows */
} lle_powerline_direction_t;

/**
 * @brief Render a powerline prompt from the theme's segment list
 *
 * Iterates through the theme's enabled_segments list, renders each
 * visible segment, wraps with fg+bg colors, and inserts arrow
 * separators with correct color transitions.
 *
 * @param theme     Active theme (provides segment list, colors, symbols)
 * @param segments  Segment registry for rendering
 * @param context   Current prompt context
 * @param direction Arrow direction
 * @param output    Output buffer for assembled ANSI string
 * @param output_size Size of output buffer
 * @return LLE_SUCCESS or error code
 */
lle_result_t lle_powerline_render(const lle_theme_t *theme,
                                  lle_segment_registry_t *segments,
                                  const lle_prompt_context_t *context,
                                  lle_powerline_direction_t direction,
                                  char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* LLE_PROMPT_POWERLINE_H */
