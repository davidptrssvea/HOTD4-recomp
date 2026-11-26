/*
 * hud_widen.c - a 2D layout made for a narrower screen, drawn at its own shape
 * on a wider one.
 *
 * A game whose 3D supports a wide monitor but whose 2D was only ever laid out
 * for a narrower one draws that layout stretched across the frame. Its 2D
 * arrives as immediate-mode quads with an identity modelview, so each quad is
 * held back until glEnd, measured in normalised device coordinates, and drawn
 * again through a projection that squeezes it back to the layout's own width:
 * x' = f x + s. Where it goes is decided by where it was:
 *
 *   - a quad that covers the screen (fades, flashes, frames) is left alone;
 *   - one in the middle third stays centred (s = 0);
 *   - one further out keeps to its edge (s = +-(1 - f)), as a HUD on a wide
 *     monitor would.
 *
 * A quad that continues the one before it - the next letter of a word, the
 * next piece of a gauge - goes wherever that one went, so a string is never
 * split between the middle and an edge.
 *
 * hud_widen_enable(f, anchored) turns it on: f is the layout's width as a
 * fraction of the frame's (0.75 for 4:3 in 16:9), anchored() says whether the
 * edges apply now (gameplay) or everything is centred (menus, attract).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lindbergh_rt.h"
#include "gl_posix.h"
#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
#include <windows.h>
#define HOTD4_DESKTOP_HUD 1
#endif

/* CEnvMgr::RenderEnv indices in the retail HOTD4 executable. */
enum { HUD_PLAYER_SPRITES = 15, HUD_ARCADE_TEXT = 16 };
static int g_render_env = -1;
static HudSpriteLayout g_sprite_layout;
static unsigned g_wide_canvas_frame = ~0u;
static float g_hud_scale;
static int g_hud_scale_set;

float hud_custom_scale(void)
{
    if (g_hud_scale_set) return g_hud_scale;
#ifdef HOTD4_DESKTOP_HUD
    const char *value = getenv("HOTD4_HUD_SCALE");
    if (value) {
        if (!strcmp(value, "75")) return 0.75f;
        if (!strcmp(value, "100")) return 1.0f;
        if (!strcmp(value, "125")) return 1.25f;
    }
#endif
    return 0.0f;
}

void hud_set_custom_scale(float scale)
{
    g_hud_scale = scale;
    g_hud_scale_set = 1;
}

int hud_custom_render_env(int id)
{
    int previous = g_render_env;
    g_render_env = id;
    return previous;
}


HudSpriteLayout hud_sprite_layout(HudSpriteLayout layout)
{
    HudSpriteLayout previous = g_sprite_layout;
    g_sprite_layout = layout;
    return previous;
}

extern unsigned g_frame;
static float g_f;

void hud_wide_canvas_mark(void)
{
    g_wide_canvas_frame = g_frame;
}

static int wide_canvas_active(void)
{
    // The game's sprite commands are queued and submitted later by RenderEnv.
    return g_wide_canvas_frame == g_frame && g_render_env == HUD_PLAYER_SPRITES;
}
static int (*g_anchored)(void);
int hud_custom_draw_viewport(int viewport[4])
{
    /* The ammunition models are drawn as indexed geometry in the separate
     * player UI pass. Scale them in screen space, around the player's edge. */
    if (wide_canvas_active() || g_render_env != HUD_PLAYER_SPRITES || g_hud_scale <= 0 || g_hud_scale == 1)
        return 0;

    GLint framebuffer, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, vp);
    if (framebuffer || vp[2] <= 0 || vp[3] <= 0) return 0;

    GLfloat projection[16], modelview[16];
    glGetFloatv(GL_PROJECTION_MATRIX, projection);
    glGetFloatv(GL_MODELVIEW_MATRIX, modelview);
    float clip_x = 0, clip_w = 0;
    for (int col = 0; col < 4; col++) {
        clip_x += projection[4 * col] * modelview[12 + col];
        clip_w += projection[4 * col + 3] * modelview[12 + col];
    }
    float side = signbit(clip_x) == signbit(clip_w) ? 0.98f : 0.02f;
    int width = (int)lroundf(vp[2] * g_hud_scale);
    int height = (int)lroundf(vp[3] * g_hud_scale);
    memcpy(viewport, vp, sizeof vp);
    glViewport(vp[0] + (int)lroundf((vp[2] - width) * side),
               vp[1] + (int)lroundf((vp[3] - height) * 0.03f), width, height);
    return 1;
}

/* the layout's own coordinates, when it was positioned for a smaller frame
 * than it is drawn into: NDC x -> lx x + lox, y -> ly y + loy, applied to quads
 * drawn into a viewport at least g_layout_vw wide, before the widening */
static float g_lx = 1, g_lox, g_ly = 1, g_loy;
static int g_layout_vw;

/* the modelview, as far as it matters here: identity or not */
static GLenum g_mode = GL_MODELVIEW;
static int g_mv_identity;

enum { OP_COLOR4UB, OP_TEXCOORD2F, OP_MULTITEX2F, OP_VERTEX3F };
typedef struct { int op; GLenum target; float v[4]; } Op;
#define MAX_OPS 1024
static Op g_ops[MAX_OPS];
static int g_nops, g_recording, g_overflow;
static GLenum g_prim;

/* the previous quad, for keeping words and gauges together */
static float g_last_box[4];     /* x0 x1 y0 y1, NDC */
static float g_last_s;
static unsigned g_last_frame = ~0u;

static HleHandler o_glMatrixMode, o_glLoadIdentity, o_glLoadMatrixf, o_glMultMatrixf, o_glTranslatef,
                  o_glScalef, o_glRotatef, o_glPushMatrix, o_glPopMatrix, o_glBegin, o_glEnd,
                  o_glColor4ub, o_glTexCoord2f, o_glMultiTexCoord2f, o_glVertex3f, o_glVertex3fv;

static float argf(CPU *c, int i) { uint32_t b = A32(i); float f; memcpy(&f, &b, 4); return f; }

static void mv_changed(void) { if (g_mode == GL_MODELVIEW) g_mv_identity = 0; }

static void w_glMatrixMode(CPU *c)   { g_mode = A32(0); o_glMatrixMode(c); }
static void w_glLoadIdentity(CPU *c) { if (g_mode == GL_MODELVIEW) g_mv_identity = 1; o_glLoadIdentity(c); }
static void w_glLoadMatrixf(CPU *c)  { mv_changed(); o_glLoadMatrixf(c); }
static void w_glMultMatrixf(CPU *c)  { mv_changed(); o_glMultMatrixf(c); }
static void w_glTranslatef(CPU *c)   { mv_changed(); o_glTranslatef(c); }
static void w_glScalef(CPU *c)       { mv_changed(); o_glScalef(c); }
static void w_glRotatef(CPU *c)      { mv_changed(); o_glRotatef(c); }
static void w_glPushMatrix(CPU *c)   { o_glPushMatrix(c); }
static void w_glPopMatrix(CPU *c)    { mv_changed(); o_glPopMatrix(c); }

static void push(int op, GLenum target, float a, float b, float cc, float d)
{
    if (g_nops >= MAX_OPS) { g_overflow = 1; return; }
    Op *o = &g_ops[g_nops++];
    o->op = op; o->target = target; o->v[0] = a; o->v[1] = b; o->v[2] = cc; o->v[3] = d;
}

static void w_glBegin(CPU *c)
{
    int fixed_canvas = g_sprite_layout == HUD_LAYOUT_GRAB ||
                       g_sprite_layout == HUD_LAYOUT_SPLASH ||
                       g_sprite_layout == HUD_LAYOUT_CENTER || g_sprite_layout == HUD_LAYOUT_WIDE;
    if (g_f > 0 && (g_mv_identity || wide_canvas_active() || fixed_canvas) && A32(0) == GL_QUADS) {
        g_recording = 1; g_nops = 0; g_overflow = 0; g_prim = A32(0);
        return;
    }
    o_glBegin(c);
}

static void w_glColor4ub(CPU *c)
{
    if (!g_recording) { o_glColor4ub(c); return; }
    push(OP_COLOR4UB, 0, A32(0) & 0xFF, A32(1) & 0xFF, A32(2) & 0xFF, A32(3) & 0xFF);
}
static void w_glTexCoord2f(CPU *c)
{
    if (!g_recording) { o_glTexCoord2f(c); return; }
    push(OP_TEXCOORD2F, 0, argf(c, 0), argf(c, 1), 0, 0);
}
static void w_glMultiTexCoord2f(CPU *c)
{
    if (!g_recording) { o_glMultiTexCoord2f(c); return; }
    push(OP_MULTITEX2F, A32(0), argf(c, 1), argf(c, 2), 0, 0);
}
static void w_glVertex3f(CPU *c)
{
    if (!g_recording) { o_glVertex3f(c); return; }
    push(OP_VERTEX3F, 0, argf(c, 0), argf(c, 1), argf(c, 2), 0);
}
static void w_glVertex3fv(CPU *c)
{
    if (!g_recording) { o_glVertex3fv(c); return; }
    const float *v = (const float *)APTR(0);
    push(OP_VERTEX3F, 0, v[0], v[1], v[2], 0);
}

static void replay(void)
{
    glBegin(g_prim);
    for (int i = 0; i < g_nops; i++) {
        const Op *o = &g_ops[i];
        switch (o->op) {
        case OP_COLOR4UB:   glColor4ub((GLubyte)o->v[0], (GLubyte)o->v[1], (GLubyte)o->v[2], (GLubyte)o->v[3]); break;
        case OP_TEXCOORD2F: glTexCoord2f(o->v[0], o->v[1]); break;
        case OP_MULTITEX2F: glMultiTexCoord2f(o->target, o->v[0], o->v[1]); break;
        case OP_VERTEX3F:   glVertex3f(o->v[0], o->v[1], o->v[2]); break;
        }
    }
    glEnd();
}

/* where along x this quad goes: s in x' = f x + s */
static float place(const float box[4])
{
    float w = box[1] - box[0], h = box[3] - box[2], cx = (box[0] + box[1]) / 2;
    float edge = 1 - g_f;
    /* continuing the previous quad: same row, starting about where it ended */
    if (g_last_frame == g_frame) {
        float lh = g_last_box[3] - g_last_box[2];
        int same_row = fabsf(box[2] - g_last_box[2]) < 0.5f * (h > lh ? h : lh) + 0.01f;
        int follows = box[0] > g_last_box[0] - 0.01f && box[0] < g_last_box[1] + 0.25f * (w + h) + 0.02f;
        if (same_row && follows) return g_last_s;
    }
    if (!g_anchored || !g_anchored()) return 0;
    if (cx < -1.0f / 3) return -edge;
    if (cx >  1.0f / 3) return  edge;
    return 0;
}

#ifdef HOTD4_DESKTOP_HUD
enum { ATLAS_SOURCE = 256, ATLAS_SCALE = 4, ATLAS_SIZE = ATLAS_SOURCE * ATLAS_SCALE };

static GLuint arcade_atlas(void)
{
    /* Preserve the original UV rectangles and fill them with rasterized
     * outline text at four times the original resolution. */
    static const struct {
        const wchar_t *text;
        int x0, x1, y;
    } labels[] = {
        {L"0", 2, 19, 2}, {L"1", 22, 38, 2}, {L"2", 42, 59, 2},
        {L"3", 62, 79, 2}, {L"4", 81, 99, 2}, {L"5", 102, 119, 2},
        {L"6", 121, 139, 2}, {L"7", 142, 159, 2}, {L"8", 161, 179, 2},
        {L"9", 181, 199, 2}, {L"/", 202, 215, 2},
        {L"CREDITS TO START", 4, 200, 26},
        {L"CREDITS TO CONTINUE", 4, 240, 50},
        {L"INSERT COIN(S)", 48, 208, 75},
        {L"INSERT MORE COIN(S)", 17, 243, 100},
        {L"INSERT MORE COIN(S)", 17, 243, 123},
        {L"PRESS START BUTTON", 15, 243, 146},
        {L"CREDIT(S)", 147, 252, 171},
        {L"FREE PLAY", 72, 184, 194},
        {L"PLEASE WAIT", 60, 197, 218}
    };
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = ATLAS_SIZE;
    info.bmiHeader.biHeight = -ATLAS_SIZE;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;

    unsigned char *mask = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP dib = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&mask, NULL, 0) : NULL;
    HFONT font = CreateFontW(-19 * ATLAS_SCALE, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    unsigned char *rgba = malloc(ATLAS_SIZE * ATLAS_SIZE * 4);
    GLuint texture = 0;
    if (!dib || !mask || !font || !rgba) goto cleanup;

    memset(mask, 0, ATLAS_SIZE * ATLAS_SIZE * 4);
    HGDIOBJ previous_bitmap = SelectObject(dc, dib);
    HGDIOBJ previous_font = SelectObject(dc, font);
    SetGraphicsMode(dc, GM_ADVANCED);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    for (unsigned i = 0; i < sizeof labels / sizeof *labels; i++) {
        const wchar_t *label = labels[i].text;
        int characters = (int)wcslen(label);
        SIZE measured;
        if (!GetTextExtentPoint32W(dc, label, characters, &measured) || !measured.cx) continue;
        XFORM transform = {0};
        transform.eM11 = (float)((labels[i].x1 - labels[i].x0 - 4) * ATLAS_SCALE) / measured.cx;
        transform.eM22 = 1.0f;
        transform.eDx = (float)((labels[i].x0 + 2) * ATLAS_SCALE);
        transform.eDy = (float)((labels[i].y + 16) * ATLAS_SCALE);
        SetWorldTransform(dc, &transform);
        TextOutW(dc, 0, 0, label, characters);
        ModifyWorldTransform(dc, NULL, MWT_IDENTITY);
    }
    GdiFlush();
    SelectObject(dc, previous_font);
    SelectObject(dc, previous_bitmap);

    memset(rgba, 0, ATLAS_SIZE * ATLAS_SIZE * 4);
    const int radius = ATLAS_SCALE;
    for (int y = radius; y < ATLAS_SIZE - radius; y++) {
        for (int x = radius; x < ATLAS_SIZE - radius; x++) {
            int i = (y * ATLAS_SIZE + x) * 4;
            unsigned fill = mask[i], edge = fill;
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    if (dx * dx + dy * dy > radius * radius) continue;
                    unsigned coverage = mask[i + (dy * ATLAS_SIZE + dx) * 4];
                    if (coverage > edge) edge = coverage;
                }
            }
            if (edge) {
                rgba[i] = rgba[i + 1] = rgba[i + 2] = (unsigned char)(245 * fill / edge);
                rgba[i + 3] = (unsigned char)edge;
            }
        }
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_SIZE, ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPopClientAttrib();

cleanup:
    free(rgba);
    if (font) DeleteObject(font);
    if (dib) DeleteObject(dib);
    if (dc) DeleteDC(dc);
    return texture;
}

static GLuint bind_arcade_atlas(void)
{
    /* Environment 16 also draws other text. Identify only the stock English
     * credit/coin atlas by its alpha mask before replacing its texture. */
    static GLuint original, replacement;
    static GLint last_checked;
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    if (bound <= 0) return 0;
    if ((GLuint)bound == original) {
        glBindTexture(GL_TEXTURE_2D, replacement);
        return original;
    }
    if (original || bound == last_checked) return 0;
    last_checked = bound;

    GLint width, height, pack, unpack;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width != ATLAS_SOURCE || height != ATLAS_SOURCE) return 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
    if (pack || unpack) return 0;

    unsigned char *pixels = malloc(ATLAS_SOURCE * ATLAS_SOURCE * 4);
    if (!pixels) return 0;
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPopClientAttrib();
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 3; i < ATLAS_SOURCE * ATLAS_SOURCE * 4; i += 4)
        hash = (hash ^ (pixels[i] >= 128)) * UINT64_C(1099511628211);
    free(pixels);
    if (hash != UINT64_C(0xbfb469df87389b5d)) return 0;

    replacement = arcade_atlas();
    if (!replacement) return 0;
    original = (GLuint)bound;
    fprintf(stderr, "[hud] high-resolution credit/coin atlas active\n");
    return original;
}
#endif

static void w_glEnd(CPU *c)
{
    if (!g_recording) { o_glEnd(c); return; }
    g_recording = 0;

    GLfloat p[16];
    glGetFloatv(GL_PROJECTION_MATRIX, p);
    int wide_canvas = wide_canvas_active() || g_sprite_layout == HUD_LAYOUT_WIDE;
    if (wide_canvas || g_sprite_layout == HUD_LAYOUT_GRAB || g_sprite_layout == HUD_LAYOUT_SPLASH || g_sprite_layout == HUD_LAYOUT_CENTER) {
        // Full-screen overlays and route selection share the whole viewport.
        // Other fixed canvases preserve the shape of their original artwork.
        float fit_x = wide_canvas ? 1.0f : g_f;
        for (int col = 0; col < 4; ++col) {
            p[col * 4] = fit_x * (g_lx * p[col * 4] + g_lox * p[col * 4 + 3]);
            p[col * 4 + 1] = g_ly * p[col * 4 + 1] + g_loy * p[col * 4 + 3];
        }
        GLint mode;
        glGetIntegerv(GL_MATRIX_MODE, &mode);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadMatrixf(p);
        replay();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode((GLenum)mode);
        return;
    }
    float box[4] = { 1e9f, -1e9f, 1e9f, -1e9f };
    int nv = 0;
    for (int i = 0; i < g_nops; i++) {
        const Op *o = &g_ops[i];
        if (o->op != OP_VERTEX3F) continue;
        float x = o->v[0], y = o->v[1], z = o->v[2];
        float cx = p[0] * x + p[4] * y + p[8] * z + p[12];
        float cy = p[1] * x + p[5] * y + p[9] * z + p[13];
        float cw = p[3] * x + p[7] * y + p[11] * z + p[15];
        if (fabsf(cw) < 1e-6f) continue;
        float nx = cx / cw, ny = cy / cw;
        if (nx < box[0]) box[0] = nx;
        if (nx > box[1]) box[1] = nx;
        if (ny < box[2]) box[2] = ny;
        if (ny > box[3]) box[3] = ny;
        nv++;
    }
    /* covers the screen, or nothing to measure: as it was */
    if (g_overflow || !nv || box[1] - box[0] >= 1.9f) { replay(); return; }

    /* the layout's coordinates into the frame's */
    float lx = 1, lox = 0, ly = 1, loy = 0;
    if (g_layout_vw) {
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
#ifdef HOTD4_DESKTOP_HUD
        /* Compare cabinet pixels with cabinet pixels. The displayed viewport
         * may be smaller (supersampling) or larger than the game's render size. */
        host_viewport_unscale(vp);
#endif
        if (vp[2] >= g_layout_vw) { lx = g_lx; lox = g_lox; ly = g_ly; loy = g_loy; }
    }
    box[0] = lx * box[0] + lox; box[1] = lx * box[1] + lox;
    box[2] = ly * box[2] + loy; box[3] = ly * box[3] + loy;
    if (box[1] - box[0] >= 1.9f) {          /* now covers the frame: scaled, not squeezed */
        GLfloat m[16];
        for (int col = 0; col < 4; col++) {
            m[col * 4 + 0] = lx * p[col * 4 + 0] + lox * p[col * 4 + 3];
            m[col * 4 + 1] = ly * p[col * 4 + 1] + loy * p[col * 4 + 3];
            m[col * 4 + 2] = p[col * 4 + 2];
            m[col * 4 + 3] = p[col * 4 + 3];
        }
        GLint mode; glGetIntegerv(GL_MATRIX_MODE, &mode);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadMatrixf(m);
        replay();
        glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode((GLenum)mode);
        return;
    }

    int result_panel = g_sprite_layout == HUD_LAYOUT_RESULT_LEFT || g_sprite_layout == HUD_LAYOUT_RESULT_RIGHT;
    int score_row = g_sprite_layout == HUD_LAYOUT_SCORE_LEFT || g_sprite_layout == HUD_LAYOUT_SCORE_RIGHT;
    float s = 0;
    if (result_panel || score_row || g_sprite_layout == HUD_LAYOUT_CREDIT_LEFT || g_sprite_layout == HUD_LAYOUT_CREDIT_RIGHT) {
        // Give each group a common offset. Clamping the score glyphs
        // individually can make adjacent digits land on top of one another.
        int left = g_sprite_layout == HUD_LAYOUT_CREDIT_LEFT ||
                   g_sprite_layout == HUD_LAYOUT_RESULT_LEFT ||
                   g_sprite_layout == HUD_LAYOUT_SCORE_LEFT;
        if (g_anchored && g_anchored()) {
            float edge = 1.0f - g_f;
            if (score_row) edge -= 0.04f; // Inset the whole row by 2% of the frame.
            // Leave room for both players' full life displays.
            if (g_render_env == HUD_ARCADE_TEXT &&
                (g_sprite_layout == HUD_LAYOUT_CREDIT_LEFT || g_sprite_layout == HUD_LAYOUT_CREDIT_RIGHT))
                edge -= 0.08f;
            s = left ? -edge : edge;
        }
    } else if (g_sprite_layout != HUD_LAYOUT_BOSS) {
        s = place(box);
        /* Keep widened quads within the visible viewport. */
        if (box[0] * g_f + s < -0.96f) s = -0.96f - box[0] * g_f;
        if (box[1] * g_f + s >  0.96f) s =  0.96f - box[1] * g_f;
        memcpy(g_last_box, box, sizeof box);
        g_last_s = s;
        g_last_frame = g_frame;
    }

    float scale = 1, anchor_x = 0, anchor_y = 0;
    if (g_hud_scale > 0 && (g_render_env == HUD_PLAYER_SPRITES || g_render_env == HUD_ARCADE_TEXT)) {
        scale = g_hud_scale;
        anchor_x = s < -0.05f ? -0.96f : s > 0.05f ? 0.96f : 0.0f;
        // Tall result cards scale vertically around the middle of the frame.
        if (!result_panel)
            anchor_y = score_row || g_sprite_layout == HUD_LAYOUT_BOSS ? 0.94f : -0.94f;
    }
    float tx = scale * (g_f * lox + s) + (1.0f - scale) * anchor_x;
    float ty = scale * loy + (1.0f - scale) * anchor_y;

    /* Apply the HUD's affine transform to the projection in clip space. */
    GLfloat m[16];
    for (int col = 0; col < 4; col++) {
        m[col * 4 + 0] = scale * g_f * lx * p[col * 4 + 0] + tx * p[col * 4 + 3];
        m[col * 4 + 1] = scale * ly * p[col * 4 + 1] + ty * p[col * 4 + 3];
        m[col * 4 + 2] = p[col * 4 + 2];
        m[col * 4 + 3] = p[col * 4 + 3];
    }
    GLint mode; glGetIntegerv(GL_MATRIX_MODE, &mode);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(m);
#ifdef HOTD4_DESKTOP_HUD
    GLuint original_atlas = 0;
    if (g_hud_scale > 0 && g_render_env == HUD_ARCADE_TEXT)
        original_atlas = bind_arcade_atlas();
#endif
    replay();
#ifdef HOTD4_DESKTOP_HUD
    if (original_atlas) glBindTexture(GL_TEXTURE_2D, original_atlas);
#endif
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode((GLenum)mode);
}

void hud_widen_enable(float f, int (*anchored)(void))
{
    g_f = f;
    g_hud_scale = hud_custom_scale();
    g_anchored = anchored;
    for (unsigned i = 0; i < HLE_COUNT; i++) {
        const char *n = hle_name((HleId)i);
        if (!g_hle_handlers[i]) continue;
#define HOOK(nm) if (!strcmp(n, #nm)) { o_##nm = g_hle_handlers[i]; g_hle_handlers[i] = w_##nm; continue; }
        HOOK(glMatrixMode) HOOK(glLoadIdentity) HOOK(glLoadMatrixf) HOOK(glMultMatrixf) HOOK(glTranslatef)
        HOOK(glScalef) HOOK(glRotatef) HOOK(glPushMatrix) HOOK(glPopMatrix) HOOK(glBegin) HOOK(glEnd)
        HOOK(glColor4ub) HOOK(glTexCoord2f) HOOK(glMultiTexCoord2f) HOOK(glVertex3f) HOOK(glVertex3fv)
#undef HOOK
    }
    fprintf(stderr, "[hud] 2D drawn at %.3f of the frame's width\n", f);
}

/* The 2D was positioned for a frame lx, ly times smaller than the viewport it
 * is drawn into (at least vw wide), anchored at (ox, oy) in NDC. */
void hud_widen_layout(float lx, float lox, float ly, float loy, int vw)
{
    g_lx = lx; g_lox = lox; g_ly = ly; g_loy = loy; g_layout_vw = vw;
}
