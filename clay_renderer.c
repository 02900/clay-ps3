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
#include <ya2d/ya2d.h>
#include "ttf_render.h"

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "clay_renderer.h"

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

    Clay_Initialize(arena, dims, handler);
    Clay_SetMeasureTextFunction(clay_measure_text, NULL);
}

void clay_render(Clay_RenderCommandArray commands)
{
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
        default:
            /* SCISSOR / OVERLAY / CUSTOM / NONE: unused in the layout-only demo.
             * Hook clipping (gcm scissor) here if scrolling content is added. */
            break;
        }
    }
}
