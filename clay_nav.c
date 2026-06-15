/*
 * clay_nav - gamepad focus & directional navigation for Clay (immediate mode).
 * See clay_nav.h and docs/GAMEPAD-NAVIGATION.md.
 *
 * This TU uses Clay's declarations only; the CLAY_IMPLEMENTATION lives in
 * clay_renderer.c, so do NOT define CLAY_IMPLEMENTATION here.
 */

#include "clay.h"
#include "clay_nav.h"

/* How strongly to penalise perpendicular offset when picking a candidate in a
 * direction. Higher = prefer elements directly in line with the current one. */
#ifndef CLAY_NAV_PERP_WEIGHT
#define CLAY_NAV_PERP_WEIGHT 2.0f
#endif

static float clay_nav__absf(float v) { return v < 0.0f ? -v : v; }

static bool clay_nav__same(Clay_ElementId a, Clay_ElementId b) {
    return a.id == b.id;
}

static Clay_Vector2 clay_nav__center(Clay_BoundingBox b) {
    Clay_Vector2 c;
    c.x = b.x + b.width  * 0.5f;
    c.y = b.y + b.height * 0.5f;
    return c;
}

void clay_nav_begin(ClayNav *nav) {
    nav->count = 0;
}

void clay_nav_add(ClayNav *nav, Clay_ElementId id) {
    if (nav->count < CLAY_NAV_MAX) {
        nav->items[nav->count++] = id;
    }
    if (!nav->has_focus) {
        nav->focused = id;
        nav->has_focus = true;
    }
}

bool clay_nav_is_focused(const ClayNav *nav, Clay_ElementId id) {
    return nav->has_focus && clay_nav__same(nav->focused, id);
}

void clay_nav_focus(ClayNav *nav, Clay_ElementId id) {
    nav->focused = id;
    nav->has_focus = true;
}

Clay_Vector2 clay_nav_scroll_into_view(const ClayNav *nav, Clay_ElementId viewport,
                                       Clay_Vector2 offset, float margin)
{
    if (!nav->has_focus) {
        return offset;
    }

    Clay_ElementData v = Clay_GetElementData(viewport);
    Clay_ElementData f = Clay_GetElementData(nav->focused);
    if (!v.found || !f.found) {
        return offset;
    }

    /* boundingBox is the on-screen (post-offset) rect, so the delta needed to bring
     * the focused element into the viewport applies directly to childOffset. When the
     * element (plus margins) is larger than the viewport, align its leading edge instead
     * of thrashing between the two edges every frame. */
    float ftop = f.boundingBox.y - margin;
    float fbot = f.boundingBox.y + f.boundingBox.height + margin;
    float vtop = v.boundingBox.y;
    float vbot = v.boundingBox.y + v.boundingBox.height;
    if ((fbot - ftop) > (vbot - vtop)) offset.y += (vtop - ftop);
    else if (ftop < vtop)              offset.y += (vtop - ftop);
    else if (fbot > vbot)              offset.y -= (fbot - vbot);

    float fleft  = f.boundingBox.x - margin;
    float fright = f.boundingBox.x + f.boundingBox.width + margin;
    float vleft  = v.boundingBox.x;
    float vright = v.boundingBox.x + v.boundingBox.width;
    if ((fright - fleft) > (vright - vleft)) offset.x += (vleft - fleft);
    else if (fleft < vleft)                  offset.x += (vleft - fleft);
    else if (fright > vright)                offset.x -= (fright - vright);

    return offset;
}

void clay_nav_move(ClayNav *nav, ClayNavDir dir) {
    if (dir == CLAY_NAV_NONE || nav->count == 0) {
        return;
    }

    Clay_ElementData cur = Clay_GetElementData(nav->focused);
    if (!cur.found) {
        /* Focused element vanished (e.g. layout changed) - snap to the first. */
        nav->focused = nav->items[0];
        nav->has_focus = true;
        return;
    }

    Clay_Vector2 from = clay_nav__center(cur.boundingBox);

    bool found_best = false;
    float best_score = 0.0f;
    Clay_ElementId best = nav->focused;

    for (int i = 0; i < nav->count; i++) {
        Clay_ElementId cand = nav->items[i];
        if (clay_nav__same(cand, nav->focused)) {
            continue;
        }

        Clay_ElementData cd = Clay_GetElementData(cand);
        if (!cd.found) {
            continue;
        }

        Clay_Vector2 to = clay_nav__center(cd.boundingBox);
        float dx = to.x - from.x;
        float dy = to.y - from.y;

        float primary, perp;
        switch (dir) {
            case CLAY_NAV_UP:    primary = -dy; perp = clay_nav__absf(dx); break;
            case CLAY_NAV_DOWN:  primary =  dy; perp = clay_nav__absf(dx); break;
            case CLAY_NAV_LEFT:  primary = -dx; perp = clay_nav__absf(dy); break;
            case CLAY_NAV_RIGHT: primary =  dx; perp = clay_nav__absf(dy); break;
            default: continue;
        }

        /* Candidate must actually lie in the requested direction. */
        if (primary <= 0.5f) {
            continue;
        }

        float score = primary + CLAY_NAV_PERP_WEIGHT * perp;
        if (!found_best || score < best_score) {
            found_best = true;
            best_score = score;
            best = cand;
        }
    }

    /* Nothing ahead: optionally wrap to the element at the opposite edge. */
    if (!found_best && nav->wrap) {
        for (int i = 0; i < nav->count; i++) {
            Clay_ElementId cand = nav->items[i];
            if (clay_nav__same(cand, nav->focused)) {
                continue;
            }
            Clay_ElementData cd = Clay_GetElementData(cand);
            if (!cd.found) {
                continue;
            }
            Clay_Vector2 to = clay_nav__center(cd.boundingBox);

            /* Score (minimized) = distance toward the opposite edge + alignment (perp).
             * Pressing DOWN with nothing below wraps to the top-most element (min y);
             * UP wraps to the bottom-most (max y); LEFT -> right-most; RIGHT -> left-most. */
            float primary, perp;
            switch (dir) {
                case CLAY_NAV_UP:    primary = -to.y; perp = clay_nav__absf(to.x - from.x); break;
                case CLAY_NAV_DOWN:  primary =  to.y; perp = clay_nav__absf(to.x - from.x); break;
                case CLAY_NAV_LEFT:  primary = -to.x; perp = clay_nav__absf(to.y - from.y); break;
                case CLAY_NAV_RIGHT: primary =  to.x; perp = clay_nav__absf(to.y - from.y); break;
                default: continue;
            }

            float score = primary + CLAY_NAV_PERP_WEIGHT * perp;
            if (!found_best || score < best_score) {
                found_best = true;
                best_score = score;
                best = cand;
            }
        }
    }

    if (found_best) {
        nav->focused = best;
        nav->has_focus = true;
    }
}

ClayNavDir clay_nav_repeat(ClayNavRepeat *r, ClayNavDir held,
                           int initial_delay, int repeat_rate) {
    if (repeat_rate < 1) {
        repeat_rate = 1;
    }
    if (initial_delay < 0) {
        initial_delay = 0;
    }

    if (held == CLAY_NAV_NONE) {
        r->last = CLAY_NAV_NONE;
        r->timer = 0;
        return CLAY_NAV_NONE;
    }

    if (held != r->last) {
        /* Fresh press: fire immediately and start the hold timer. */
        r->last = held;
        r->timer = 0;
        return held;
    }

    /* Same direction still held. */
    r->timer++;
    if (r->timer >= initial_delay &&
        (r->timer - initial_delay) % repeat_rate == 0) {
        return held;
    }
    return CLAY_NAV_NONE;
}
