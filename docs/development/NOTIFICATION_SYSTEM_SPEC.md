# LLE Notification System Specification

## Overview

The LLE Notification System provides transient, unobtrusive hints displayed below the command line. Notifications auto-dismiss on user action and integrate with the display system through `screen_buffer` for proper cursor positioning and rendering.

**Primary Use Case**: Notifying users in multiline mode that they need `Ctrl+P/Ctrl+N` for history navigation when pressing up/down arrows at buffer boundaries.

## Architecture

### Display Integration

The notification system follows the **completion menu pattern** for display integration:

```
User Action -> Notification State -> Display Controller -> screen_buffer -> Terminal
```

**Critical Principle**: All display output must go through `screen_buffer`. The screen_buffer is the mechanism that makes LLE display work correctly. Notifications are added via `screen_buffer_add_text_rows()`, identical to how completion menus are handled.

### Component Responsibilities

| Component | Responsibility |
|-----------|---------------|
| `notification.c` | State management, message formatting, ANSI styling |
| `lle_readline.c` | Triggering notifications, dismissal logic |
| `display_controller.c` | Copying notification state, adding to screen_buffer, terminal writes |
| `command_layer.c` | Change detection (notification_changed flag) |
| `screen_buffer.c` | Row tracking for cursor positioning |

## Data Structures

### lle_notification_state_t

Defined in `include/lle/notification.h`:

```c
typedef struct lle_notification_state {
    char message[LLE_NOTIFICATION_MAX_MESSAGE];  /* 256 bytes */
    lle_notification_type_t type;                /* Visual style */
    bool visible;                                /* Visibility flag */
    lle_notification_trigger_action_t trigger_action;  /* For suppress-on-repeat */
} lle_notification_state_t;
```

### Notification Types

```c
typedef enum {
    LLE_NOTIFICATION_HINT,    /* Cyan - unobtrusive hints */
    LLE_NOTIFICATION_INFO,    /* Cyan - informational */
    LLE_NOTIFICATION_WARNING, /* Yellow - warnings */
    LLE_NOTIFICATION_ERROR    /* Red - errors */
} lle_notification_type_t;
```

### Trigger Actions

Used for suppress-on-repeat logic:

```c
typedef enum {
    LLE_NOTIF_ACTION_NONE = 0,
    LLE_NOTIF_ACTION_UP_ARROW,   /* Up arrow at first line */
    LLE_NOTIF_ACTION_DOWN_ARROW, /* Down arrow at last line */
} lle_notification_trigger_action_t;
```

## API Functions

### State Management

```c
void lle_notification_init(lle_notification_state_t *state);
void lle_notification_cleanup(lle_notification_state_t *state);
```

### Show/Dismiss

```c
lle_result_t lle_notification_show(lle_notification_state_t *state,
                                   const char *message,
                                   lle_notification_type_t type);

lle_result_t lle_notification_show_with_trigger(lle_notification_state_t *state,
                                                const char *message,
                                                lle_notification_type_t type,
                                                lle_notification_trigger_action_t trigger);

void lle_notification_dismiss(lle_notification_state_t *state);
```

### State Queries

```c
bool lle_notification_is_visible(const lle_notification_state_t *state);

lle_notification_trigger_action_t
lle_notification_get_trigger_action(const lle_notification_state_t *state);

bool lle_notification_should_dismiss_for_action(
    const lle_notification_state_t *state,
    lle_notification_trigger_action_t action);
```

### Styled Output

```c
const char *lle_notification_get_styled_text(const lle_notification_state_t *state,
                                             char *buf, size_t size);
```

Returns ANSI-styled text with format: `"Tip: <message>"` for HINT type, just `"<message>"` for others.

## Display Controller Integration

### Notification Storage

The display controller stores a **copy** of the notification (not a pointer) because the source may be on the stack:

```c
/* In display_controller_t */
lle_notification_state_t notification_copy;  /* Inline copy */
bool notification_visible;
bool notification_state_changed;  /* For change detection */
```

### Display Controller API

```c
display_controller_error_t display_controller_set_notification(
    display_controller_t *controller,
    lle_notification_state_t *notification);

display_controller_error_t display_controller_clear_notification(
    display_controller_t *controller);

bool display_controller_has_notification(const display_controller_t *controller);

bool display_controller_check_and_clear_notification_changed(
    display_controller_t *controller);
```

### Render Cycle Integration

In `dc_handle_redraw_needed()`:

1. **Notification text generation** (after menu preparation):
   ```c
   if (controller->notification_visible && controller->notification_copy.visible) {
       notification_text = lle_notification_get_styled_text(
           &controller->notification_copy, notification_buffer,
           sizeof(notification_buffer));

       /* Add to screen_buffer like menu */
       if (notification_text && *notification_text) {
           int notif_start_row = desired_screen.num_rows;
           notification_rows_added = screen_buffer_add_text_rows(
               &desired_screen, notif_start_row, notification_text);
       }
   }
   ```

2. **Terminal write** (Step 4c):
   ```c
   if (notification_text && *notification_text) {
       write(STDOUT_FILENO, "\n", 1);
       write(STDOUT_FILENO, notification_text, strlen(notification_text));
   }
   ```

3. **Cursor positioning** (Step 5):
   - `screen_buffer_get_rows_below_cursor()` automatically includes notification rows
   - No separate calculation needed (unlike before the fix)

## Command Layer Change Detection

The `command_layer_set_command()` function checks for notification changes to trigger redraws even when command text and cursor haven't changed:

```c
bool notification_changed = false;
display_controller_t *dc = display_integration_get_controller();
if (dc) {
    menu_changed = display_controller_check_and_clear_menu_changed(dc);
    notification_changed = display_controller_check_and_clear_notification_changed(dc);
}

if (!command_changed && !cursor_changed && !is_first_render &&
    !menu_changed && !notification_changed) {
    return COMMAND_LAYER_SUCCESS;  /* Skip redraw */
}
```

This is critical because when the user presses UP at a boundary:
- Command text doesn't change
- Cursor position doesn't change
- But notification state DOES change

Without this check, the redraw would be skipped and the notification wouldn't appear.

## Trigger and Dismissal Logic

### Triggering Notifications

In `lle_smart_up_arrow_context()` and `lle_smart_down_arrow_context()`:

```c
if (is_multiline) {
    lle_line_nav_result_t nav = lle_previous_line_ex(editor);

    if (nav.hit_boundary) {
        show_multiline_history_hint(ctx, true);  /* Show notification */
    } else {
        maybe_dismiss_notification(ctx, LLE_NOTIF_ACTION_UP_ARROW);
    }

    if (nav.result == LLE_SUCCESS) {
        refresh_display(ctx);
    }
}
```

The notification appears when the user **tries to go beyond** the boundary, not when they first arrive at it. This requires two UP presses:
1. First UP: Moves cursor to first line (successful move)
2. Second UP: Already on first line, hits boundary, notification appears

### Suppress-on-Repeat

If the same action (e.g., UP arrow) is repeated while the notification is visible, the notification stays visible instead of being dismissed and re-shown:

```c
bool lle_notification_should_dismiss_for_action(
    const lle_notification_state_t *state,
    lle_notification_trigger_action_t action) {

    if (!state || !state->visible) {
        return false;
    }

    /* Same action repeating - don't dismiss */
    if (action != LLE_NOTIF_ACTION_NONE &&
        action == state->trigger_action) {
        return false;
    }

    return true;  /* Different action - dismiss */
}
```

### Dismissal Triggers

Notifications are dismissed by:

| Action | Handler | Notes |
|--------|---------|-------|
| Character input | `handle_character_input()` | Any typed character |
| ESC key | `lle_escape_context()` | Tier 0 dismissal |
| Ctrl-G | `lle_abort_line_context()` | Tier 0 dismissal |
| Successful navigation | `maybe_dismiss_notification()` | When cursor actually moves |

### Tiered ESC/Ctrl-G Handling

Notification dismissal is **Tier 0** (highest priority):

```c
/* Tier 0: Check if notification is visible - dismiss it first */
if (lle_notification_is_visible(&ctx->notification)) {
    lle_notification_dismiss(&ctx->notification);
    display_controller_clear_notification(dc);
    refresh_display(ctx);
    return LLE_SUCCESS;  /* Stop here */
}

/* Tier 1: Completion menu... */
/* Tier 2: Autosuggestion... */
/* Tier 3: Abort line (Ctrl-G only) */
```

## Configuration

The notification only appears when `arrow_key_mode` is `LLE_ARROW_MODE_CONTEXT_AWARE` (the default Lush mode):

```c
static void show_multiline_history_hint(readline_context_t *ctx, bool is_up_arrow) {
    /* Only show hint in context-aware mode (Lush default) */
    if (config.lle_arrow_key_mode != LLE_ARROW_MODE_CONTEXT_AWARE) {
        return;
    }
    /* ... */
}
```

Users who configure other arrow modes have explicitly chosen different behavior and don't need the hint.

## Visual Styling

### ANSI Color Codes

```c
#define ANSI_RESET     "\033[0m"
#define ANSI_DIM_GRAY  "\033[90m"  /* Not used for HINT */
#define ANSI_CYAN      "\033[36m"  /* HINT and INFO */
#define ANSI_YELLOW    "\033[33m"  /* WARNING */
#define ANSI_RED       "\033[31m"  /* ERROR */
```

### Output Format

```
Tip: Use Ctrl+P/Ctrl+N for history navigation in multiline mode
^    ^
|    +-- Message text
+------- Prefix (HINT type only)
```

The entire line is colored according to notification type.

## File Locations

| File | Purpose |
|------|---------|
| `include/lle/notification.h` | Public API and type definitions |
| `src/lle/notification.c` | State management and styling implementation |
| `src/lle/lle_readline.c` | Trigger logic, dismissal, `show_multiline_history_hint()` |
| `src/display/display_controller.c` | Display integration, screen_buffer addition |
| `src/display/command_layer.c` | Change detection for redraw triggering |
| `include/display/display_controller.h` | Display controller notification fields |

## Testing

### Manual Test Procedure

1. Run `./build/lush`
2. Type `if true; then` and press Enter (enters multiline mode)
3. Press UP arrow once - cursor moves to first line (no notification)
4. Press UP arrow again - notification appears ("Tip: Use Ctrl+P/Ctrl+N...")
5. Press UP arrow again - notification stays (suppress-on-repeat)
6. Type any character - notification clears
7. Press UP at boundary again - notification reappears
8. Press ESC - notification clears
9. Press UP at boundary - notification appears
10. Press Ctrl-G - notification clears

### Verification Points

- Notification appears immediately when boundary is hit (no delay)
- Notification clears immediately on dismissal triggers
- Cursor positioning remains correct with notification visible
- Multiline continuation prompts render correctly with notification
- No display artifacts when notification appears/disappears
