#ifdef TARGET_VITA

#include <vitasdk.h>
#include <vitaGL.h>

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "../pc_main.h"
#include "../configfile.h"
#include "../cliopts.h"

// TODO: figure out if this shit even works
#ifdef VERSION_EU
# define FRAMERATE 25
#else
# define FRAMERATE 30
#endif

static const uint64_t frametime = 1000000 / FRAMERATE;
static uint32_t vid_width = 960;
static uint32_t vid_height = 544;

static void gfx_vita_init(void) {

}

static void gfx_vita_main_loop(void (*run_one_game_iter)(void)) {
    uint64_t t = 0;
    while (1) {
        t = sceKernelGetProcessTimeWide();
        run_one_game_iter();
        t = sceKernelGetProcessTimeWide() - t;
        if (t < frametime)
            sceKernelDelayThreadCB(frametime - t);
    }
}

static void gfx_vita_get_dimensions(uint32_t *width, uint32_t *height) {
    *width = vid_width;
    *height = vid_height;
}

static void gfx_vita_handle_events(void) {

}

static bool gfx_vita_start_frame(void) {
    vglStartRendering(); // TODO: this has no business being in this file
    return true;
}

static void gfx_vita_swap_buffers_begin(void) {
    vglStopRendering();
}

static void gfx_vita_swap_buffers_end(void) {
}

static double gfx_vita_get_time(void) {
    return 0.0;
}

static void gfx_vita_shutdown(void) {
    vglEnd();
}

struct GfxWindowManagerAPI gfx_vita = {
    gfx_vita_init,
    gfx_vita_main_loop,
    gfx_vita_get_dimensions,
    gfx_vita_handle_events,
    gfx_vita_start_frame,
    gfx_vita_swap_buffers_begin,
    gfx_vita_swap_buffers_end,
    gfx_vita_get_time,
    gfx_vita_shutdown
};

#endif
