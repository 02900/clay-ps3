#ifndef CLAY_RENDERER_H
#define CLAY_RENDERER_H

/*
 * Clay -> Tiny3D/YA2D render backend (layout-only).
 *
 * Clay (https://github.com/nicbarker/clay) is a layout-only library: it emits a
 * list of Clay_RenderCommand (rectangles, borders, text, images) that this
 * backend draws using YA2D primitives and the project's TTF renderer.
 */

#include "clay.h"

/*
 * Wrapper passed as a Clay image element's imageData. We keep the source pixel
 * size here so the renderer can compute the YA2D draw scale without having to
 * dereference the ya2d_Texture struct (whose layout lives in the toolchain).
 */
typedef struct {
    void  *tex;     /* ya2d_Texture* */
    float  src_w;   /* native texture width  in px */
    float  src_h;   /* native texture height in px */
} clay_image_t;

/* Create Clay's arena and register the TTF-based text measurement callback. */
void clay_backend_init(int screen_w, int screen_h);

/*
 * Draw a Clay command list with YA2D + the TTF renderer. Must be called inside a
 * frame, after tiny3d_Project2D() and reset_ttf_frame().
 */
void clay_render(Clay_RenderCommandArray commands);

#endif /* CLAY_RENDERER_H */
