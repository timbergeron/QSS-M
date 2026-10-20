#!/usr/bin/env python3
"""Exercise the production radius helper with real model types and sanitizers."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
renderer = (ROOT / 'Quake/r_alias.c').read_text()
mesh = (ROOT / 'Quake/gl_mesh.c').read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index('\n}', start) + 2] + '\n'


source = r'''
#include "quakedef.h"
#include <assert.h>
#undef main
typedef struct { bonepose_t *bonestate; } lerpdata_t;
static int square_roots;
static double counted_sqrt(double value) { square_roots++; return sqrt(value); }
#define sqrt counted_sqrt
'''
source += function(mesh, 'void Matrix3x4_RM_Transform4(')
source += function(renderer, 'static qboolean R_AliasModelIsMD5(')
source += function(renderer, 'static float R_CalculateAliasModelRadius(')
source += r'''
#undef sqrt

/* Two frame descriptors plus an unused third pose for an MDL frame group. */
static aliashdr_t *fixture(int type, int vertices, size_t stride) {
    size_t header_size = sizeof(aliashdr_t) + sizeof(maliasframedesc_t);
    aliashdr_t *h = calloc(1, header_size + 3 * vertices * stride);
    assert(h);
    h->poseverttype = type;
    h->numframes = 2;
    h->numverts = vertices;
    h->vertexes = header_size;
    h->frames[0].firstpose = 0;
    h->frames[0].numposes = 1;
    h->frames[1].firstpose = 1;
    h->frames[1].numposes = 2;
    h->scale[0] = h->scale[1] = h->scale[2] = 1;
    return h;
}

static float radius(aliashdr_t *h, int frame) {
    entity_t e = {0};
    qboolean md5 = true;
    e.frame = frame;
    float result = R_CalculateAliasModelRadius(h, &e, NULL, &md5);
    assert(!md5);
    return result;
}

int main(void) {
    aliashdr_t *mdl = fixture(PV_QUAKE1, 2, sizeof(trivertx_t));
    trivertx_t *v = (trivertx_t *)((byte *)mdl + mdl->vertexes);
    v[0].v[0] = 3; v[0].v[1] = 4;
    v[2].v[0] = 5; v[2].v[1] = 12;
    v[4].v[0] = 200; /* Existing behavior uses the group's first pose. */
    assert(radius(mdl, 0) == 5 && square_roots == 2);
    assert(radius(mdl, 1) == 13 && square_roots == 4);
    for (int i = 0; i < 1000; i++) {
        assert(radius(mdl, 0) == 5 && radius(mdl, 1) == 13);
    }
    assert(square_roots == 4);
    assert(radius(mdl, -1) == 5 && radius(mdl, 2) == 5);
    assert(square_roots == 4);

    /* A relocated cache block retains offsets and its own cached results. */
    size_t bytes = mdl->vertexes + 6 * sizeof(*v);
    aliashdr_t *moved = malloc(bytes);
    assert(moved);
    memcpy(moved, mdl, bytes);
    free(mdl);
    assert(radius(moved, 1) == 13 && square_roots == 4);

    /* Reload in the same allocation: zeroed model storage cannot retain radii. */
    memset(moved, 0, bytes);
    moved->poseverttype = PV_QUAKE1; moved->numframes = 2; moved->numverts = 2;
    moved->vertexes = sizeof(aliashdr_t) + sizeof(maliasframedesc_t);
    moved->scale[0] = moved->scale[1] = moved->scale[2] = 1;
    v = (trivertx_t *)((byte *)moved + moved->vertexes);
    v[0].v[0] = 8; v[0].v[1] = 15;
    assert(radius(moved, 0) == 17 && square_roots == 6);
    free(moved);

    mdl = fixture(PV_QUAKE1, 2, sizeof(trivertx_t));
    mdl->boundingradius = 42;
    assert(radius(mdl, 0) == 42 && radius(mdl, 1) == 42 && square_roots == 6);
    free(mdl);
    mdl = fixture(PV_QUAKE1, 2, sizeof(trivertx_t));
    assert(radius(mdl, 0) == 1 && radius(mdl, 0) == 1 && square_roots == 8);
    free(mdl);

    /* Independent surfaces/models must not share a radius, even at frame zero. */
    aliashdr_t *md3 = fixture(PV_QUAKE3, 2, sizeof(meshxyz_md3_t));
    aliashdr_t *other = fixture(PV_QUAKE3, 2, sizeof(meshxyz_md3_t));
    meshxyz_md3_t *q = (meshxyz_md3_t *)((byte *)md3 + md3->vertexes);
    meshxyz_md3_t *q2 = (meshxyz_md3_t *)((byte *)other + other->vertexes);
    q[0].xyz[0] = 3 * 64; q[0].xyz[1] = 4 * 64;
    q[2].xyz[0] = 5 * 64; q[2].xyz[1] = 12 * 64;
    q2[0].xyz[0] = -8 * 64; q2[0].xyz[1] = 15 * 64;
    assert(radius(md3, 0) == 5 && radius(md3, 1) == 13 && radius(other, 0) == 17);
    int before = square_roots;
    assert(radius(md3, 1) == 13 && radius(other, 0) == 17 && square_roots == before);
    free(other);

    /* Compare both morph formats against independent scans with nonuniform
     * scales, signed MD3 coordinates, MDL offsets, and nonintegral radii. */
    mdl = fixture(PV_QUAKE1, 2, sizeof(trivertx_t));
    v = (trivertx_t *)((byte *)mdl + mdl->vertexes);
    for (int i = 0; i < 256; i++) {
        float expected_mdl = 1, expected_md3 = 1;
        mdl->frames[0].outline_radius = md3->frames[0].outline_radius = 0;
        for (int j = 0; j < 3; j++) {
            mdl->scale[j] = md3->scale[j] = (j + 1) * 0.125f;
            mdl->scale_origin[j] = j * 13.25f - 27;
            for (int k = 0; k < 2; k++) {
                v[k].v[j] = (i * 17 + k * 43 + j * 61) & 255;
                q[k].xyz[j] = (i * 127 + k * 4093 + j * 8081) % 65536 - 32768;
            }
        }
        for (int k = 0; k < 2; k++) {
            float x = v[k].v[0] * mdl->scale[0] + mdl->scale_origin[0];
            float y = v[k].v[1] * mdl->scale[1] + mdl->scale_origin[1];
            float z = v[k].v[2] * mdl->scale[2] + mdl->scale_origin[2];
            float d = sqrt(x*x + y*y + z*z);
            if (d > expected_mdl) expected_mdl = d;
            x = q[k].xyz[0] * (1.0f/64.0f) * md3->scale[0];
            y = q[k].xyz[1] * (1.0f/64.0f) * md3->scale[1];
            z = q[k].xyz[2] * (1.0f/64.0f) * md3->scale[2];
            d = sqrt(x*x + y*y + z*z);
            if (d > expected_md3) expected_md3 = d;
        }
        assert(radius(mdl, 0) == expected_mdl && radius(mdl, 0) == expected_mdl);
        assert(radius(md3, 0) == expected_md3 && radius(md3, 0) == expected_md3);
    }
    free(mdl); free(md3);

    /* Same frame and bone-state pointer, different live/blended poses. */
    aliashdr_t *iqm = fixture(PV_IQM, 1, sizeof(iqmvert_t));
    iqmvert_t *s = (iqmvert_t *)((byte *)iqm + iqm->vertexes);
    bonepose_t bones[2] = {{{1,0,0,0, 0,1,0,0, 0,0,1,0}},
                           {{4,0,0,0, 0,1,0,0, 0,0,1,0}}};
    lerpdata_t lerp = {bones}; entity_t e = {0}; qboolean md5;
    s->xyz[0] = 3; s->xyz[1] = 4;
    s->weight[0] = 1;
    assert(R_CalculateAliasModelRadius(iqm, &e, &lerp, &md5) == 5 && md5);
    bones[0].mat[0] = 2;
    assert(R_CalculateAliasModelRadius(iqm, &e, &lerp, &md5) == (float)sqrt(52) && md5);
    s->idx[1] = 1; s->weight[0] = s->weight[1] = 0.5f;
    assert(R_CalculateAliasModelRadius(iqm, &e, &lerp, &md5) == (float)sqrt(97) && md5);
    /* A skeletal radius is never retained, however often it is asked for. */
    assert(iqm->frames[0].outline_radius == 0);

    /* With no bone state those same vertices are static, so the radius is
     * retained and the weight verdict is reached once instead of per call. */
    lerp.bonestate = NULL;
    assert(R_CalculateAliasModelRadius(iqm, &e, &lerp, &md5) == 5 && md5);
    assert(iqm->frames[0].outline_radius == 5);
    before = square_roots;
    assert(R_CalculateAliasModelRadius(iqm, &e, NULL, &md5) == 5 && md5);
    assert(square_roots == before);
    free(iqm);

    /* The verdict follows the vertex data: zeroed weights do not sum to one. */
    iqm = fixture(PV_IQM, 1, sizeof(iqmvert_t));
    s = (iqmvert_t *)((byte *)iqm + iqm->vertexes);
    s->xyz[0] = 3; s->xyz[1] = 4;
    assert(R_CalculateAliasModelRadius(iqm, &e, NULL, &md5) == 5 && !md5);
    assert(iqm->md5_weighted == 2 && iqm->frames[0].outline_radius == 5);
    free(iqm);
    puts("PASS: cached MDL/MD3/boneless-iqm radii, grouped/invalid frames, independent surfaces, relocation/reload, live skeletal poses, and a once-per-model weight verdict");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='qssm-radius-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True))
    cc = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run([*cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'Quake'), str(work / 'test.c'),
                    '-o', str(work / 'test'), *flags, '-lm'], check=True)
    subprocess.run([str(work / 'test')], check=True)
