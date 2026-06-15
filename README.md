# clay-ps3

[Clay](https://github.com/nicbarker/clay) layout library + a **Tiny3D/YA2D render
backend** for PS3 homebrew (PSL1GHT). Designed to be consumed as a **git submodule**
so multiple projects share one source of truth.

Contents:

| File              | Origin     | Purpose                                               |
|-------------------|------------|-------------------------------------------------------|
| `clay.h`          | upstream   | Vendored single-header Clay (do not edit; re-vendor). |
| `clay_renderer.h` | this repo  | Backend public API.                                   |
| `clay_renderer.c` | this repo  | Maps Clay render commands to YA2D + the TTF renderer. |
| `clay_nav.h`      | this repo  | Gamepad focus & directional-navigation helper API.    |
| `clay_nav.c`      | this repo  | Focus model on top of Clay (no input/render deps).    |

This is **layout-only**: Clay computes positions/sizes; you navigate with the gamepad.
The backend draws RECTANGLE / BORDER / TEXT / IMAGE commands and honors SCISSOR_START/END
(clip/scroll containers are clipped via the RSX scissor). OVERLAY / CUSTOM are no-ops.

## Gamepad navigation

Clay has no built-in focus system. The `clay_nav` helper adds an app-managed focus model
(register focusables per frame, move with the D-pad/stick via element geometry, highlight
the focused one). See **[docs/GAMEPAD-NAVIGATION.md](docs/GAMEPAD-NAVIGATION.md)** for the
full guidelines, patterns, and a minimal end-to-end example. `clay_nav` depends only on
`clay.h` — no PSL1GHT/YA2D — so input and rendering stay in your project.

## Requirements in the consuming project

The backend depends on symbols the host project must already provide:

- **YA2D** (`<ya2d/ya2d.h>`, `-lya2d`): `ya2d_drawFillRectZ`, `ya2d_drawTextureZ`.
- **TTF renderer** (`ttf_render.h`): `display_ttf_string` (with `color == 0` it measures
  text without drawing). Both PS3 boilerplate and ps3-remote-play ship this.
- Compiler in **C99+** mode (`-std=gnu99`) — Clay needs designated initializers,
  packed enums and `bool`.

## Adding it to a project (git submodule)

```sh
# from the consuming repo root (both projects live under github.com/02900,
# so the relative URL resolves to github.com/02900/clay-ps3.git)
git submodule add ../clay-ps3.git extern/clay-ps3
git commit -m "Add clay-ps3 submodule"
```

Then wire the Makefile so the renderer is compiled and the headers are on the include
path. Add the submodule dir to both `SOURCES` and `INCLUDES`, ensure `-std=gnu99`, and
make sure `clay_renderer.c` is in the build:

```make
SOURCES   := source extern/clay-ps3
INCLUDES  := include extern/clay-ps3      # or: source extern/clay-ps3
CFLAGS    += -std=gnu99
# wildcard Makefiles pick up clay_renderer.c automatically once SOURCES includes the dir.
# explicit-list Makefiles: add clay_renderer.c to your source file list.
```

## Using it (sketch)

```c
#include "clay.h"
#include "clay_renderer.h"

clay_backend_init(SCREEN_WIDTH, SCREEN_HEIGHT);   // once, after ya2d/ttf init

// per frame:
Clay_SetLayoutDimensions((Clay_Dimensions){ SCREEN_WIDTH, SCREEN_HEIGHT });
Clay_BeginLayout();
//   ... declare UI with CLAY({...}) / CLAY_TEXT(...) ...
Clay_RenderCommandArray cmds = Clay_EndLayout(0.0f);

tiny3d_Project2D();
reset_ttf_frame();
clay_render(cmds);
tiny3d_Flip();
```

For IMAGE elements, pass a `clay_image_t { void *tex; float src_w, src_h; }` as the
element's `imageData` (the `tex` is a `ya2d_Texture*`), so the backend can compute the
draw scale without depending on YA2D's internal struct layout.

## Re-vendoring Clay

`clay.h` is a copy of upstream. To update:

```sh
curl -fsSL https://raw.githubusercontent.com/nicbarker/clay/<tag>/clay.h -o clay.h
```

Then verify the render-command struct field names still match `clay_renderer.c`
(`renderData.rectangle/text/border/image`, `boundingBox`) and that
`Clay_EndLayout`'s signature is unchanged.
