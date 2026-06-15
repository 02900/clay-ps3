#ifndef CLAY_NAV_H
#define CLAY_NAV_H

/*
 * clay_nav - gamepad focus & directional navigation for Clay (immediate mode).
 *
 * Clay is layout-only and stateless: it rebuilds the element tree every frame and
 * has no built-in focus system. clay_nav adds an app-managed focus model on top:
 * you register the focusable element IDs each frame and it resolves D-pad movement
 * using each element's bounding box from the previous frame (Clay_GetElementData).
 *
 * Depends only on clay.h (no PSL1GHT / YA2D / input deps) so it stays portable.
 * Input reading and rendering remain the consuming project's responsibility.
 *
 * See docs/GAMEPAD-NAVIGATION.md for the full guidelines and patterns.
 *
 * Per-frame call order:
 *   clay_nav_move(&nav, dir);     // uses LAST frame's list + geometry
 *   clay_nav_begin(&nav);         // start this frame's registration pass
 *   Clay_BeginLayout();
 *     // for each focusable, in declaration order:
 *     clay_nav_add(&nav, id);
 *     CLAY({ .id = id, .backgroundColor = clay_nav_is_focused(&nav, id) ? FOCUS : NORMAL }) { ... }
 *   Clay_RenderCommandArray cmds = Clay_EndLayout(0.0f);
 *   clay_render(cmds);
 */

#include "clay.h"

#ifndef CLAY_NAV_MAX
#define CLAY_NAV_MAX 256   /* max focusable elements per frame */
#endif

typedef enum {
    CLAY_NAV_NONE = 0,
    CLAY_NAV_UP,
    CLAY_NAV_DOWN,
    CLAY_NAV_LEFT,
    CLAY_NAV_RIGHT
} ClayNavDir;

typedef struct {
    Clay_ElementId focused;            /* currently focused element */
    bool           has_focus;          /* false until the first element is registered */
    bool           wrap;               /* if true, moving past an edge wraps to the
                                          opposite end (default false; opt-in) */
    Clay_ElementId items[CLAY_NAV_MAX];/* focusables registered this frame */
    int            count;
} ClayNav;

/* Begin a registration pass for the current frame (clears the focusable list). */
void clay_nav_begin(ClayNav *nav);

/* Register a focusable element. Call in the same order you declare elements.
 * The first element registered while has_focus is false becomes the initial focus. */
void clay_nav_add(ClayNav *nav, Clay_ElementId id);

/* True if `id` is the currently focused element. Use inside CLAY({...}) to apply
 * a focus highlight (swap background/border color). */
bool clay_nav_is_focused(const ClayNav *nav, Clay_ElementId id);

/* Move focus in `dir`, choosing the nearest registered element in that direction
 * using each element's previous-frame bounding box (Clay_GetElementData).
 * No-op for CLAY_NAV_NONE. If no candidate exists in that direction and nav->wrap
 * is true, focus wraps to the element at the opposite edge (best for linear menus);
 * otherwise focus stays put. */
void clay_nav_move(ClayNav *nav, ClayNavDir dir);

/* Force focus to a specific element (e.g. restoring focus after a state change).
 * Pass an id you also register via clay_nav_add the same frame. */
void clay_nav_focus(ClayNav *nav, Clay_ElementId id);

/* Keep the focused element inside a scroll/clip container's viewport.
 * `viewport` is the clip container's element id; `offset` is the scroll offset you
 * own and feed to its .clip.childOffset; `margin` is px of padding to keep visible
 * around the focused element. Returns the adjusted offset. Uses previous-frame
 * geometry, so call after clay_nav_move and before building the layout. No-op (returns
 * `offset` unchanged) if the focused element or the viewport has no known box yet, so
 * only call it when the focused element actually lives inside this container. */
Clay_Vector2 clay_nav_scroll_into_view(const ClayNav *nav, Clay_ElementId viewport,
                                       Clay_Vector2 offset, float margin);

/* Auto-repeat for held directions (D-pad or analog mapped to a direction).
 * Call once per frame with the direction currently held (CLAY_NAV_NONE if none).
 * Returns the direction that should "fire" this frame: immediately on a fresh
 * press, then nothing until `initial_delay` frames, then every `repeat_rate`
 * frames while held. Frame-count based (no wall-clock). */
typedef struct {
    ClayNavDir last;
    int        timer;
} ClayNavRepeat;

ClayNavDir clay_nav_repeat(ClayNavRepeat *r, ClayNavDir held,
                           int initial_delay, int repeat_rate);

#endif /* CLAY_NAV_H */
