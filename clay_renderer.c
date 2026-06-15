/*
 * Clay -> Tiny3D/YA2D render backend (layout-only).
 *
 * This translation unit owns the single CLAY_IMPLEMENTATION. It maps Clay's
 * render commands to YA2D fill-rects and the TTF string renderer, and provides
 * a text-measurement callback so Clay can size text elements.
 *
 * Text sizing: display_ttf_string() takes per-character width/height (sw/sh) and,
 * when called with color 0, computes the string width WITHOUT drawing. Both the
 * measurement callback and the TEXT render path go through the same clay_font_dims()
 * mapping, so measured and rendered widths always agree.
 */

#include <string.h>
#include <stdlib.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <ya2d/ya2d.h>
#include "ttf_render.h"

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "clay_renderer.h"

/* Clay layout space (set in clay_backend_init) -> physical framebuffer scaling.
 * rsxSetScissor takes physical pixels, but Clay boxes are in the 2D layout space
 * mapped to the screen by tiny3d_Project2D(). */
static float clay_layout_w = 848.0f;
static float clay_layout_h = 512.0f;

/* Scissor (clip) stack for nested SCISSOR_START/END. Rects are physical pixels. */
typedef struct { u16 x, y, w, h; } clay_scissor_t;
static clay_scissor_t scissor_stack[8];
static int scissor_depth = 0;

static u16 clay_clampu16(float v)
{
    if (v < 0.0f) return 0;
    if (v > 65535.0f) return 65535;
    return (u16)v;
}

static void clay_scissor_apply(clay_scissor_t s)
{
    gcmContextData *ctx = (gcmContextData *)tiny3d_Get_GCM_Context();
    rsxSetScissor(ctx, s.x, s.y, s.w, s.h);
}

static clay_scissor_t clay_scissor_full(void)
{
    clay_scissor_t s = { 0, 0, (u16)Video_Resolution.width, (u16)Video_Resolution.height };
    return s;
}

/* Map a Clay fontSize (px height) to the TTF renderer's sw/sh character size. */
static void clay_font_dims(uint16_t font_size, int *sw, int *sh)
{
    *sh = font_size;
    *sw = (font_size * 7) / 10;   /* ~0.7 aspect, matches the rest of the UI */
}

/* Clay colors are float 0-255 RGBA; project colors are packed 0xRRGGBBAA. */
static u32 clay_color_to_rgba(Clay_Color c)
{
    u32 r = (u32)(c.r < 0.0f ? 0.0f : (c.r > 255.0f ? 255.0f : c.r));
    u32 g = (u32)(c.g < 0.0f ? 0.0f : (c.g > 255.0f ? 255.0f : c.g));
    u32 b = (u32)(c.b < 0.0f ? 0.0f : (c.b > 255.0f ? 255.0f : c.b));
    u32 a = (u32)(c.a < 0.0f ? 0.0f : (c.a > 255.0f ? 255.0f : c.a));
    return (r << 24) | (g << 16) | (b << 8) | a;
}

/* Copy a (possibly non null-terminated) Clay slice into a C string buffer. */
static void clay_slice_to_cstr(Clay_StringSlice s, char *buf, int buf_size)
{
    int n = s.length;
    if (n > buf_size - 1) n = buf_size - 1;
    if (n > 0) memcpy(buf, s.chars, n);
    buf[n > 0 ? n : 0] = '\0';
}

static Clay_Dimensions clay_measure_text(Clay_StringSlice text,
                                         Clay_TextElementConfig *config,
                                         void *userData)
{
    char buf[256];
    int sw, sh;
    Clay_Dimensions d;

    (void)userData;
    clay_font_dims(config->fontSize, &sw, &sh);
    clay_slice_to_cstr(text, buf, sizeof(buf));

    /* color 0 => measure only, returns the rendered width in pixels */
    d.width  = (float)display_ttf_string(0, 0, buf, 0, 0, sw, sh);
    d.height = (float)sh;
    return d;
}

static void clay_error_handler(Clay_ErrorData err)
{
    (void)err;   /* layout-only demo: nothing actionable to surface on-screen */
}

void clay_backend_init(int screen_w, int screen_h)
{
    uint32_t mem_size = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(mem_size, malloc(mem_size));
    Clay_Dimensions dims = { (float)screen_w, (float)screen_h };
    Clay_ErrorHandler handler = { clay_error_handler, NULL };

    clay_layout_w = (float)screen_w;
    clay_layout_h = (float)screen_h;

    Clay_Initialize(arena, dims, handler);
    Clay_SetMeasureTextFunction(clay_measure_text, NULL);
}

void clay_render(Clay_RenderCommandArray commands)
{
    float sx = (float)Video_Resolution.width  / clay_layout_w;
    float sy = (float)Video_Resolution.height / clay_layout_h;

    /* Start each frame with a full-screen clip. */
    scissor_depth = 0;
    scissor_stack[0] = clay_scissor_full();
    clay_scissor_apply(scissor_stack[0]);

    for (int32_t i = 0; i < commands.length; i++) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&commands, i);
        Clay_BoundingBox bb = cmd->boundingBox;

        switch (cmd->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            u32 col = clay_color_to_rgba(cmd->renderData.rectangle.backgroundColor);
            ya2d_drawFillRectZ((int)bb.x, (int)bb.y, 0,
                               (int)bb.width, (int)bb.height, col);
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_BORDER: {
            Clay_BorderRenderData b = cmd->renderData.border;
            u32 col = clay_color_to_rgba(b.color);
            /* Borders are drawn inset, one fill-rect per side (radius ignored). */
            if (b.width.top)
                ya2d_drawFillRectZ((int)bb.x, (int)bb.y, 0,
                                   (int)bb.width, b.width.top, col);
            if (b.width.bottom)
                ya2d_drawFillRectZ((int)bb.x, (int)(bb.y + bb.height - b.width.bottom), 0,
                                   (int)bb.width, b.width.bottom, col);
            if (b.width.left)
                ya2d_drawFillRectZ((int)bb.x, (int)bb.y, 0,
                                   b.width.left, (int)bb.height, col);
            if (b.width.right)
                ya2d_drawFillRectZ((int)(bb.x + bb.width - b.width.right), (int)bb.y, 0,
                                   b.width.right, (int)bb.height, col);
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_TEXT: {
            Clay_TextRenderData t = cmd->renderData.text;
            char buf[256];
            int sw, sh;
            clay_font_dims(t.fontSize, &sw, &sh);
            clay_slice_to_cstr(t.stringContents, buf, sizeof(buf));
            display_ttf_string((int)bb.x, (int)bb.y, buf,
                               clay_color_to_rgba(t.textColor), 0, sw, sh);
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
            clay_image_t *img = (clay_image_t *)cmd->renderData.image.imageData;
            if (img && img->tex && img->src_w > 0.0f) {
                float scale = bb.width / img->src_w;   /* square icons => uniform */
                ya2d_drawTextureZ((ya2d_Texture *)img->tex,
                                  (int)bb.x, (int)bb.y, 0, scale);
            }
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
            /* Intersect the requested clip with the current one, push, apply. */
            float x0 = bb.x * sx, y0 = bb.y * sy;
            float x1 = (bb.x + bb.width) * sx, y1 = (bb.y + bb.height) * sy;
            clay_scissor_t cur = scissor_stack[scissor_depth];
            float cx0 = cur.x, cy0 = cur.y;
            float cx1 = (float)cur.x + cur.w, cy1 = (float)cur.y + cur.h;
            if (x0 < cx0) x0 = cx0;
            if (y0 < cy0) y0 = cy0;
            if (x1 > cx1) x1 = cx1;
            if (y1 > cy1) y1 = cy1;
            if (x1 < x0) x1 = x0;
            if (y1 < y0) y1 = y0;
            clay_scissor_t s;
            s.x = clay_clampu16(x0);
            s.y = clay_clampu16(y0);
            s.w = clay_clampu16(x1 - x0);
            s.h = clay_clampu16(y1 - y0);
            if (scissor_depth < 7) scissor_stack[++scissor_depth] = s;
            clay_scissor_apply(scissor_stack[scissor_depth]);
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
            if (scissor_depth > 0) scissor_depth--;
            clay_scissor_apply(scissor_stack[scissor_depth]);
            break;
        }
        default:
            /* OVERLAY / CUSTOM / NONE: not used. */
            break;
        }
    }

    /* Restore a full-screen clip so non-Clay drawing after this isn't clipped. */
    clay_scissor_apply(clay_scissor_full());
}
