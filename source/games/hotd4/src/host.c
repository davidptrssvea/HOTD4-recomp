/*
 * host.c - run recompiled The House of the Dead 4.
 *
 * There is very little to do here, and that is the point. The runtime maps the
 * game where it was linked and builds the stack the kernel would have; the
 * lifted code is the game. This just hands control over at the ELF's entry
 * point and gets out of the way.
 *
 * Everything the game reaches for that is not its own code - libc, OpenGL,
 * sound, the JVS I/O board - arrives as hle_call(), and the handlers live in
 * hle_*.c next to this file. A handler that is missing aborts naming itself,
 * so the way to work on this port is to run it and read what it asks for.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "xr_overlay.h"
#include "game_profile.h"
#include "high_scores.h"
#include "gameplay_options.h"
#include "nn_culling.h"
#include "lindbergh_rt.h"
extern HleHandler dispatch_lifted(uint32_t va);

#if defined(_WIN32) && !defined(LINDBERGH_OPENXR)
#include <SDL.h>
#endif

/* Use the original emitter variants and lifecycle. The optional stronger
 * body-hit variant does not replace severed-limb, head or boss effects. */
static HleHandler lifted_blood_ctor;
static void h_blood_ctor(CPU *c)
{
    uint32_t args=c->esp,self=rd32(args),type=rd32(args+12),scale_bits=rd32(args+24);
    int enhanced=gameplay_blood_splatter() && type==1;
    if(enhanced){
        float scale;memcpy(&scale,&scale_bits,4);
        if(isfinite(scale) && scale>0 && scale<=8){
            scale*=1.25f;uint32_t bits;memcpy(&bits,&scale,4);wr32(args+24,bits);
        }
        wr32(args+12,type+1);
    }
    push32(c,rd32(args-4));lifted_blood_ctor(c);
    if(enhanced){wr32(args+12,type);wr32(args+24,scale_bits);}
    static int log=-1;
    if(log<0){const char *v=getenv("LINDBERGH_BLOOD_LOG");log=v && !strcmp(v,"1");}
    if(log){extern unsigned g_frame;fprintf(stderr,"[blood] frame %u: original %u, selected %u, active %u\n",g_frame,type,enhanced?type+1:type,rd32(self+0x78));}
}
static void install_blood_effects(void)
{
    const char *name="_ZN12CBloodEffectC9EP6MlTaskPPS_iP12KfVectorNNGLS5_f";
    lifted_blood_ctor=dispatch_lifted(guest_symbol(name));
    if(lifted_blood_ctor)guest_override(name,h_blood_ctor);
}

/* The gun, at the two functions the game's gun code reads it through. Both
 * take the board's raw words and the game's stored calibration; a mouse needs
 * neither, so they answer from the pointer directly.
 *
 * GetAdjstGunPos(&x, &y, rawx, rawy, adjust, ...) -> on screen. The position
 * is a byte each, 0..255 across the whole screen. Register arguments: eax =
 * &x, edx = &y. The pointer outside the window is off the screen. */
static uint32_t g_gun_adjust[2];   /* each player's calibration, as GetAdjstGunPos gets it */
static void h_gun_pos(CPU *c)
{
    /* the stack: raw x, raw y, adjust, player - player 2's gun (dual
     * wielding) answers from the second pointer, not the first */
    uint32_t player = rd32(c->esp + 20);
    if (player < 2) g_gun_adjust[player] = rd32(c->esp + 16);
    int mx, my, w, h;
    if ((player^host_solo_player()) == 1) { if (!host_gun2_state(&mx, &my)) mx = my = -100000; }
    else host_mouse_state(&mx, &my, NULL);
    lindbergh_window_size(&w, &h);
    int on = mx >= 0 && my >= 0 && mx < w && my < h;
    /* the nearest of the 256 gun steps, first and last on the window's edges */
    long x = ((long)mx * 255 + (w - 1) / 2) / (w > 1 ? w - 1 : 1),
         y = ((long)my * 255 + (h - 1) / 2) / (h > 1 ? h - 1 : 1);
    wr8(c->eax, (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x));
    wr8(c->edx, (uint8_t)(y < 0 ? 0 : y > 255 ? 255 : y));
    c->eax = on;
}

/* The arcade also treats loss of gun tracking as an unconditional reload.
 * Keep the position result, but let the host's trigger/reload policy decide
 * when to reload. Otherwise simply pointing off screen bypasses the option. */
static HleHandler lifted_gun_mainproc;
static void h_gun_mainproc(CPU *c)
{
    uint32_t input=c->edx,player=c->ecx&0xff;
    push32(c,rd32(c->esp-4));lifted_gun_mainproc(c);
    if(player<2)wr32(input+0x0c,(host_cabinet_buttons() & (player?CAB_P2_RELOAD:CAB_P1_RELOAD))!=0);
}
static void install_gun_reload_policy(void)
{
    const char *name="_ZN7CGunMgr8MainProcEPNS_9INPUT_SETEh";
    lifted_gun_mainproc=dispatch_lifted(guest_symbol(name));
    if(lifted_gun_mainproc)guest_override(name,h_gun_mainproc);
}

#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
/* Rev A CPlayerMgr::GetCatchType checks these three grab/struggle flags.
 * Special has a different manager layout; never reuse its offsets here. */
static int mouse_grab_shake(void)
{
    uint32_t manager = rd32(0x0a7117a8u);
    int caught = manager && (rd32(manager + 0x54) || rd32(manager + 0x74) || rd32(manager + 0x7c));
    int shake = host_mouse_shake(caught);
    return caught ? shake : -1;
}

static unsigned mouse_grab_buttons(unsigned buttons)
{
    int shake = mouse_grab_shake();
    if (shake >= 0) {
        unsigned reload=host_solo_player()?CAB_P2_RELOAD:CAB_P1_RELOAD;
        unsigned firemode=host_solo_player()?CAB_P2_FIREMODE:CAB_P1_FIREMODE;
        buttons &= ~(reload | firemode);
        if (shake) buttons |= reload;
    }
    return buttons;
}
#endif

/* The reload. A House of the Dead 4 gun has an accelerometer and the game
 * reloads when it is shaken. GetAdjstGunAccel(&a, &b, rawa, rawb, adjust)
 * gives the two axes as 0..1 past the calibrated dead zone, and anything over
 * zero is a shake. Outside grabs, the cabinet's reload input (the right
 * button or pointer leaving the window) shakes it for a quarter second.
 * During desktop mouse grabs, only actual back-and-forth motion contributes;
 * old reload pulses must not carry over into a new grab. Register arguments:
 * eax = &a, edx = &b. */
static void h_gun_accel(CPU *c)
{
    /* whose gun: its calibration sits 12 bytes past the same player's
     * position calibration (GetAdjstGunPos's, recorded above) */
    uint32_t adj = rd32(c->esp + 16);
    int pl = g_gun_adjust[1] && adj == g_gun_adjust[1] + 12;
    static unsigned left[2];
    int shake = -1;
#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
    if (pl==host_solo_player()) shake = mouse_grab_shake();
#endif
    float v;
    if (shake >= 0) {
        left[pl] = 0;
        v = shake ? 1.0f : 0.0f;
    } else {
        if (host_cabinet_buttons() & (pl ? CAB_P2_RELOAD : CAB_P1_RELOAD)) left[pl] = 15;
        v = left[pl] ? 1.0f : 0.0f;
        if (left[pl]) left[pl]--;
    }
    uint32_t bits; memcpy(&bits, &v, 4);
    wr32(c->eax, bits);
    wr32(c->edx, bits);
}

/* GetSpriteResolution(&w, &h) -> screen ratio. The game lays its 2D out for
 * two monitors only: the cabinet's 1280x768 (ratio 2) and 640x480 (every
 * other shape, positions in 640x480 pixels). It then converts those with the
 * sprite resolution, which for a 16:9 monitor is the monitor's own 1280x720,
 * so the HUD came out at half size in the top-left. In a 16:9 mode the sprite
 * resolution is 640x480, matching the layout; the runtime then draws that
 * 4:3 layout at its own shape in the 16:9 frame (host_hud_widen). */
static int movie_aspect_active, screen_four_three;
static void h_four_three_ratio(CPU*c){c->eax=1;}

static void h_sprite_res(CPU *c)
{
    /* The camera asks too, for the 3D viewport: it gets the monitor's own
     * size, or the 3D would render at 640x480. The return address is just
     * below the stack pointer once the override is entered. */
    uint32_t ret = rd32(c->esp - 4);
    int w = 640, h = 480;
    int movie = ret == 0x0807cda4 || ret == 0x0807ca19;
    if (movie && movie_aspect_active) {
        /* Stock widescreen sprite canvas: keep the original Sofdec layout
         * arithmetic in its own units, including its type-specific crop. */
        wr32(c->eax,screen_four_three?640:1280);wr32(c->edx,screen_four_three?480:768);c->eax=screen_four_three?1:2;
        return;
    }
    if ((ret >= 0x08210c14 && ret < 0x082127c4) || movie)
        lindbergh_window_size(&w, &h);
    wr32(c->eax, (uint32_t)w);
    wr32(c->edx, (uint32_t)h);
    c->eax = screen_four_three?1:5; /* native 4:3 or widescreen layout */
}

/* CSofdecMgr::Load builds four 24-byte texture vertices at +0x98.
 * The stock wide canvas is 1280x768 (GetSpriteResolution, ratio 2).
 * Constructor constants 1280, 960, 120 mix canvas and target units when
 * handed a high-resolution framebuffer. Preserve the original calculation,
 * then map the finished quad, with its original UVs/crop, to the target. */
extern HleHandler dispatch_lifted(uint32_t va);
static HleHandler lifted_movie_load;
static void h_movie_load(CPU *c)
{
    uint32_t manager=rd32(c->esp);
    int slot=(int)rd32(c->esp+4),movie=(int)rd32(c->esp+8);
    int type=(int)rd32(c->esp+16);
    push32(c,rd32(c->esp-4));
    lifted_movie_load(c);
    if(slot<0 || slot>=2 || movie<0 || movie>=79)return;
    int width,height;lindbergh_window_size(&width,&height);
    if(width<=0 || height<=0)return;
    float sx=width/(screen_four_three?640.0f:1280.0f),sy=height/(screen_four_three?480.0f:768.0f);
    uint32_t vertices=manager+0x98+(uint32_t)slot*0xc4;
    float raw[8],mapped[8];
    for(unsigned i=0;i<4;i++){
        uint32_t vertex=vertices+i*24;
        uint32_t bx=rd32(vertex),by=rd32(vertex+4);
        float x,y;memcpy(&x,&bx,4);memcpy(&y,&by,4);
        raw[2*i]=x;raw[2*i+1]=y;
        x*=sx;y*=sy;mapped[2*i]=x;mapped[2*i+1]=y;
        memcpy(&bx,&x,4);memcpy(&by,&y,4);wr32(vertex,bx);wr32(vertex+4,by);
    }
    fprintf(stderr,"[movie-aspect] id=%d type=%d source=(%.3f,%.3f)-(%.3f,%.3f) mapped=(%.3f,%.3f)-(%.3f,%.3f) target=%dx%d\n",
        movie,type,raw[0],raw[1],raw[6],raw[7],mapped[0],mapped[1],mapped[6],mapped[7],width,height);
}
static void install_movie_aspect(void)
{
    const char *name="_ZN10CSofdecMgr4LoadEiij8eSfdType";
    lifted_movie_load=dispatch_lifted(guest_symbol(name));
    if(lifted_movie_load && guest_override(name,h_movie_load))movie_aspect_active=1;
    else {fprintf(stderr,"[movie-aspect] original layout hook unavailable\n");exit(1);}
}

/* whether the HUD keeps to the screen's edges, or stays a centred 4:3 block */
static int hud_anchored(void) { return 1; }

/* What the runtime needs to know about this game (game_profile.h). */
static const LgGameProfile k_profile = {
    .solo_characters = 1,
    .title = "The House of the Dead 4",
    /* The monitor the game draws for. 16:9 is the board's 1360x768 mode - its
     * DIP switches have no 1280x720 one - and the game renders and aims for
     * 1360x768, so the frame is that size too and the window scales it.
     * LINDBERGH_WIDTH/HEIGHT pick another mode (1280x768 is the cabinet's). */
    .width = 1360, .height = 768,
    /* The gun goes straight to the game (h_gun_pos above), so the cabinet's
     * raw gun words carry the pointer as is too: no correction. */
    .aim = { 1, 0, 1, 0 },
    .vr = {
        .gun_model = XO_GUN_TP9,    /* a B&T TP9 in the player's hand */
        .title = "The House of the Dead 4 VR",
        /* In the headset the game draws its 640x480 mode, which the
         * runtime's stereo (stereo.c) scales to each eye; the screen in front
         * of you is 4:3 and the HUD sits on it as the game lays it out. */
        .width = 640, .height = 480,
        .scale = 1,
        .depth_read = LG_DEPTH_AUTO,
        /* GL on its own thread: in play the game thread is the limit (85-100%
         * of a core, the GPU 40-60% busy), a third of it the GL calls */
        .gl_thread = 1,
    },
};

#if defined(_WIN32) && !defined(LINDBERGH_OPENXR)
extern HleHandler dispatch_lifted(uint32_t va);

static HleHandler lifted_practice_stage_init;
static HleHandler lifted_practice_end_frame;
static unsigned practice_chapter;
static int practice_started;
static SDL_atomic_t practice_restart;
extern unsigned g_frame;
#if defined(_WIN64) && !defined(LINDBERGH_VULKAN)
extern int host_hotd4_restart_key(int key);
extern int host_hotd4_start_binding(void);
#else
static int host_hotd4_restart_key(int key){return key==SDLK_F6;}
static int host_hotd4_start_binding(void){return SDLK_RETURN;}
#endif

enum { PRACTICE_RESTART_EXIT = 74 };

static int SDLCALL practice_key(void *unused, SDL_Event *event)
{
    (void)unused;
    if (event->type == SDL_KEYDOWN && !event->key.repeat &&
        host_hotd4_restart_key(event->key.keysym.sym))
        SDL_AtomicSet(&practice_restart, 1);
    return 1;
}

static void h_practice_end_frame(CPU *c)
{
    static int watching;
    if (!watching) {
        SDL_AddEventWatch(practice_key, NULL);
        watching = 1;
    }
    if (SDL_AtomicGet(&practice_restart)) {
        fprintf(stderr, "[practice] restart chapter %u\n", practice_chapter);
        exit(PRACTICE_RESTART_EXIT);
    }

    push32(c, rd32(c->esp - 4));
    lifted_practice_end_frame(c);

    // Use the normal Start binding until the game's first stage initializes.
    // The regular SDL input path inserts credits if needed.
    if (!practice_started && g_frame % 30 == 0) {
        SDL_Event key;
        SDL_zero(key);
        key.type = SDL_KEYDOWN;
        key.key.state = SDL_PRESSED;
        key.key.keysym.sym = host_hotd4_start_binding();
        key.key.keysym.scancode = SDL_GetScancodeFromKey(key.key.keysym.sym);
        SDL_PushEvent(&key);
        key.type = SDL_KEYUP;
        key.key.state = SDL_RELEASED;
        SDL_PushEvent(&key);
    }
}

static void h_practice_stage_init(CPU *c)
{
    uint32_t marker_offset = 0, saved_offset = 0;
    if (!practice_started) {
        uint32_t stage = practice_chapter - 1;
        uint32_t game = rd32(c->esp);
        // CEvtMgr selects scene 0 from a table of chapter pointers. Marker 2
        // normally skips the load commands because the preceding chapter
        // already ran them. For this call, start at the first real command.
        uint32_t scene = rd32(0x084c8d20 + 4 * stage);
        uint32_t markers = rd32(scene + 4);
        uint32_t count = rd32(scene + 8);
        for (uint32_t i = 0; i < count; i++) {
            if (rd32(markers + 8 * i) == 2) {
                marker_offset = markers + 8 * i + 4;
                saved_offset = rd32(marker_offset);
                wr32(marker_offset, 3);
                break;
            }
        }
        if (!marker_offset) {
            fprintf(stderr, "[practice] no chapter entry found\n");
            exit(1);
        }
        wr32(game + 0x38, stage);
        wr32(game + 0x3c, 0);
        practice_started = 1;
        fprintf(stderr, "[practice] start chapter %u\n", practice_chapter);
    }

    push32(c, rd32(c->esp - 4));
    lifted_practice_stage_init(c);

    if (marker_offset) wr32(marker_offset, saved_offset);
}

static void install_practice(void)
{
    const char *chapter = getenv("HOTD4_PRACTICE_CHAPTER");
    if (!chapter || chapter[0] < '1' || chapter[0] > '6' || chapter[1]) return;

    const char *stage_init = "_ZN8CGameMgr17GM_STAGEINIT_funcEv";
    const char *end_frame = "_ZN5MyApp8EndFrameEv";
    lifted_practice_stage_init = dispatch_lifted(guest_symbol(stage_init));
    lifted_practice_end_frame = dispatch_lifted(guest_symbol(end_frame));
    if (!lifted_practice_stage_init || !lifted_practice_end_frame) {
        fprintf(stderr, "[practice] game build does not expose the chapter entry\n");
        exit(1);
    }

    practice_chapter = (unsigned)(chapter[0] - '0');
    if (!guest_override(stage_init, h_practice_stage_init) ||
        !guest_override(end_frame, h_practice_end_frame)) {
        fprintf(stderr, "[practice] chapter entry could not be installed\n");
        exit(1);
    }
}

#if defined(_WIN64) && !defined(LINDBERGH_VULKAN)
void hotd4_lighting_apply(void);
void hotd4_install_hd_textures(void);
static HleHandler lifted_render_surface_end;

static void h_render_surface_end(CPU *c)
{
    // The main world copy has both a color and a depth surface.
    if (rd32(c->esp) && rd32(c->esp + 4) && rd32(c->esp + 8))
        hotd4_lighting_apply();
    push32(c, rd32(c->esp - 4));
    lifted_render_surface_end(c);
}

static void install_lighting(void)
{
    const char *enabled = getenv("HOTD4_EXPERIMENT_LIGHTING");
    if (!enabled || strcmp(enabled, "1") != 0) return;
    const char *name = "_Z30_hySystemEndRenderTextureFrameP16HyTextureSurfaceS0_P9HyPBuffer";
    lifted_render_surface_end = dispatch_lifted(guest_symbol(name));
    if (lifted_render_surface_end) guest_override(name, h_render_surface_end);
}

#endif

#if defined(_WIN64) && !defined(LINDBERGH_VULKAN)
#include "photo_camera.h"

enum {
    PHOTO_PLAYER_SPRITES = 15,
    PHOTO_ARCADE_TEXT = 16,
    PHOTO_STACK_SIZE = 512 * 1024
};

static HleHandler lifted_render_env, lifted_camera_matrix, lifted_fovy;
static struct {
    CPU cpu;
    uint32_t manager, stack_top;
    uint32_t begin, draw, render, end;
    unsigned scene_frame;
    int environment;
    const PhotoCamera *active;
    float view[3][4];
} photo_render = {.scene_frame = ~0u, .environment = -1};

static int photo_world_environment(void)
{
    return photo_render.environment >= 0 && photo_render.environment < PHOTO_PLAYER_SPRITES;
}

static void h_photo_camera_matrix(CPU *c)
{
    uint32_t out = A32(1);
    // HyEnv::_renderMain reads the world-to-camera matrix at this call site.
    int world = rd32(c->esp - 4) == 0x08293fc7u && photo_world_environment();
    push32(c, rd32(c->esp - 4));
    lifted_camera_matrix(c);
    if (!world) return;
    if (!photo_render.active) {
        photo_render.scene_frame = g_frame;
        return;
    }

    float original[16], moved[16];
    memcpy(original, GP(out), sizeof original);
    memcpy(moved, original, sizeof moved);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 3; ++row) {
            const float *v = photo_render.view[row];
            moved[col * 4 + row] = v[0] * original[col * 4] + v[1] * original[col * 4 + 1] +
                                  v[2] * original[col * 4 + 2] + v[3] * original[col * 4 + 3];
        }
    }
    memcpy(GP(out), moved, sizeof moved);
}

static void h_photo_fovy(CPU *c)
{
    uint32_t caller = rd32(c->esp - 4);
    push32(c, caller);
    lifted_fovy(c);
    const PhotoCamera *camera = photo_render.active;
    // This call reads the lens for nnMakePerspectiveMatrix. Keep arcade HUD
    // projections, other uses of the getter and each camera's clip planes intact.
    if (camera && camera->zoom_stops != 0 && caller == 0x08294012u &&
        photo_world_environment() && c->eax > 0 && c->eax < 32768) {
        const float radians_per_half_angle_unit = 3.14159265f / 65536.0f;
        float half_angle = (float)c->eax * radians_per_half_angle_unit;
        float zoomed = atanf(tanf(half_angle) * exp2f(-camera->zoom_stops));
        c->eax = (uint32_t)lroundf(zoomed / radians_per_half_angle_unit);
    }
}

static void h_hud_render_env(CPU *c)
{
    int id = (int)c->edx;
    if (!photo_render.active && id == -1) {
        photo_render.cpu = *c;
        photo_render.manager = c->eax;
    }
    if (photo_render.active && photo_render.active->hide_hud &&
        (id == PHOTO_PLAYER_SPRITES || id == PHOTO_ARCADE_TEXT))
        return;
    int earlier_id = photo_render.environment;
    photo_render.environment = id;
    int previous = hud_custom_render_env(id);
    push32(c, rd32(c->esp - 4));
    lifted_render_env(c);
    hud_custom_render_env(previous);
    photo_render.environment = earlier_id;
}

static int hotd4_photo_camera_available(void)
{
    return g_frame != 0 && photo_render.scene_frame == g_frame - 1 && photo_render.manager &&
           lifted_camera_matrix && lifted_fovy && photo_render.begin && photo_render.draw && photo_render.render && photo_render.end;
}

static int hotd4_photo_render(const PhotoCamera *camera)
{
    if (!hotd4_photo_camera_available()) return 0;
    const uint32_t tasks = rd32(0x0a7118ccu);  // HOTD4's active MlTaskSystem.
    if (!tasks) return 0;
    if (!photo_render.stack_top) {
        void *stack = guest_alloc(PHOTO_STACK_SIZE, 16);
        if (!stack) return 0;
        photo_render.stack_top = H2G(stack) + PHOTO_STACK_SIZE;
    }

    float cy = cosf(camera->yaw), sy = sinf(camera->yaw);
    float cp = cosf(camera->pitch), sp = sinf(camera->pitch);
    float axes[3][3] = {{cy, 0, sy}, {-sy * sp, cp, cy * sp}, {-sy * cp, -sp, cy * cp}};
    float cr = cosf(camera->roll), sr = sinf(camera->roll);
    for (int column = 0; column < 3; ++column) {
        float right = axes[0][column], up = axes[1][column];
        axes[0][column] = cr * right - sr * up;
        axes[1][column] = sr * right + cr * up;
    }
    for (int row = 0; row < 3; ++row) {
        memcpy(photo_render.view[row], axes[row], sizeof axes[row]);
        photo_render.view[row][3] = -(axes[row][0] * camera->position[0] +
                                      axes[row][1] * camera->position[1] +
                                      axes[row][2] * camera->position[2]);
    }

    // Rebuild just the render passes. The task system's Pre/Main/Post phases
    // and the movie and sound servers stay paused.
    CPU cpu = photo_render.cpu;
    cpu.esp = photo_render.stack_top;
    photo_render.active = camera;
    guest_call(&cpu, photo_render.begin, NULL, 0);
    guest_call(&cpu, photo_render.draw, &tasks, 1);
    cpu.eax = photo_render.manager;
    cpu.edx = (uint32_t)-1;
    guest_call(&cpu, photo_render.render, NULL, 0);
    guest_call(&cpu, photo_render.end, NULL, 0);
    photo_render.active = NULL;
    return 1;
}

static void install_hud(void)
{
    const char *name = "_ZN7CEnvMgr9RenderEnvEi";
    photo_render.render = guest_symbol(name);
    lifted_render_env = dispatch_lifted(photo_render.render);
    if (lifted_render_env) guest_override(name, h_hud_render_env);

    photo_render.begin = guest_symbol("HySystemBeginFrame");
    photo_render.draw = guest_symbol("_ZN12MlTaskSystem8ExecDrawEv");
    photo_render.end = guest_symbol("HySystemEndFrame");
    const char *camera = "_ZN8HyCamera9GetMatrixEP12KfMatrixNNGL";
    lifted_camera_matrix = dispatch_lifted(guest_symbol(camera));
    if (lifted_camera_matrix && !guest_override(camera, h_photo_camera_matrix))
        lifted_camera_matrix = NULL;
    const char *fovy = "_ZN8HyCamera9GetNNFovyEv";
    lifted_fovy = dispatch_lifted(guest_symbol(fovy));
    if (lifted_fovy && !guest_override(fovy, h_photo_fovy))
        lifted_fovy = NULL;
    hotd4_photo_register(hotd4_photo_camera_available, hotd4_photo_render);
}
#endif

#endif /* Windows desktop helpers */

#if defined(_WIN32)
static HleHandler lifted_frame_rate_init;
static void sync_cutscene_swap(int cutscene)
{
#if defined(LINDBERGH_OPENXR)
    (void)cutscene; /* OpenXR presents the eyes; the desktop swap is unused. */
#else
    static int restore_interval;
    static int previous_interval;
    if (!SDL_GL_GetCurrentContext() || getenv("LINDBERGH_VSYNC")) return;
    if (cutscene && !restore_interval) {
        int interval = SDL_GL_GetSwapInterval();
        if (interval != 1 && SDL_GL_SetSwapInterval(1) == 0) {
            previous_interval = interval;
            restore_interval = 1;
        }
    } else if (!cutscene && restore_interval) {
        SDL_GL_SetSwapInterval(previous_interval);
        restore_interval = 0;
    }
#endif
}

static void h_frame_rate_init(CPU *c)
{
    uint32_t manager = rd32(c->esp);
    // Dispatch consumed the guest return address. Restore it for the lifted body.
    push32(c, rd32(c->esp - 4));
    lifted_frame_rate_init(c);

    // The software timer uses 3.333 ms ticks. Ten ticks is the 30 fps
    // cutscene mode. Keep its cadence, but wait at presentation so variable
    // rendering time after the guest's wait does not shift the displayed frame.
    int cutscene = rd32(manager) == 0 && rd32(manager + 0x20) == 1 &&
                   rd32(manager + 0x54) == 10;
    if (cutscene) wr32(manager + 0x54, 1);
    sync_cutscene_swap(cutscene);
    host_pace_fps(cutscene ? 30 : 0);
}

static void install_cutscene_pacing(void)
{
    const char *enabled = getenv("LINDBERGH_HOTD4_PACE_CUTSCENES");
    const char *unpaced = getenv("LINDBERGH_UNPACED");
#if defined(LINDBERGH_OPENXR)
    if (enabled && strcmp(enabled,"0")==0) return;
#else
    if (!enabled || strcmp(enabled,"1")!=0) return;
#endif
    if (unpaced && atoi(unpaced)!=0) return;

    const char *name = "_ZN13CFrameRateMgr5Init0EP19tagFRAMERATE_CONFIG";
    lifted_frame_rate_init = dispatch_lifted(guest_symbol(name));
    if (lifted_frame_rate_init && guest_override(name, h_frame_rate_init))
        fprintf(stderr,"[hotd4] cutscenes: original 30 Hz cadence, wait at presentation\n");
}
#endif

#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
#include "achievement_hooks.h"

enum {
    PLAYER_INDEX = 0x34,
    PLAYER_MODE = 0x38,
    PLAYER_RELOAD_PROMPT = 0x1d4,
    PLAYER_RELOAD_VOICE_TIMER = 0x1dc,
    PLAYER_AMMO = 0x274,
    PLAYER_RELOAD_TIME = 0x284,
    SPRITE_FRAME = 0x28
};
extern void hud_wide_canvas_mark(void);
static HleHandler lifted_player_sound, lifted_sprite_next, lifted_sprite_draw, lifted_resolution;

static void h_player_sound(CPU *c)
{
    static struct { uint32_t player; int played; } state[2];
    const uint32_t player = c->eax;
    const unsigned index = rd8(player + PLAYER_INDEX);
    uint32_t original_prompt = rd32(player + PLAYER_RELOAD_PROMPT);
    int request = 0;
    if (index < 2) {
        int empty = (int32_t)rd32(player + PLAYER_AMMO) <= 0;
        if (state[index].player != player || !empty) {
            state[index].player = player;
            state[index].played = 0;
        }
        uint32_t reload_bits = rd32(player + PLAYER_RELOAD_TIME);
        float reload_time;
        memcpy(&reload_time, &reload_bits, sizeof reload_time);
        request = empty && rd32(player + PLAYER_MODE) == 3 &&
                  reload_time <= 0.0f && !state[index].played;
        // Reuse the voice branch and its original checks for grabs and scripted
        // scenes. Keep the stock prompt state outside this sound update.
        wr32(player + PLAYER_RELOAD_PROMPT, request);
        if (request) wr32(player + PLAYER_RELOAD_VOICE_TIMER, 0);
    }
    push32(c, rd32(c->esp - 4));
    lifted_player_sound(c);
    if (index < 2) {
        wr32(player + PLAYER_RELOAD_PROMPT, original_prompt);
        // The original sound branch reloads the countdown to 120.0.
        if (request && rd32(player + PLAYER_RELOAD_VOICE_TIMER) == 0x42f00000u)
            state[index].played = 1;
    }
}

static void h_reload_sprite_next(CPU *c)
{
    // CPlayer::SprMain advances the original red prompt through a blink loop.
    // Keep an opaque frame; its original CalcScene and Draw still run.
    if (rd32(c->esp - 4) == 0x08164e9au) {
        wr32(c->eax + SPRITE_FRAME, 0x41400000u); // 12.0
        return;
    }
    push32(c, rd32(c->esp - 4));
    lifted_sprite_next(c);
}

/* Rev A ConfigMgr_GetRankDataPtr returns config->rank record + 12 bytes.
 * Each of its ten records is score, three letter indices and two game fields. */
static int hotd4_read_high_scores(HighScoreState *out)
{
    uint32_t config=rd32(0x0a711710u);
    uint32_t record=config?rd32(config+0x14):0;
    if(!record)return 0;
    uint32_t words[HOTD4_SCORE_ROWS*HOTD4_SCORE_WORDS];
    for(unsigned i=0;i<sizeof words/sizeof *words;i++)words[i]=rd32(record+12+4*i);
    if(!high_scores_decode(out,words))return 0;
    uint32_t game=rd32(0x0a7117f8u);
    out->playing=game && rd32(game+0x34)==3;
    const char *chapter=getenv("HOTD4_PRACTICE_CHAPTER");
    out->practice=chapter && chapter[0]>='1' && chapter[0]<='6' && !chapter[1];
    /* CPlayer::GetPlayerParam(9, chapter, 0) reads +0x4c+4*chapter.
     * CalcPlayerRankingInfo sums those six counters for the final result.
     * StageParameterInit leaves them intact; GameParameterInit resets them. */
    uint32_t players=rd32(0x0a7117a8u);
    for(unsigned i=0;i<2 && players;i++){
        uint32_t player=rd32(players+0x34+4*i);
        if(!player || rd8(player+0x34)!=i || !rd32(player+0x38))continue;
        uint32_t chapters[6];
        for(unsigned j=0;j<6;j++)chapters[j]=rd32(player+0x4c+4*j);
        out->total[i]=high_scores_run_total(chapters);
        out->player_mask|=1u<<i;
    }
    return 1;
}

static HleHandler lifted_hud_begin_frame, lifted_primitive_begin, lifted_primitive_draw, lifted_boss_sprite_draw;
static unsigned hud_primitive_count;
static struct {
    uint32_t work;
    HudSpriteLayout layout;
} hud_primitives[256]; // Two result cards can submit separate primitives per glyph.
static HudSpriteLayout queuing_layout;
static HleHandler lifted_font_clear, lifted_font_draw, lifted_font_ctor;
static uint32_t subtitle_fonts[32];
static void h_font_ctor(CPU *c)
{
    /* A font's address can be reused by unrelated UI after a scene unload. */
    uint32_t font=A32(0);
    for(unsigned i=0;i<32;i++)if(subtitle_fonts[i]==font)subtitle_fonts[i]=0;
    push32(c,rd32(c->esp-4));lifted_font_ctor(c);
}
static void h_font_clear(CPU *c)
{
    uint32_t caller=rd32(c->esp-4),font=c->eax;
    int subtitle=caller==0x080e3f5bu || caller==0x081733b9u;
    int free_slot=-1;
    for(unsigned i=0;i<32;i++) {
        if(subtitle_fonts[i]==font) {subtitle_fonts[i]=subtitle?font:0;free_slot=-2;break;}
        if(!subtitle_fonts[i])free_slot=(int)i;
    }
    if(subtitle && free_slot>=0)subtitle_fonts[free_slot]=font;
    push32(c,caller);lifted_font_clear(c);
}
static void h_font_draw(CPU *c)
{
    HudSpriteLayout previous=queuing_layout;
    uint32_t font=A32(0);
    for(unsigned i=0;i<32;i++)if(font && subtitle_fonts[i]==font){queuing_layout=HUD_LAYOUT_CENTER;break;}
    push32(c,rd32(c->esp-4));lifted_font_draw(c);
    queuing_layout=previous;
}


static void h_hud_begin_frame(CPU *c)
{
    hud_primitive_count = 0;
    push32(c, rd32(c->esp - 4));
    lifted_hud_begin_frame(c);
}

static void h_primitive_begin(CPU *c)
{
    push32(c, rd32(c->esp - 4));
    lifted_primitive_begin(c);
    // HyPrimitiveBegin returns success and sets the current primitive work pointer.
    if (queuing_layout != HUD_LAYOUT_DEFAULT && c->eax &&
        hud_primitive_count < sizeof hud_primitives / sizeof *hud_primitives) {
        hud_primitives[hud_primitive_count].work = rd32(0x0a73410c);
        hud_primitives[hud_primitive_count++].layout = queuing_layout;
    }
}

static void h_primitive_draw(CPU *c)
{
    HudSpriteLayout layout = HUD_LAYOUT_DEFAULT;
    if (hud_primitive_count) {
        // The renderer replays the work allocated by HyPrimitiveBegin.
        uint32_t work = rd32(rd32(A32(0) + 8));
        for (unsigned i = 0; i < hud_primitive_count; ++i) {
            if (work == hud_primitives[i].work) {
                layout = hud_primitives[i].layout;
                break;
            }
        }
    }
    HudSpriteLayout previous = hud_sprite_layout(layout);
    push32(c, rd32(c->esp - 4));
    lifted_primitive_draw(c);
    hud_sprite_layout(previous);
}

static void h_sprite_draw(CPU *c)
{
    uint32_t caller = rd32(c->esp - 4);
    // Route selection and name entry map both guns across the full sprite canvas.
    if (caller == 0x081800b1u || caller == 0x081605d7u ||
        caller == 0x081605f0u || caller == 0x08160614u)
        hud_wide_canvas_mark();
    HudSpriteLayout previous = queuing_layout;
    // CSpriteScreen::Draw submits the boot artwork on the logical sprite canvas.
    if (caller == 0x081571f8u) queuing_layout = HUD_LAYOUT_SPLASH;
    // Result and final ranking screens use the same full-screen tiled grid.
    else if (caller == 0x08171b5fu || caller == 0x0816a24fu)
        queuing_layout = HUD_LAYOUT_WIDE;
    else if (caller == 0x0816a26au) // Player ranking title
        queuing_layout = HUD_LAYOUT_CENTER;
    // CCreditMgr::Draw queues each player's labels and digits separately.
    // ESI is the player offset in the digit table (0 for P1, 0x18 for P2).
    else if (caller == 0x08078acau || caller == 0x08078ae3u || caller == 0x08078b13u)
        queuing_layout = c->esi ? HUD_LAYOUT_CREDIT_RIGHT : HUD_LAYOUT_CREDIT_LEFT;
    // CPlayer::Draw queues the score digits from right to left.
    else if (caller == 0x08165581u)
        queuing_layout = rd8(c->esi + PLAYER_INDEX) == 1 ? HUD_LAYOUT_SCORE_RIGHT : HUD_LAYOUT_SCORE_LEFT;
    push32(c, caller);
    lifted_sprite_draw(c);
    queuing_layout = previous;
}

static int weapon_disabled_player = -1;

static int draw_inventory_sprite(CPU *c, uint32_t caller)
{
    // CPlayer::Draw submits these sprites with an identity matrix. The
    // offsets are fractions of the final frame relative to the arcade HUD.
    uint32_t sprite_offset;
    float x, y;
    switch (caller) {
    case 0x0816540du: // Life
        sprite_offset = 0x2bc;
        x = 0.009f;
        y = 0.045f;
        break;
    case 0x0816541cu: // Grenades
        sprite_offset = 0x3a0;
        x = 0.010f;
        y = 0.074f;
        break;
    case 0x0816542bu: // Player indicator
        sprite_offset = 0x484;
        x = 0.006f;
        y = -0.030f;
        break;
    default:
        return 0;
    }

    unsigned player = rd8(c->eax - sprite_offset + PLAYER_INDEX);
    if (player >= 2) return 0;

    void *translation = GP(c->edx + 0x30);
    float saved[2];
    memcpy(saved, translation, sizeof saved);
    // Convert to logical sprite coordinates before the 0.75x HUD fit.
    const float moved[2] = {
        saved[0] + (player ? -x : x) * (640.0f / 0.75f),
        saved[1] + y * 480.0f
    };
    memcpy(translation, moved, sizeof moved);
    HudSpriteLayout previous = queuing_layout;
    queuing_layout = player ? HUD_LAYOUT_CREDIT_RIGHT : HUD_LAYOUT_CREDIT_LEFT;
    push32(c, caller);
    const int previous_weapon_player = weapon_disabled_player;
    const uint32_t bank = rd32(c->eax + 8);
    if (caller == 0x0816542bu && (bank == 4u || bank == 5u))
        weapon_disabled_player = (int)player;
    lifted_boss_sprite_draw(c);
    weapon_disabled_player = previous_weapon_player;
    queuing_layout = previous;
    memcpy(translation, saved, sizeof saved);
    return 1;
}

static void h_hud_sprite_draw(CPU *c)
{
    uint32_t caller = rd32(c->esp - 4);
    if (draw_inventory_sprite(c, caller)) return;
    HudSpriteLayout previous = queuing_layout;
    // These weak-point sprites already have an anchor in render pixels.
    // Convert only the translation to the centered HUD canvas so the
    // layout keeps the sprite's size without moving it away from the model.
    if (caller == 0x080da483u || caller == 0x080f8319u) {
        int width, height;
        lindbergh_window_size(&width, &height);
        void *translation = GP(c->edx + 0x30);
        float projected[2];
        memcpy(projected, translation, sizeof projected);
        const float centered[2] = {
            320.0f + (projected[0] - 0.5f * width) * 640.0f / (0.75f * width),
            projected[1] * 480.0f / height
        };
        memcpy(translation, centered, sizeof centered);
        queuing_layout = HUD_LAYOUT_CENTER;
        push32(c, caller);
        lifted_boss_sprite_draw(c);
        queuing_layout = previous;
        memcpy(translation, projected, sizeof projected);
        return;
    }
    // CPlayerSprite::Draw also draws the bell multiplier as separate quads.
    // Its owner stores this specific sprite at +0xd88.
    if (caller == 0x081688d5u) {
        const uint32_t sprite = c->eax - 0x40;
        const uint32_t owner = rd32(sprite + 0x34);
        if (owner && rd32(owner + 0xd88) == sprite)
            queuing_layout = rd8(owner + PLAYER_INDEX) == 1 ? HUD_LAYOUT_CREDIT_RIGHT : HUD_LAYOUT_CREDIT_LEFT;
    }
    // CBossBase::Draw submits the label, fill and empty track as separate sprites.
    if (caller == 0x080da291u || caller == 0x080da2a3u || caller == 0x080da2b5u ||
        caller == 0x080da2e2u || caller == 0x080da2f4u)
        queuing_layout = HUD_LAYOUT_BOSS;
    // CPlayerMgr::SprDraw submits the grab gauge and shake prompt together.
    else if (caller == 0x0816bf3au || caller == 0x0816bf49u ||
             caller == 0x0816bf62u || caller == 0x0816bf71u)
        queuing_layout = HUD_LAYOUT_GRAB;
    // The SEGA panel and CAdvertise's two serial ID rows share the boot canvas.
    else if (caller == 0x08050712u || caller == 0x0805092eu)
        queuing_layout = HUD_LAYOUT_SPLASH;
    // The pushed-down warning is a single sprite with several quads.
    else if (caller == 0x0816bf8cu)
        queuing_layout = HUD_LAYOUT_GRAB;
    // The door control-panel notice is animation bank 8 of the gameplay telop.
    else if (caller == 0x08175c46u && rd32(c->eax + 0xec) == 0u &&
             rd32(c->eax + 8) == 8u)
        queuing_layout = HUD_LAYOUT_GRAB;
    else if (caller == 0x08175c46u && rd32(c->eax + 0xec) == 1u) {
        // CTelopBase also draws gameplay panels. Match the logo telop ID
        // while CAdvertise's startup serial timer is active.
        uint32_t advertise = rd32(0x0a711750);
        if (advertise && (int32_t)rd32(advertise + 0x38) > 0)
            queuing_layout = HUD_LAYOUT_SPLASH;
    }
    push32(c, caller);
    lifted_boss_sprite_draw(c);
    queuing_layout = previous;
}

static void h_route_resolution(CPU *c)
{
    // CBRANCH00::GetSelectSide converts normalized gun aim into sprite units.
    if (rd32(c->esp - 4) == 0x0817ff72u) {
        wr32(c->eax, 640);
        wr32(c->edx, 480);
        c->eax = 480;
        return;
    }
    push32(c, rd32(c->esp - 4));
    lifted_resolution(c);
}

static void install_reload_feedback(void)
{
    const char *sound = "_ZN7CPlayer7SndMainEv";
    const char *next = "_ZN9CCsPlayer13NextFrameSkipEf";
    lifted_player_sound = dispatch_lifted(guest_symbol(sound));
    lifted_sprite_next = dispatch_lifted(guest_symbol(next));
    if (lifted_player_sound) guest_override(sound, h_player_sound);
    if (lifted_sprite_next) guest_override(next, h_reload_sprite_next);
}

static HleHandler lifted_effect_vertices;
typedef struct {
    float source_x, source_y, target_x, target_y, scale_x, scale_y;
} IntroCaptionLayout;

static const IntroCaptionLayout *intro_caption_layout(void)
{
    static const IntroCaptionLayout later = {0.524f, 0.612f, 0.36f, 0.627f, 3.1f, 2.05f};
    static const IntroCaptionLayout earlier = {0.704f, 0.315f, 0.57f, 0.325f, 2.39f, 1.65f};
    const uint32_t event = rd32(0x0a71173cu);
    if (!event) return NULL;

    // Each address is the script's wait command for that opening card.
    switch (rd32(event + 0x40)) {
    case 0x0845d024u: return &later;
    case 0x0846ae80u: return &earlier;
    default: return NULL;
    }
}

static void h_effect_vertices(CPU *c)
{
    uint32_t caller = rd32(c->esp - 4);
    switch (caller) {
    case 0x0816c84eu: { // CCsPlayer sprite quad
        if (weapon_disabled_player < 0) break;
        float (*vertices)[6] = GP(A32(0));
        // The tall X shares an animation bank with the small player label.
        if (fabsf(vertices[2][1] - vertices[0][1]) < 100.0f) break;

        float saved[4][6];
        memcpy(saved, vertices, sizeof saved);
        const float x_scale = 1.33f, y_scale = 1.25f;
        const float pixels_to_hud = 640.0f / 0.75f;
        const float original_center = 0.0545f * pixels_to_hud;
        const float target_center = 0.063f * pixels_to_hud;
        const int right = weapon_disabled_player == 1;
        const float from = right ? 640.0f - original_center : original_center;
        const float to = right ? 640.0f - target_center : target_center;
        for (unsigned i = 0; i < 4; ++i) {
            vertices[i][0] = to + (vertices[i][0] - from) * x_scale;
            vertices[i][1] = 0.333f * 480.0f + (vertices[i][1] - 0.337f * 480.0f) * y_scale;
        }
        push32(c, caller);
        lifted_effect_vertices(c);
        memcpy(vertices, saved, sizeof saved);
        return;
    }
    case 0x08153c8au: // Glare's first pass samples the original scene texture.
    case 0x08161b05u: { // Draw_Blur's horizontal pass
        if (caller == 0x08153c8au && c->edi != 0) break;
        const IntroCaptionLayout *layout = intro_caption_layout();
        if (!layout) break;

        // This opening card is the only artwork in the scene texture.
        // Reframe it before the native blur and separate arcade HUD passes.
        float (*vertices)[6] = GP(A32(0));
        float original[4][2];
        const float left = vertices[0][3], top = vertices[0][4];
        const float du = vertices[1][3] - left, dv = vertices[2][4] - top;
        for (unsigned i = 0; i < 4; ++i) {
            original[i][0] = vertices[i][3];
            original[i][1] = vertices[i][4];
            vertices[i][3] = left + (layout->source_x - layout->target_x / layout->scale_x) * du
                             + (original[i][0] - left) / layout->scale_x;
            vertices[i][4] = top + (layout->source_y - layout->target_y / layout->scale_y) * dv
                             + (original[i][1] - top) / layout->scale_y;
        }
        push32(c, caller);
        lifted_effect_vertices(c);
        for (unsigned i = 0; i < 4; ++i) {
            vertices[i][3] = original[i][0];
            vertices[i][4] = original[i][1];
        }
        return;
    }
    case 0x08161a79u: // Draw_DoF's final composite
    case 0x08161b80u: // Draw_Blur's final composite
    case 0x08161c53u: { // Draw_1Pass
        // The final pass targets a screen at render resolution, but its
        // original quad is fixed at 1280x768. Replace just the right and bottom
        // edges of the four 24-byte KfTextureVertex records. The same arrays
        // are also used by offscreen passes, so restore them after submission.
        int width, height;
        lindbergh_window_size(&width, &height);
        float (*vertices)[6] = GP(A32(0));
        const float right_top = vertices[1][0];
        const float bottom_left = vertices[2][1];
        const float right_bottom = vertices[3][0];
        const float bottom_right = vertices[3][1];
        vertices[1][0] = vertices[3][0] = (float)width;
        vertices[2][1] = vertices[3][1] = (float)height;
        push32(c, caller);
        lifted_effect_vertices(c);
        vertices[1][0] = right_top;
        vertices[2][1] = bottom_left;
        vertices[3][0] = right_bottom;
        vertices[3][1] = bottom_right;
        return;
    }
    }
    push32(c, caller);
    lifted_effect_vertices(c);
}

static HleHandler lifted_result_player_draw, lifted_result_meter_draw;
static HleHandler lifted_ranking_player_draw, lifted_ranking_meter_draw, lifted_ranking_rank_draw;

static void draw_with_hud_layout(CPU *c, HleHandler draw, HudSpriteLayout layout)
{
    HudSpriteLayout previous = queuing_layout;
    queuing_layout = layout;
    push32(c, rd32(c->esp - 4));
    draw(c);
    queuing_layout = previous;
}

static void draw_result_panel(CPU *c, unsigned player, HleHandler draw)
{
    // Panels, meters and rank artwork use one offset for each player.
    draw_with_hud_layout(c, draw, player == 1 ? HUD_LAYOUT_RESULT_RIGHT : HUD_LAYOUT_RESULT_LEFT);
}

static void h_result_player_draw(CPU *c)
{
    draw_result_panel(c, rd32(A32(0) + 0x3c), lifted_result_player_draw);
}

static void h_result_meter_draw(CPU *c)
{
    draw_result_panel(c, rd32(A32(0) + 0x40), lifted_result_meter_draw);
}

static void h_ranking_player_draw(CPU *c)
{
    draw_result_panel(c, rd32(A32(0) + 0x3c), lifted_ranking_player_draw);
}

static void h_ranking_meter_draw(CPU *c)
{
    draw_result_panel(c, rd32(A32(0) + 0x44), lifted_ranking_meter_draw);
}

static void h_ranking_rank_draw(CPU *c)
{
    draw_result_panel(c, rd32(A32(0) + 0x44), lifted_ranking_rank_draw);
}

static void install_wide_hud_layout(void)
{
    const char *effect_vertices = "_Z27HyPrimitiveSetTextureVertexPK15KfTextureVertexii";
    lifted_effect_vertices = dispatch_lifted(guest_symbol(effect_vertices));
    if (lifted_effect_vertices) guest_override(effect_vertices, h_effect_vertices);

    lifted_font_ctor=dispatch_lifted(guest_symbol("_ZN5CFontC9EP6MlTask"));
    if(lifted_font_ctor)guest_override("_ZN5CFontC9EP6MlTask",h_font_ctor);
    lifted_font_clear=dispatch_lifted(guest_symbol("_ZN5CFont5ClearEv"));
    lifted_font_draw=dispatch_lifted(guest_symbol("_ZN5CFont4DrawEv"));
    if(lifted_font_clear && lifted_font_draw) {
        guest_override("_ZN5CFont5ClearEv",h_font_clear);
        guest_override("_ZN5CFont4DrawEv",h_font_draw);
    }
    const char *draw = "_ZN9CCsPlayer12_DrawProcessEjP12KfMatrixNNGL";
    const char *resolution = "_Z13GetResolutionPjS_";
    lifted_sprite_draw = dispatch_lifted(guest_symbol(draw));
    lifted_resolution = dispatch_lifted(guest_symbol(resolution));
    if (lifted_sprite_draw && lifted_resolution) {
        guest_override(draw, h_sprite_draw);
        guest_override(resolution, h_route_resolution);
    }
    const char *boss_draw = "_ZN9CCsPlayer4DrawEP12KfMatrixNNGL";
    const char *begin_frame = "HySystemBeginFrame";
    const char *primitive_begin = "HyPrimitiveBegin";
    const char *primitive_draw = "_Z16_hyDrawPrimitiveP10HyEnvEntryP5HyEnv";
    lifted_boss_sprite_draw = dispatch_lifted(guest_symbol(boss_draw));
    lifted_hud_begin_frame = dispatch_lifted(guest_symbol(begin_frame));
    lifted_primitive_begin = dispatch_lifted(guest_symbol(primitive_begin));
    lifted_primitive_draw = dispatch_lifted(guest_symbol(primitive_draw));
    if (lifted_boss_sprite_draw && lifted_hud_begin_frame && lifted_primitive_begin && lifted_primitive_draw) {
        guest_override(boss_draw, h_hud_sprite_draw);
        guest_override(begin_frame, h_hud_begin_frame);
        guest_override(primitive_begin, h_primitive_begin);
        guest_override(primitive_draw, h_primitive_draw);

        static const struct {
            const char *symbol;
            HleHandler *original;
            HleHandler hook;
        } end_screen_draws[] = {
            {"_ZN13CResultPlayer4DrawEv", &lifted_result_player_draw, h_result_player_draw},
            {"_ZN12CResultMeter4DrawEv", &lifted_result_meter_draw, h_result_meter_draw},
            {"_ZN16CPlRankingPlayer4DrawEv", &lifted_ranking_player_draw, h_ranking_player_draw},
            {"_ZN15CPlRankingMeter4DrawEv", &lifted_ranking_meter_draw, h_ranking_meter_draw},
            {"_ZN17CPlRankingTtlRank4DrawEv", &lifted_ranking_rank_draw, h_ranking_rank_draw},
        };
        for (unsigned i = 0; i < sizeof end_screen_draws / sizeof *end_screen_draws; ++i) {
            const char *symbol = end_screen_draws[i].symbol;
            HleHandler draw = dispatch_lifted(guest_symbol(symbol));
            if (draw) {
                *end_screen_draws[i].original = draw;
                guest_override(symbol, end_screen_draws[i].hook);
            }
        }
    }
}
#endif

#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
/* The cabinet boot service launches hod4testM after this request. The Windows
 * launcher performs that handoff instead; no shell command is executed. */
static HleHandler original_system;
static void h_operator_request(CPU *c)
{
    if (A32(0) && !strcmp(ASTR(0), "touch /tmp/segaboot/test")) {
        fprintf(stderr, "[operator] handing off to the arcade Test Menu\n");
        exit(75);
    }
    original_system(c);
}
#endif

void sega_credit_options(uint32_t init, uint32_t config, uint32_t sound, uint32_t free_play_flag,
                         uint32_t text, uint32_t first, uint32_t last);

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s <hod4M.elf> [game args...]\n"
            "\n"
            "  hod4M.elf is The House of the Dead 4's executable, from a game tree you\n"
            "  can already read. None of it ships here - see the README.\n",
            argv[0]);
        return 2;
    }

    /* Before anything guest-shaped runs, so a fault in lifted code reports
     * the simulated machine rather than a C function name in a generated file. */
    guest_install_crash_handler();

    if (guest_load(argv[1], argc - 1, argv + 1) != 0)
        return 1;

    game_profile_apply(&k_profile);

    /* Give the imports bodies. Everything the toolkit ships is bound here;
     * anything this game needs beyond that gets bound on top, and whatever is
     * still missing aborts naming itself on first call. */
    gameplay_options_load();
    hle_register_all();
    sega_credit_options(0x0831cad4u, 0x0831dc05u, 0x0831d895u, 0x0a737cc7u,
                        0x0831dac2u, 0x080546c9u, 0x08054731u);
    install_blood_effects();
    install_gun_reload_policy();
#if defined(_WIN32) && defined(LINDBERGH_OPENXR)
    install_cutscene_pacing();
#endif
#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
    original_system=g_hle_handlers[HLE_system];
    hle_bind("system",h_operator_request);
    host_cabinet_button_filter(mouse_grab_buttons);
    fprintf(stderr, "[hotd4] mouse grabs: shake the mouse back and forth to escape\n");
#endif
#if defined(_WIN32) && !defined(LINDBERGH_OPENXR)
    install_cutscene_pacing();
    install_practice();
#if defined(_WIN64) && !defined(LINDBERGH_VULKAN)
    host_high_scores_register(hotd4_read_high_scores);
    install_achievements();
    install_reload_feedback();
#endif
#if defined(_WIN64) && !defined(LINDBERGH_VULKAN)
    install_lighting();
    hotd4_install_hd_textures();
    install_hud();
#endif
#endif
    {
        int w, h;
        lindbergh_window_size(&w, &h);
        float aspect = (h > 0) ? ((float)w / (float)h) : 0.0f;
#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
        screen_four_three=fabsf(aspect-4.0f/3.0f)<0.01f;
        if(screen_four_three){
            guest_override("_Z24ConfigMgr_GetScreenRatiov",h_four_three_ratio);
            guest_override("_ZN10CConfigMgr14GetScreenRatioEv",h_four_three_ratio);
        }
#endif
        if ((screen_four_three || fabsf(aspect - (16.0f / 9.0f)) < 0.05f) && guest_override("_Z19GetSpriteResolutionPjS_", h_sprite_res)) {
            install_movie_aspect();
            fprintf(stderr, "[hotd4] %s layout active (%dx%d)\n", screen_four_three?"4:3":"16:9",w,h);
#if defined(_WIN64) && !defined(LINDBERGH_OPENXR) && !defined(LINDBERGH_VULKAN)
            install_wide_hud_layout();
#endif
            hud_widen_enable(screen_four_three?1.0f:0.75f,hud_anchored);
            /* the 2D is laid out in 640x480 pixels from the top left, and
             * drawn into the camera's w x h frame */
            hud_widen_layout(w / 640.0f, w / 640.0f - 1.0f, h / 480.0f, 1.0f - h / 480.0f, w - 8);
        }
    }
    if (guest_override("_Z14GetAdjstGunPosPhS_ttP18GunAdjustValueTypei", h_gun_pos))
        fprintf(stderr, "[hotd4] aim: the gun is where the pointer is\n");
    if (guest_override("_Z16GetAdjstGunAccelPfS_ttP23GunAccelAdjustValueType", h_gun_accel))
        fprintf(stderr, "[hotd4] reload: the reload input shakes the gun\n");
    nn_culling_install("nnSetClipPlane",guest_symbol("nnSetClipPlane"),0x0a767c80u,0x0a767c94u,nn_culling_default_widen());

    CPU cpu;
    guest_init_cpu(&cpu);

    /* The guest never returns here: its exit path is the exit_group syscall,
     * which the runtime turns into the host's exit(). Reaching the line below
     * means the entry point returned, which a Linux _start does not do. */
    dispatch(&cpu, guest_entry());

    fprintf(stderr, "[host] the entry point returned - _start should not.\n");
    return 1;
}
