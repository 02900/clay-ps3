# Clay Gamepad Navigation Guidelines

Reference document for building **gamepad-navigable** UI on PS3 with
[Clay](https://github.com/nicbarker/clay) + the `clay-ps3` Tiny3D/YA2D backend.

Clay is **layout-only and immediate-mode**: it has no built-in focus, no event system,
and no retained widget tree. It rebuilds the entire layout every frame from your code and
returns a flat list of draw commands. There is no mouse on PS3 — the DualShock is the only
input — so **focus and navigation are something you build on top of Clay**, not something
it gives you.

This document defines how to do that consistently, and how to use the `clay_nav` helper
(`clay_nav.h` / `clay_nav.c`) so screens don't reinvent focus handling.

> If you've used Unity UI Toolkit (`focusable`, `tabindex`, `NavigationMoveEvent`,
> `:focus`): none of that exists here. The concepts carry over; the mechanism is manual.

---

## 1. The Immediate-Mode Focus Model

In a retained-mode UI (Unity UI Toolkit, the DOM), the framework keeps the widget tree
alive between frames and tracks which element is focused. In Clay, **nothing persists**:

```
Every frame you:
  1. Read the gamepad.
  2. Update YOUR focus state (which element id is focused).
  3. Rebuild the whole layout, styling the focused element differently.
  4. Render the draw commands.
```

So **focus is application state that you own** — a single "currently focused element id"
that survives across frames in your screen's variables (or inside a `ClayNav`).

### The per-frame loop

```c
// Persisted across frames (static / screen state):
static ClayNav nav;
static ClayNavRepeat rep;

while (running) {
    // 1. Read input -> a direction held this frame
    ClayNavDir held = read_dpad_direction();          // your code (see §6)
    ClayNavDir dir  = clay_nav_repeat(&rep, held, 18, 6);

    // 2. Move focus using LAST frame's geometry, then start a fresh registration
    clay_nav_move(&nav, dir);
    clay_nav_begin(&nav);

    // 3. Build the layout; register + highlight focusables
    Clay_SetLayoutDimensions((Clay_Dimensions){ SCREEN_WIDTH, SCREEN_HEIGHT });
    Clay_BeginLayout();
    /* ... declare UI, calling clay_nav_add()/clay_nav_is_focused() ... */
    Clay_RenderCommandArray cmds = Clay_EndLayout(0.0f);

    // 4. Frame + render
    tiny3d_Clear(0xff101010, TINY3D_CLEAR_ALL);
    /* blend setup ... */
    tiny3d_Project2D();
    reset_ttf_frame();
    clay_render(cmds);
    tiny3d_Flip();
    sysUtilCheckCallback();
}
```

### Why "previous frame" geometry is fine

`clay_nav_move()` resolves directional movement by reading each focusable's bounding box
via `Clay_GetElementData(id)`, which returns geometry from the **last** completed layout.
At 60 fps that's ~16 ms stale — imperceptible. The only consequence: the very first frame
an element exists, its box isn't known yet (`found == false`); `clay_nav` skips such
candidates safely, and they become navigable on the next frame.

### Retained vs immediate — concept mapping

| Unity UI Toolkit (retained) | Clay (immediate) |
|---|---|
| `focusable = true` | Register the id with `clay_nav_add()` each frame |
| `tabindex` order | Registration order + geometry-based `clay_nav_move()` |
| `NavigationMoveEvent` (grid handler) | `clay_nav_move()` (spatial, any layout) |
| `element.Focus()` | `clay_nav_focus(&nav, id)` |
| `:focus` USS style | `clay_nav_is_focused()` ? focus color : normal, in the declaration |
| `NavigationSubmitEvent` / `NavigationCancelEvent` | Read Cross / Circle yourself (§7) |
| `ScrollView.ScrollTo()` | App-managed scroll offset + `.clip.childOffset` (§10) |

---

## 2. Core Principles

1. **Focus is king.** If an element can be activated, it must be focusable (registered) and
   reachable with the D-pad. There is no fallback input — unreachable means unusable.
2. **Never break the focus chain.** After *any* change to the UI (item added/removed, tab
   switched, list filtered, reflow), focus must still point at a visible element — never at
   nothing and never at a vanished id.
3. **Visual feedback is mandatory.** The focused element must be visually distinct *every
   frame*. The player should never wonder "where am I?".
4. **One focus owner per layer.** Exactly one element is focused at a time. When a modal is
   open, it owns focus completely and the background must not respond (§11).
5. **No hidden actions.** Every action must be reachable through focus + a documented button.
   If something is only triggerable by an off-screen or unfocusable element, it's a bug.

---

## 3. The `clay_nav` Helper

`clay_nav` is a tiny, malloc-free focus controller that depends only on `clay.h`. It does
**not** read input or render — your project keeps doing that. It tracks the focused id,
resolves directional movement from geometry, and tells you which element to highlight.

### API

```c
typedef enum { CLAY_NAV_NONE, CLAY_NAV_UP, CLAY_NAV_DOWN, CLAY_NAV_LEFT, CLAY_NAV_RIGHT } ClayNavDir;

typedef struct {
    Clay_ElementId focused;
    bool           has_focus;
    Clay_ElementId items[CLAY_NAV_MAX];   // CLAY_NAV_MAX defaults to 256
    int            count;
} ClayNav;

void       clay_nav_begin(ClayNav *nav);                          // clear this frame's list
void       clay_nav_add(ClayNav *nav, Clay_ElementId id);         // register a focusable (in order)
bool       clay_nav_is_focused(const ClayNav *nav, Clay_ElementId id);
void       clay_nav_move(ClayNav *nav, ClayNavDir dir);           // resolve nearest in direction
void       clay_nav_focus(ClayNav *nav, Clay_ElementId id);       // force focus (restore/clamp)

typedef struct { ClayNavDir last; int timer; } ClayNavRepeat;
ClayNavDir clay_nav_repeat(ClayNavRepeat *r, ClayNavDir held, int initial_delay, int repeat_rate);
```

### Call order (must be respected)

```
clay_nav_move(&nav, dir)   ──> uses the list + geometry from the PREVIOUS frame
clay_nav_begin(&nav)       ──> clears the list for THIS frame
  clay_nav_add(&nav, id)   ──> called once per focusable, in declaration order
  clay_nav_is_focused(...) ──> called inside CLAY({...}) to pick the highlight color
```

`clay_nav_move` runs **before** `clay_nav_begin` on purpose: it operates on the list you
built last frame (whose geometry Clay now knows). Then `clay_nav_begin` resets and you
re-register for this frame.

### How resolution works

From the focused element's center, `clay_nav_move` scans every registered candidate that
lies in the pressed direction (correct half-plane) and scores it as
`primary_axis_distance + CLAY_NAV_PERP_WEIGHT * perpendicular_offset`, picking the minimum.
This means a single function handles **rows, columns, grids, and irregular layouts** — no
manual column-count math. Tune `CLAY_NAV_PERP_WEIGHT` (default `2.0`) in `clay_nav.c`:
higher values prefer candidates directly in line with the current element.

---

## 4. Making Elements Focusable

### Give every focusable a stable ID

Focus is tracked by `Clay_ElementId` (a hash of the id string). The id **must be stable
across frames** or focus will jump. Use string-literal ids for static elements and indexed
ids for items in a loop:

```c
CLAY_ID("btn-search")          // static element
CLAY_IDI("card", i)            // i-th element of a repeated list (stable per index)
```

> Never build a focus id from frame-varying data (e.g. a pointer or a random value). Index
> or a stable key only.

### Register + highlight in the declaration

For each focusable, register it and pick its colors based on focus:

```c
Clay_ElementId id = CLAY_IDI("card", i);
clay_nav_add(&nav, id);

bool focused = clay_nav_is_focused(&nav, id);
CLAY(id, {
    .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) },
                .padding = CLAY_PADDING_ALL(10),
                .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
    .backgroundColor = focused ? UI_CARD_FOCUS : UI_CARD,
    .border = { .color = UI_FOCUS, .width = focused ? CLAY_BORDER_OUTSIDE(3)
                                                    : CLAY_BORDER_OUTSIDE(0) },
}) {
    CLAY_TEXT(clay_str(label[i]), CLAY_TEXT_CONFIG({ .textColor = UI_TEXT, .fontSize = 20 }));
}
```

> The `.id` you pass to `CLAY()` and the id you `clay_nav_add()` **must match**, otherwise
> `Clay_GetElementData()` can't find the box and movement won't work.

---

## 5. Focus Order & Regions

There is no `tabindex`. The implicit order is **registration order**, and directional
movement is **geometric**. To get predictable navigation:

- **Register in reading order**, region by region (e.g. sidebar rows first, then content
  cards). This makes the initial focus and any linear "next/prev" fallback sensible.
- **Group visually = group logically.** Because movement is geometric, elements that look
  like they're in a column will navigate as a column. Lay out regions so up/down/left/right
  map to what the player sees.
- **Crossing regions is automatic.** Pressing Right from the last sidebar row lands on the
  nearest content card — no special "switch region" code, as long as both were registered.

Tier idea (analogous to numeric tabindex ranges) — keep a consistent registration order:

| Order | Region |
|---|---|
| 1st | Top bar / primary controls |
| 2nd | Left sidebar / navigation rows |
| 3rd | Main content (cards/list), in row-major order |
| 4th | Footer / action buttons |

---

## 6. Directional Navigation

### D-pad → direction

Map the D-pad to a single `ClayNavDir` for the frame (diagonals: pick one, or none):

```c
static ClayNavDir read_dpad_direction(const padData *p) {
    if (p->BTN_UP)    return CLAY_NAV_UP;
    if (p->BTN_DOWN)  return CLAY_NAV_DOWN;
    if (p->BTN_LEFT)  return CLAY_NAV_LEFT;
    if (p->BTN_RIGHT) return CLAY_NAV_RIGHT;
    return CLAY_NAV_NONE;
}
```

### Analog stick → direction (with deadzone)

The left stick should navigate too. PSL1GHT reports axes 0–255, centered at 128:

```c
static ClayNavDir read_stick_direction(const padData *p) {
    int dx = (int)p->ANA_L_H - 128;   // -128..127
    int dy = (int)p->ANA_L_V - 128;
    const int DEAD = 48;              // ignore small tilts
    if (dx*dx + dy*dy < DEAD*DEAD) return CLAY_NAV_NONE;
    if (abs(dx) > abs(dy)) return dx > 0 ? CLAY_NAV_RIGHT : CLAY_NAV_LEFT;
    return dy > 0 ? CLAY_NAV_DOWN : CLAY_NAV_UP;
}
```

Combine D-pad and stick: `held = dpad != NONE ? dpad : stick;`

### Auto-repeat (the part Unity gave you for free)

A held direction must fire once, pause, then repeat at a steady rate — otherwise focus
either jumps one step per press (tedious) or flies across the screen (one step per frame =
60/s). `clay_nav_repeat` handles this with frame counts:

```c
ClayNavDir dir = clay_nav_repeat(&rep, held, /*initial_delay*/ 18, /*repeat_rate*/ 6);
clay_nav_move(&nav, dir);
```

`initial_delay` = frames to wait before the first repeat (~0.3 s @ 60 fps); `repeat_rate` =
frames between repeats (~10/s). A fresh press always fires immediately.

---

## 7. Submit & Cancel — DualShock 3 Mapping

Clay does not fire submit/cancel events; you read the buttons yourself. Use **edge
detection** (fire on press, not while held) for actions:

```c
// Per frame:
u32 cur = (p.BTN_CROSS)      | (p.BTN_CIRCLE   << 1) |
          (p.BTN_TRIANGLE<<2) | (p.BTN_SQUARE  << 3) |
          (p.BTN_L1     << 4) | (p.BTN_R1      << 5);
u32 pressed = cur & ~prev;   // rising edge
prev = cur;

if (pressed & (1 << 0)) activate(nav.focused);   // Cross  = submit
if (pressed & (1 << 1)) go_back();                // Circle = cancel
```

Standard mapping (keep it consistent across screens):

| Button | Role | Notes |
|---|---|---|
| **Cross (✕)** | Submit / activate the focused element | The primary "OK" |
| **Circle (○)** | Cancel / back / close | Never destructive without confirm |
| **Triangle (△)** | Detail / preview / info | Optional, context-dependent |
| **Square (□)** | Secondary action / context | Optional |
| **L1 / R1** | Previous / next tab or page | Section/zone switching |
| **L2 / R2** | Fast scroll / jump | Optional |
| **Start** | Open menu / confirm-exit | |
| **Select** | Toggle help / debug | Optional |
| **D-pad / Left stick** | Move focus (§6) | Never a game action while a UI is up |

> "Activate" means: run the focused element's action. Because the layout is rebuilt each
> frame, you map the focused **id** to an action — e.g. a `switch` on a screen-local enum
> stored alongside the registration, or compare `nav.focused.id` against known ids.

---

## 8. Focus After State Changes

Whenever the set of focusables changes, **fix focus explicitly** in the same frame. The
focused id is just data — set it with `clay_nav_focus()` (or let the next `clay_nav_move`
fall back to `items[0]` if the old id vanished).

| Scenario | Focus target |
|---|---|
| List populated | First item |
| List became empty | A still-present fallback (e.g. a header button) |
| Item added | The newly added item |
| Item removed | Next item, or previous if it was last |
| Tab / zone switched | First item of the new zone (or the tab itself if empty) |
| Reflow (count/columns changed) | Keep the same id if still present; else clamp to range |
| Filter cleared | The search/filter control |
| Modal opened | First interactive element inside the modal (§11) |
| Modal closed | The element that opened it (restore saved focus) |

### Clamp pattern (e.g. when a count shrinks)

```c
// num_items just dropped; if focus pointed past the end, pull it back.
if (focused_index >= num_items) focused_index = num_items - 1;
if (focused_index < 0)          focused_index = 0;
clay_nav_focus(&nav, CLAY_IDI("card", focused_index));
```

### Store / restore pattern

```c
Clay_ElementId saved = nav.focused;   // before opening a sub-view
// ... open, navigate the sub-view ...
clay_nav_focus(&nav, saved);          // on returning
```

---

## 9. Visual Feedback (Focus Styling)

The focus highlight is just conditional styling in the declaration — there is no `:focus`.
Make it unmistakable and make **focus** distinct from **selected/active state** so the
player can tell "where I am" from "what is chosen".

```c
bool focused  = clay_nav_is_focused(&nav, id);
bool selected = (id.id == current_selection.id);

CLAY(id, {
    .backgroundColor = selected ? UI_CARD_SELECTED : UI_CARD,
    .border = { .color = focused ? UI_FOCUS : UI_BORDER,
                .width = CLAY_BORDER_OUTSIDE(focused ? 3 : 1) },
}) { /* ... */ }
```

Rules:
- Every focusable has a visible focus treatment **every frame** (border thickness + a focus
  color is the simplest reliable choice — borders render as fill-rects in the backend).
- Ensure contrast against the background; the focus color should not clash with the
  selected/active color.
- Corner radius is ignored by the backend (square corners) — don't rely on rounding to
  convey focus.

---

## 10. Scrolling & Bring-Focus-Into-View

When content is taller/wider than its container, use a Clay clip (scroll) container and
offset its children. On PS3 there's no scroll wheel, so **you manage the scroll offset as
app state** and move it when focus leaves the viewport.

```c
static float scroll_y = 0.0f;   // app state

CLAY(CLAY_ID("list"), {
    .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8 },
    .clip = { .vertical = true, .childOffset = (Clay_Vector2){ 0, scroll_y } },
}) { /* items ... */ }
```

After moving focus, nudge `scroll_y` so the focused box sits inside the list box (both come
from `Clay_GetElementData`, in absolute coordinates):

```c
Clay_ElementData list = Clay_GetElementData(CLAY_ID("list"));
Clay_ElementData foc  = Clay_GetElementData(nav.focused);
if (list.found && foc.found) {
    float top    = foc.boundingBox.y;
    float bottom = top + foc.boundingBox.height;
    float vtop    = list.boundingBox.y;
    float vbottom = vtop + list.boundingBox.height;
    if (top    < vtop)    scroll_y += (vtop - top);          // scrolled above -> reveal
    if (bottom > vbottom) scroll_y -= (bottom - vbottom);    // scrolled below -> reveal
}
```

> `childOffset` is normally fed by `Clay_GetScrollOffset()` (pointer/drag driven). For
> gamepad UIs, drive it yourself as above; don't call `Clay_UpdateScrollContainers()`.

---

## 11. Modal Dialogs (Focus Trapping)

A modal (confirm, alert, detail) must **own focus completely** while open. In immediate
mode this is clean because you control the whole frame:

1. **Separate `ClayNav` for the modal.** Keep `ClayNav modal_nav;` and, while the modal is
   open, register only the modal's buttons into it and run `clay_nav_move`/activation
   against `modal_nav`. The background nav is simply not driven — it can't move.
2. **Draw the modal last / on top.** Declare it as a floating element (or just after the
   rest of the tree) so it renders over the content. Add a full-screen dim rectangle behind
   it for clarity.
3. **Store/restore background focus.** Save `nav.focused` when opening; restore it when the
   modal closes.
4. **Cancel closes it.** Circle hides the modal and restores focus.

```c
if (modal_open) {
    clay_nav_move(&modal_nav, dir);
    clay_nav_begin(&modal_nav);
} else {
    clay_nav_move(&nav, dir);
    clay_nav_begin(&nav);
}

Clay_BeginLayout();
  build_screen(&nav);                 // background (focus frozen while modal is up)
  if (modal_open) build_modal(&modal_nav);
Clay_RenderCommandArray cmds = Clay_EndLayout(0.0f);

// input
if (modal_open) {
    if (pressed_cross)  modal_confirm();
    if (pressed_circle) { modal_open = false; clay_nav_focus(&nav, saved_focus); }
} else {
    if (pressed_cross)  activate(nav.focused);
    if (pressed_circle) go_back();
}
```

Routing input to one nav at a time is the whole trap — the background never moves and never
activates while the modal is up.

---

## 12. Design Tokens

Keep colors and spacing in one place; don't scatter literals. Colors are `0xRRGGBBAA` for
the YA2D backend, but Clay declarations take `Clay_Color { float r,g,b,a; }` (0–255). Define
a small palette as `Clay_Color` constants:

```c
#define RGBA(r,g,b,a) ((Clay_Color){ (r), (g), (b), (a) })

static const Clay_Color UI_BG            = RGBA( 16, 24, 40,255);
static const Clay_Color UI_PANEL         = RGBA( 30, 44, 60,255);
static const Clay_Color UI_CARD          = RGBA( 58,102,153,255);
static const Clay_Color UI_CARD_FOCUS    = RGBA( 78,132,193,255);  // brighter when focused
static const Clay_Color UI_CARD_SELECTED = RGBA(120, 90, 40,255);  // distinct from focus
static const Clay_Color UI_BORDER        = RGBA( 64, 80,104,255);
static const Clay_Color UI_FOCUS         = RGBA(255,200, 50,255);  // focus ring (gold)
static const Clay_Color UI_TEXT          = RGBA(255,255,255,255);
static const Clay_Color UI_TEXT_DIM      = RGBA(180,180,180,255);
```

| Token | Usage |
|---|---|
| `UI_BG` | Root background |
| `UI_PANEL` | Sidebars / panels |
| `UI_CARD` / `UI_CARD_FOCUS` | Item, and its focused variant |
| `UI_CARD_SELECTED` | Chosen item (≠ focus) |
| `UI_FOCUS` | Focus ring/border color |
| `UI_TEXT` / `UI_TEXT_DIM` | Primary / secondary text |

Spacing: pick a scale and reuse it (e.g. `4 / 8 / 16` px for tight / standard / section
gaps via `.childGap` and `CLAY_PADDING_ALL`).

---

## 13. Per-Screen Navigation Checklist

- [ ] Every interactive element is registered with `clay_nav_add()` every frame.
- [ ] Each focusable's `CLAY()` `.id` matches the id passed to `clay_nav_add()`.
- [ ] Focus ids are stable (`CLAY_ID` / `CLAY_IDI(i)`), never frame-varying.
- [ ] The focused element is visually distinct **every frame** (§9).
- [ ] D-pad reaches every focusable, including across regions (try all 4 directions).
- [ ] Left stick navigates too, with a deadzone (§6).
- [ ] Held direction auto-repeats via `clay_nav_repeat()` (not 1/frame, not 1/press).
- [ ] Cross activates the focused element; Circle goes back/cancels.
- [ ] Focus is fixed after **every** state change (§8) — never lands on nothing.
- [ ] On reflow/count change, focus is clamped or preserved by id.
- [ ] Focused element scrolls into view in clipped containers (§10).
- [ ] Modals route input to their own nav and restore focus on close (§11).
- [ ] Gamepad-only pass: every action is reachable without any other input.

---

## 14. Reference Implementation

`ps3-homebrew-template` → `source/main.c`, function `demo_clay_ui()` is a working screen
built with `clay_nav`: a focusable sidebar + a grid of cards, D-pad/stick focus movement
with auto-repeat, a focus highlight, Cross to activate, Circle to exit, and L1/R1 reflow
that keeps focus valid. Read it alongside this document.

---

## 15. Minimal End-to-End Example

A complete, compact focusable screen (omitting the surrounding app/init):

```c
#include "clay.h"
#include "clay_renderer.h"
#include "clay_nav.h"

#define RGBA(r,g,b,a) ((Clay_Color){ (r),(g),(b),(a) })
static const Clay_Color BG = RGBA(16,24,40,255), CARD = RGBA(58,102,153,255),
                        CARDF = RGBA(78,132,193,255), FOCUS = RGBA(255,200,50,255),
                        TEXT = RGBA(255,255,255,255);

static const char *ITEMS[] = { "Play", "Options", "Credits", "Quit" };
#define N 4

static Clay_String cstr(const char *s){ return (Clay_String){ false, (int)strlen(s), s }; }

void menu_screen(void) {
    static ClayNav nav = {0};
    static ClayNavRepeat rep = {0};
    static u32 prev = 0;

    int chosen = -1;
    while (chosen < 0) {
        ioPadGetData(0, &pad_data);

        ClayNavDir held = pad_data.BTN_UP   ? CLAY_NAV_UP   :
                          pad_data.BTN_DOWN ? CLAY_NAV_DOWN : CLAY_NAV_NONE;
        clay_nav_move(&nav, clay_nav_repeat(&rep, held, 18, 6));
        clay_nav_begin(&nav);

        Clay_SetLayoutDimensions((Clay_Dimensions){ SCREEN_WIDTH, SCREEN_HEIGHT });
        Clay_BeginLayout();
        CLAY(CLAY_ID("root"), {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                        .padding = CLAY_PADDING_ALL(24), .childGap = 8,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM },
            .backgroundColor = BG,
        }) {
            for (int i = 0; i < N; i++) {
                Clay_ElementId id = CLAY_IDI("item", i);
                clay_nav_add(&nav, id);
                bool f = clay_nav_is_focused(&nav, id);
                CLAY(id, {
                    .layout = { .sizing = { CLAY_SIZING_FIXED(280), CLAY_SIZING_FIXED(48) },
                                .padding = CLAY_PADDING_ALL(12) },
                    .backgroundColor = f ? CARDF : CARD,
                    .border = { .color = FOCUS, .width = CLAY_BORDER_OUTSIDE(f ? 3 : 0) },
                }) {
                    CLAY_TEXT(cstr(ITEMS[i]), CLAY_TEXT_CONFIG({ .textColor = TEXT, .fontSize = 22 }));
                }
            }
        }
        Clay_RenderCommandArray cmds = Clay_EndLayout(0.0f);

        u32 cur = pad_data.BTN_CROSS | (pad_data.BTN_CIRCLE << 1);
        u32 pressed = cur & ~prev; prev = cur;
        if (pressed & 1) {                      // Cross: activate
            for (int i = 0; i < N; i++)
                if (clay_nav_is_focused(&nav, CLAY_IDI("item", i))) chosen = i;
        }
        if (pressed & 2) chosen = N - 1;        // Circle: treat as Quit

        tiny3d_Clear(0xff101820, TINY3D_CLEAR_ALL);
        /* blend setup ... */
        tiny3d_Project2D();
        reset_ttf_frame();
        clay_render(cmds);
        tiny3d_Flip();
        sysUtilCheckCallback();
    }
    /* handle `chosen` ... */
}
```
