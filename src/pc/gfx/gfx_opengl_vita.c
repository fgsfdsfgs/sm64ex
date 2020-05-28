#ifdef TARGET_VITA

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <malloc.h>

#include <vitasdk.h>
#include <vitaGL.h>
#include <stdarg.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "gfx_cc.h"
#include "gfx_rendering_api.h"

#define MAX_INDICES 8192
#define MAX_ATTRIBS 7

void dlog(const char *fmt, ...) {
    FILE *f = fopen("ux0:/data/sm64log.txt", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

// this will be set in sceGxmShaderPatcherCreateFragmentProgram if the shader's opt_alpha is 1
static const struct SceGxmBlendInfo vgl_blend = {
    .colorMask = SCE_GXM_COLOR_MASK_R | SCE_GXM_COLOR_MASK_G | SCE_GXM_COLOR_MASK_B,
    .colorFunc = SCE_GXM_BLEND_FUNC_ADD,
    .alphaFunc = SCE_GXM_BLEND_FUNC_NONE,
    .colorSrc  = SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
    .colorDst  = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .alphaSrc  = SCE_GXM_BLEND_FACTOR_ONE,
    .alphaDst  = SCE_GXM_BLEND_FACTOR_ZERO,
};

struct GxmShader {
    const SceGxmProgram *prog;
    SceGxmShaderPatcherId id;
};

struct ShaderProgram {
    uint32_t shader_id;
    uint8_t num_inputs;
    uint8_t num_floats;
    uint8_t num_attribs;
    bool used_textures[2];

    struct GxmShader gsh[2]; // vert, frag
    SceGxmVertexProgram *vprog;
    SceGxmFragmentProgram *fprog;
    SceGxmVertexAttribute attrib[MAX_ATTRIBS];
    SceGxmVertexStream stream;
    
    bool debug;
};

// HACK: vitaGL's already initialized one for us, what's not to like?
extern SceGxmShaderPatcher *gxm_shader_patcher;
// HACK: these are also defined in vitaGL
extern SceGxmContext *gxm_context;
extern SceGxmMultisampleMode msaa_mode;
extern GLuint cur_program; // and this

static struct ShaderProgram shader_program_pool[64];
static uint8_t shader_program_pool_size;

static struct ShaderProgram *cur_shader = NULL;

static uint16_t *vgl_indices = NULL;

static bool gfx_vitagl_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_vitagl_unload_shader(struct ShaderProgram *old_prg) {
    if (!old_prg || old_prg == cur_shader)
        cur_shader = NULL;
}

static void gfx_vitagl_load_shader(struct ShaderProgram *new_prg) {
    cur_shader = new_prg;
    sceGxmSetVertexProgram(gxm_context, new_prg->vprog);
    sceGxmSetFragmentProgram(gxm_context, new_prg->fprog);
}

static void read_shader(struct GxmShader *gsh, const char *fname) {
    SceUID fd = sceIoOpen(fname, SCE_O_RDONLY, 0777);
    assert(fd >= 0 && "could not open shader file");

    // get the size
    int64_t size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    uint8_t *data = malloc(size);
    assert(data && "could not allocate space for shader");

    sceIoRead(fd, data, size);
    sceIoClose(fd);

    gsh->prog = (SceGxmProgram*)data;
    sceGxmShaderPatcherRegisterProgram(gxm_shader_patcher, gsh->prog, &gsh->id);
    gsh->prog = sceGxmShaderPatcherGetProgramFromId(gsh->id);
}

static void cache_shader(struct ShaderProgram *prg) {
    static char fname[256];

    snprintf(fname, sizeof(fname), "app0:/shaders/%08x_v.gxp", (unsigned int)prg->shader_id);
    read_shader(prg->gsh + 0, fname);
    snprintf(fname, sizeof(fname), "app0:/shaders/%08x_f.gxp", (unsigned int)prg->shader_id);
    read_shader(prg->gsh + 1, fname);

    prg->vprog = NULL;
    prg->fprog = NULL;
}

static inline void setup_attrib(struct ShaderProgram *prg, const size_t idx, const char *name, const size_t count, const size_t ofs) {
    dlog("setup_attrib %08x %u `%s` %u %u (%u)", prg->shader_id, (unsigned)idx, name, (unsigned)count, (unsigned)ofs, (unsigned)ofs * sizeof(GLfloat));
    const SceGxmProgramParameter *param = sceGxmProgramFindParameterByName(prg->gsh[0].prog, name);
    prg->attrib[idx].streamIndex = 0;
    prg->attrib[idx].offset = ofs * sizeof(GLfloat);
    prg->attrib[idx].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    prg->attrib[idx].regIndex = sceGxmProgramParameterGetResourceIndex(param);
    prg->attrib[idx].componentCount = count;
}

static struct ShaderProgram *gfx_vitagl_create_and_load_new_shader(uint32_t shader_id) {
    uint8_t c[2][4];
    for (int i = 0; i < 4; i++) {
        c[0][i] = (shader_id >> (i * 3)) & 7;
        c[1][i] = (shader_id >> (12 + i * 3)) & 7;
    }
    bool opt_alpha = (shader_id & SHADER_OPT_ALPHA) != 0;
    bool opt_fog = (shader_id & SHADER_OPT_FOG) != 0;
    bool used_textures[2] = {0, 0};
    int num_inputs = 0;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            if (c[i][j] >= SHADER_INPUT_1 && c[i][j] <= SHADER_INPUT_4) {
                if (c[i][j] > num_inputs) {
                    num_inputs = c[i][j];
                }
            }
            if (c[i][j] == SHADER_TEXEL0 || c[i][j] == SHADER_TEXEL0A) {
                used_textures[0] = true;
            }
            if (c[i][j] == SHADER_TEXEL1) {
                used_textures[1] = true;
            }
        }
    }

    size_t num_floats = 4;

    if (used_textures[0] || used_textures[1])
        num_floats += 2;
    if (opt_fog)
        num_floats += 4;
    for (int i = 0; i < num_inputs; i++)
        num_floats += opt_alpha ? 4 : 3;

    size_t cnt = 0, ofs = 0;

    struct ShaderProgram *prg = &shader_program_pool[shader_program_pool_size++];
    prg->shader_id = shader_id;

    // load shader binary from romfs
    cache_shader(prg);

    // attribs are all sequential

    setup_attrib(prg, cnt++, "aPosition", 4, ofs);
    ofs += 4;

    if (used_textures[0] || used_textures[1]) {
        setup_attrib(prg, cnt++, "aTexCoord", 2, ofs);
        ofs += 2;
    }

    if (opt_fog) {
        setup_attrib(prg, cnt++, "aFog", 4, ofs);
        ofs += 4;
    }

    char name[] = "aInput0";
    const int asz = opt_alpha ? 4 : 3;
    for (int i = 0; i < num_inputs; i++) {
        name[6] = '1' + i;
        setup_attrib(prg, cnt++, name, asz, ofs);
        ofs += asz;
    }

    // setup the vertex stream
    prg->stream.stride = ofs * sizeof(GLfloat);
    prg->stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;

    prg->num_inputs = num_inputs;
    prg->used_textures[0] = used_textures[0];
    prg->used_textures[1] = used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    dlog("setup_shader %08x inp %d cnt %d numflt %d ofs %d\n", prg->shader_id, (int)num_inputs, (int)cnt, (int)num_floats, (int)ofs);

    // build the vertex shader:
    // num_attribs attributes, but only 1 vertex stream
    sceGxmShaderPatcherCreateVertexProgram(
        gxm_shader_patcher,
        prg->gsh[0].id, prg->attrib, prg->num_attribs,
        &prg->stream, 1, &prg->vprog
    );

    // build the fragment shader
    sceGxmShaderPatcherCreateFragmentProgram(
        gxm_shader_patcher,
        prg->gsh[1].id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
        msaa_mode, opt_alpha ? &vgl_blend : NULL, prg->gsh[0].prog, &prg->fprog
    );

    gfx_vitagl_load_shader(prg);

    return prg;
}

static struct ShaderProgram *gfx_vitagl_lookup_shader(uint32_t shader_id) {
    for (size_t i = 0; i < shader_program_pool_size; i++) {
        if (shader_program_pool[i].shader_id == shader_id) {
            return &shader_program_pool[i];
        }
    }
    return NULL;
}

static void gfx_vitagl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static GLuint gfx_vitagl_new_texture(void) {
    GLuint ret;
    glGenTextures(1, &ret);
    return ret;
}

static void gfx_vitagl_select_texture(int tile, GLuint texture_id) {
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, texture_id);
}

static void gfx_vitagl_upload_texture(uint8_t *rgba32_buf, int width, int height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    if (val & G_TX_CLAMP) {
        return GL_CLAMP;
    }
    return (val & G_TX_MIRROR) ? GL_MIRROR_CLAMP_EXT : GL_REPEAT;
}

static void gfx_vitagl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    const GLenum filter = linear_filter ? GL_LINEAR : GL_NEAREST;
    glActiveTexture(GL_TEXTURE0 + tile);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
}

static void gfx_vitagl_set_depth_test(bool depth_test) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_vitagl_set_depth_mask(bool z_upd) {
    glDepthMask(z_upd ? GL_TRUE : GL_FALSE);
}

static void gfx_vitagl_set_zmode_decal(bool zmode_decal) {
    if (zmode_decal) {
        glPolygonOffset(-2, -2);
        glEnable(GL_POLYGON_OFFSET_FILL);
    } else {
        glPolygonOffset(0, 0);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

static void gfx_vitagl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

static void gfx_vitagl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

static void gfx_vitagl_set_use_alpha(bool use_alpha) {
    // this is set per shader
}

// vglVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLuint count, const GLvoid *pointer) {
static void gfx_vitagl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (!cur_shader->debug) {
        cur_shader->debug = true;
        dlog("new shader %08x: attrib %d floats %d tex %d alpha %d",
          cur_shader->shader_id, (int)cur_shader->num_attribs, (int)cur_shader->num_floats,
          cur_shader->used_textures[0] + cur_shader->used_textures[1],
          cur_shader->shader_id & SHADER_OPT_ALPHA);
    }
    const size_t count = buf_vbo_num_tris*3;
    vglVertexAttribPointer(0, cur_shader->num_floats, GL_FLOAT, GL_FALSE, 0, count, buf_vbo);
    cur_program = 1; // HACK HACK HACK: make vitaGL think we're using a custom shader (which we are)
    vglDrawObjects(GL_TRIANGLES, count, false);
    cur_program = 0; // HACK HACK HACK: reset it back to the default shader
}

static void gfx_vitagl_init(void) {
    dlog("vglIndexAlloc");
    // generate an index buffer
    // vertices that we get are always sequential, so it will stay constant
    vgl_indices = malloc(sizeof(uint16_t) * MAX_INDICES);
    for (uint16_t i = 0; i < MAX_INDICES; ++i) vgl_indices[i] = i;

    vglUseVramForUSSE(GL_FALSE);
    vglInit(0x800000);
    dlog("\n\n--------------\n\n");
    dlog("vglInit");
    vglUseVram(GL_TRUE);
    vglWaitVblankStart(GL_TRUE);
    vglUseExtraMem(GL_FALSE);
    dlog("vglInitEnd");

    vglIndexPointerMapped(vgl_indices);
    dlog("vglIndexPointerMapped");

    glDepthFunc(GL_LEQUAL);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); /./ this is set per shader
}

static void gfx_vitagl_start_frame(void) {
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE); // Must be set to clear Z-buffer
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
}

static void gfx_vitagl_shutdown(void) {
}

struct GfxRenderingAPI gfx_opengl_api = {
    gfx_vitagl_z_is_from_0_to_1,
    gfx_vitagl_unload_shader,
    gfx_vitagl_load_shader,
    gfx_vitagl_create_and_load_new_shader,
    gfx_vitagl_lookup_shader,
    gfx_vitagl_shader_get_info,
    gfx_vitagl_new_texture,
    gfx_vitagl_select_texture,
    gfx_vitagl_upload_texture,
    gfx_vitagl_set_sampler_parameters,
    gfx_vitagl_set_depth_test,
    gfx_vitagl_set_depth_mask,
    gfx_vitagl_set_zmode_decal,
    gfx_vitagl_set_viewport,
    gfx_vitagl_set_scissor,
    gfx_vitagl_set_use_alpha,
    gfx_vitagl_draw_triangles,
    gfx_vitagl_init,
    gfx_vitagl_start_frame,
    gfx_vitagl_shutdown
};

#endif // TARGET_VITA
