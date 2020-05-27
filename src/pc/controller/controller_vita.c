#ifdef TARGET_VITA

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <vitasdk.h>

// Analog camera movement by Pathétique (github.com/vrmiguel), y0shin and Mors
// Contribute or communicate bugs at github.com/vrmiguel/sm64-analog-camera

#include <ultra64.h>

#include "controller_api.h"
#include "controller_vita.h"
#include "../configfile.h"

#include "game/level_update.h"

#define MAX_JOYBINDS 32

extern int16_t rightx;
extern int16_t righty;

#ifdef BETTERCAMERA
int mouse_x;
int mouse_y;

extern u8 newcam_mouse;
#endif

static const u32 button_map[] = {
    SCE_CTRL_CROSS,
    SCE_CTRL_SQUARE,
    SCE_CTRL_CIRCLE,
    SCE_CTRL_TRIANGLE,
    SCE_CTRL_L1,
    SCE_CTRL_R1,
    SCE_CTRL_START,
    SCE_CTRL_SELECT,
    SCE_CTRL_UP,
    SCE_CTRL_DOWN,
    SCE_CTRL_LEFT,
    SCE_CTRL_RIGHT,
};
#define NUM_BUTTONS (sizeof(button_map) / sizeof(button_map[0]))

static SceCtrlData vita_pad;
static u32 num_joy_binds = 0;
static u32 joy_binds[MAX_JOYBINDS][2];
static bool joy_buttons[NUM_BUTTONS] = { false };
static u32 last_joybutton = VK_INVALID;

static inline u32 vk_to_vita(const u32 btn) {
    const u32 idx = btn - VK_BASE_VITA_GAMEPAD;
    if (idx < NUM_BUTTONS)
        return button_map[idx];
    return 0;
}

static inline void controller_add_binds(const u32 mask, const u32 *btns) {
    for (u32 i = 0; i < MAX_BINDS; ++i) {
        if (btns[i] >= VK_BASE_VITA_GAMEPAD && btns[i] <= VK_BASE_VITA_GAMEPAD + VK_SIZE) {
            const u32 mapped = vk_to_vita(btns[i]);
            if (num_joy_binds < MAX_JOYBINDS && mapped) {
                joy_binds[num_joy_binds][0] = btns[i] - VK_BASE_VITA_GAMEPAD;
                joy_binds[num_joy_binds][1] = mask;
                ++num_joy_binds;
            }
        }
    }
}

static void controller_vita_bind(void) {
    bzero(joy_binds, sizeof(joy_binds));
    num_joy_binds = 0;

    controller_add_binds(A_BUTTON,     configKeyA);
    controller_add_binds(B_BUTTON,     configKeyB);
    controller_add_binds(Z_TRIG,       configKeyZ);
    controller_add_binds(U_CBUTTONS,   configKeyCUp);
    controller_add_binds(L_CBUTTONS,   configKeyCLeft);
    controller_add_binds(D_CBUTTONS,   configKeyCDown);
    controller_add_binds(R_CBUTTONS,   configKeyCRight);
    controller_add_binds(L_TRIG,       configKeyL);
    controller_add_binds(R_TRIG,       configKeyR);
    controller_add_binds(START_BUTTON, configKeyStart);
}

static void controller_vita_init(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    controller_vita_bind();
}

static void controller_vita_read(OSContPad *pad) {
    sceKernelPowerTick(0);
    sceCtrlPeekBufferPositive(0, &vita_pad, 1);

    for (u32 i = 0; i < NUM_BUTTONS; ++i) {
        const bool new = vita_pad.buttons & button_map[i];
        const bool pressed = !joy_buttons[i] && new;
        joy_buttons[i] = new;
        if (pressed) last_joybutton = i;
    }

    for (u32 i = 0; i < num_joy_binds; ++i)
        if (joy_buttons[joy_binds[i][0]])
            pad->button |= joy_binds[i][1];

    pad->stick_x = (s8)((s32)vita_pad.lx - 128);
    pad->stick_y = (s8)((s32)vita_pad.ly - 128);

    rightx = ((s32)vita_pad.rx - 128) * 0x100;
    rightx = ((s32)vita_pad.ry - 128) * 0x100;

    if (rightx < -0x4000) pad->button |= L_CBUTTONS;
    if (rightx > 0x4000) pad->button |= R_CBUTTONS;
    if (righty < -0x4000) pad->button |= U_CBUTTONS;
    if (righty > 0x4000) pad->button |= D_CBUTTONS;
}

static u32 controller_vita_rawkey(void) {
    if (last_joybutton != VK_INVALID) {
        const u32 ret = last_joybutton;
        last_joybutton = VK_INVALID;
        return ret;
    }
    return VK_INVALID;
}

static void controller_vita_shutdown(void) {
}

struct ControllerAPI controller_vita = {
    VK_BASE_VITA_GAMEPAD,
    controller_vita_init,
    controller_vita_read,
    controller_vita_rawkey,
    controller_vita_bind,
    controller_vita_shutdown
};

#endif // TARGET_VITA
