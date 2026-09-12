#!/usr/bin/env python3
"""Check that teleporter scissoring contains every shader texture sample.

Compiles the production projection, coplanar collection, and scissor helpers.
Requires a C compiler and SDL2 headers; no GPU or game data needed.
"""

from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
warp = (ROOT / 'Quake/gl_warp.c').read_text()
math = (ROOT / 'Quake/mathlib.c').read_text()
world = (ROOT / 'Quake/r_world.c').read_text()
rmain = (ROOT / 'Quake/gl_rmain.c').read_text()


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index('\n}', start) + 2] + '\n'


source = r'''
#include "quakedef.h"
#include <assert.h>
refdef_t r_refdef;
client_state_t cl;
cvar_t r_novis;
mplane_t frustum[5];
int r_frustumplanes;
static mleaf_t leaves[5];
static int decompressions;
mleaf_t *r_viewleaf = leaves + 4;
mleaf_t *Mod_PointInLeaf(vec3_t p, qmodel_t *model) {
    return &leaves[(p[0] > 0 ? 2 : 0) + (p[2] > -100 ? 1 : 0)];
}
byte *Mod_NoVisPVS(qmodel_t *model) { assert(!"unexpected novis"); return NULL; }
byte *Mod_LeafPVS(mleaf_t *leaf, qmodel_t *model) {
    static byte bits;
    assert(leaf->contents != CONTENTS_SOLID);
    decompressions++;
    bits = 1 << (leaf-leaves);
    return &bits;
}
static int rect[4];
void GLAPIENTRY glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    rect[0]=x; rect[1]=y; rect[2]=w; rect[3]=h;
}
void GLAPIENTRY glEnable(GLenum cap) { assert(cap==GL_SCISSOR_TEST); }
float VectorNormalize(vec3_t v) {
    float length=sqrtf(DotProduct(v,v));
    if(length) VectorScale(v,1/length,v);
    return length;
}
'''
source += warp[warp.index('#define MAX_TELEPORT_PLANES'):warp.index('extern float r_fovx')]
source += function(math, 'void Matrix4_Transform4(')
source += function(math, 'void Matrix4_Multiply(')
source += function(math, 'qboolean Matrix4_Invert(')
source += function(math, 'void Matrix4_ProjectionMatrix(')
source += function(math, 'void VectorScale (')
source += function(rmain, 'qboolean R_CullBox (')
source += r'''
struct rscenecache_s {
    msurface_t **teleports;
    size_t maxteleports, numteleports;
    qboolean teleportscomplete;
};
static qboolean RSceneCache_SurfaceListReserve(msurface_t ***surfs, size_t *max, size_t needed) {
    return needed <= *max;
}
'''
source += function(world, 'static void RSceneCache_AddTeleportSurface(')

for name in ('R_TeleportPlane', 'R_TeleportFindPlane', 'R_TeleportScreenBounds',
             'R_TeleportMergePVS', 'R_TeleportCollect', 'R_TeleportScissorBounds',
             'R_TeleportScissor', 'R_TeleportFrustum', 'R_TeleportClip'):
    declaration = ('static int ' if name == 'R_TeleportFindPlane' else 'static void ') + name + ' ('
    source += function(warp, declaration)
source += r'''
static unsigned rng=0x17efa341;
static float randomf(void) {
    rng=rng*1664525u+1013904223u;
    return (rng>>8)*(1.0f/16777216.0f);
}
static glpoly_t poly;
static mplane_t surfaceplane;
static texture_t texture;
static mtexinfo_t texinfo;
static msurface_t surf;

static void quad(float x, float y, float z, float w, float h) {
    const float corners[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(int i=0;i<4;i++) {
        poly.verts[i][0]=x+corners[i][0]*w;
        poly.verts[i][1]=y+corners[i][1]*h;
        poly.verts[i][2]=z;
    }
}

int main(void) {
    byte vis[MAX_TELEPORT_PLANES * 2] = {0};
    teleport_vis = vis; teleport_pvsbytes = 1;
    for (int i=0;i<5;i++) leaves[i].contents = CONTENTS_EMPTY;
    poly.numverts=4; surf.polys=&poly; surf.plane=&surfaceplane;
    surfaceplane.normal[2]=1;
    surf.flags=SURF_DRAWTELE; surf.texinfo=&texinfo; texinfo.texture=&texture;
    // A failed/unsupported normal map must stay entirely in the cached water
    // batches, including when another texture on the same map is supported.
    msurface_t *faces[2];
    struct rscenecache_s cache={faces,2,0,true};
    RSceneCache_AddTeleportSurface(&cache,&surf);
    assert(cache.numteleports==0 && cache.teleportscomplete);
    texture.tele_normal=(void*)1;
    RSceneCache_AddTeleportSurface(&cache,&surf);
    assert(cache.numteleports==1 && faces[0]==&surf);
    texture.tele_normal=NULL;
    RSceneCache_AddTeleportSurface(&cache,&surf);
    assert(cache.numteleports==1 && cache.teleportscomplete);
    // Collection must also ignore that unsupported face.
    quad(-40,-20,-100,10,10); R_TeleportCollect(&surf,NULL);
    assert(!teleport_numplanes && !decompressions);
    texture.tele_normal=(void*)1;
    teleport_screenmatrix[0]=teleport_screenmatrix[5]=1;
    teleport_screenmatrix[11]=-1; // clip.w = -world.z

    // Both coplanar faces contribute, even when separated on screen.
    quad(-40,-20,-100,10,10); R_TeleportCollect(&surf,NULL);
    assert(decompressions==2);
    for (int i=0;i<32;i++) R_TeleportCollect(&surf,NULL);
    assert(decompressions==2);
    quad(30,20,-100,10,10); R_TeleportCollect(&surf,NULL);
    assert(decompressions==4);
    for (int i=0;i<32;i++) R_TeleportCollect(&surf,NULL);
    assert(decompressions==4);
    assert(teleport_numplanes==1);
    assert(teleport_planes[0].screenmins[0]<.31f);
    assert(teleport_planes[0].screenmaxs[0]>.69f);

    assert(vis[0] == (1|4) && vis[1] == (2|8));
    // The same leaf still needs merging into a DIFFERENT plane's targets.
    quad(30,20,-101,10,10); R_TeleportCollect(&surf,NULL);
    assert(teleport_numplanes==2 && decompressions==6);
    assert(vis[2]==4 && vis[3]==4);
    // A probe in solid must use viewer visibility, not return the whole map.
    leaves[0].contents = CONTENTS_SOLID;
    vis[0] = 0;
    quad(-40,-20,-100,10,10); R_TeleportCollect(&surf,NULL);
    assert(vis[0] == 16);

    // Reusing target slot zero next frame must not reuse its last-leaf memo.
    teleport_numplanes=0;
    memset(vis,0,sizeof(vis));
    int previous=decompressions;
    R_TeleportCollect(&surf,NULL);
    assert(decompressions==previous+2 && vis[0]==16 && vis[1]==2);

    // Eye-plane crossings must disable cropping, including transformed faces.
    float mins[2],maxs[2];
    quad(-1,-1,-1,2,2); poly.verts[0][2]=1;
    R_TeleportScreenBounds(&surf,NULL,mins,maxs);
    assert(mins[0]==0 && mins[1]==0 && maxs[0]==1 && maxs[1]==1);

    size_t samples=0;
    for(int test=0;test<12000;test++) {
        mat4_t matrix={0};
        float angle=randomf()*6.2831853f, scale=.1f+randomf()*3;
        matrix[0]=matrix[5]=cosf(angle)*scale;
        matrix[1]=sinf(angle)*scale; matrix[4]=-matrix[1];
        matrix[10]=scale; matrix[15]=1;
        matrix[12]=(randomf()-.5f)*100;
        matrix[13]=(randomf()-.5f)*100;
        matrix[14]=(randomf()-.5f)*100;
        quad((randomf()-.5f)*200,(randomf()-.5f)*200,
             -1-randomf()*200,1+randomf()*100,1+randomf()*100);
        teleport_width=1+(int)(randomf()*1920);
        teleport_height=1+(int)(randomf()*1080);
        R_TeleportScreenBounds(&surf,matrix,teleport_planes[0].screenmins,
                              teleport_planes[0].screenmaxs);
        for(int pass=0;pass<2;pass++) {
            R_TeleportScissor(teleport_planes,pass);
            float strength=pass?TELEPORT_REFLECT_STRENGTH:TELEPORT_REFRACT_STRENGTH;
            assert(rect[0]>=0 && rect[1]>=0 && rect[2]>0 && rect[3]>0);
            assert(rect[0]+rect[2]<=teleport_width && rect[1]+rect[3]<=teleport_height);
            for(int sample=0;sample<32;sample++) {
                vec4_t point,world,clip;
                float u=randomf(),v=randomf();
                for(int j=0;j<3;j++)
                    point[j]=poly.verts[0][j]+u*(poly.verts[1][j]-poly.verts[0][j])
                        +v*(poly.verts[3][j]-poly.verts[0][j]);
                point[3]=1;
                Matrix4_Transform4(matrix,point,world);
                Matrix4_Transform4(teleport_screenmatrix,world,clip);
                if(clip[3]<=0) continue;
                float tc[2]={(1+clip[0]/clip[3])*.5f,(1+clip[1]/clip[3])*.5f};
                if(tc[0]<0 || tc[0]>1 || tc[1]<0 || tc[1]>1) continue;
                // Boundary values are conservative: actual shader normals
                // cannot simultaneously have unit x and y components.
                for(int axis=0;axis<2;axis++) for(int sign=-1;sign<=1;sign+=2) {
                    int size=axis?teleport_height:teleport_width;
                    float st=tc[axis]+sign*(.1f*fabsf(strength)+(axis?1.5f/1080:0));
                    int texel=(int)floorf(CLAMP(0.0f,st,1.0f)*size-.5f);
                    for(int tap=0;tap<2;tap++) {
                        int pixel=CLAMP(0,texel+tap,size-1);
                        assert(pixel>=rect[axis] && pixel<rect[axis]+rect[axis+2]);
                        samples++;
                    }
                }
            }
        }
    }
    // Inverse-project points known to be inside the GPU's clipped sample
    // rectangle. CPU culling must retain every one, including with reflection,
    // stereo skew and an oblique near plane. Wholly outside boxes must cull.
    for (int test=0;test<2000;test++) {
        mat4_t matrix, inverse;
        float angle=randomf()*6.2831853f;
        memset(teleport_viewmatrix,0,sizeof(mat4_t));
        float mirror=test&1?-1:1;
        teleport_viewmatrix[0]=cosf(angle)*mirror;
        teleport_viewmatrix[1]=sinf(angle)*mirror;
        teleport_viewmatrix[4]=-sinf(angle);
        teleport_viewmatrix[5]=cosf(angle);
        teleport_viewmatrix[10]=teleport_viewmatrix[15]=1;
        teleport_viewmatrix[12]=(randomf()-.5f)*100;
        teleport_viewmatrix[13]=(randomf()-.5f)*100;
        Matrix4_ProjectionMatrix(90,70,4,4096,false,(randomf()-.5f)*2,0,teleport_projection);
        vec3_t normal={.1f,.2f,-1};
        VectorNormalize(normal);
        R_TeleportClip(normal,20);
        Matrix4_Multiply(teleport_projection,teleport_viewmatrix,matrix);
        assert(Matrix4_Invert(matrix,inverse));
        teleport_width=512; teleport_height=384;
        teleport_planes[0].screenmins[0]=randomf()*.4f;
        teleport_planes[0].screenmins[1]=randomf()*.4f;
        teleport_planes[0].screenmaxs[0]=.6f+randomf()*.4f;
        teleport_planes[0].screenmaxs[1]=.6f+randomf()*.4f;
        R_TeleportScissor(teleport_planes,test&1);
        R_TeleportFrustum(teleport_planes,test&1);
        assert(r_frustumplanes==5);
        for (int sample=0;sample<64;sample++) {
            vec4_t clip, world;
            vec3_t point, mins, maxs;
            clip[0]=2*(rect[0]+(.001f+randomf()*.998f)*rect[2])/teleport_width-1;
            clip[1]=2*(rect[1]+(.001f+randomf()*.998f)*rect[3])/teleport_height-1;
            clip[2]=-.999f+randomf()*1.998f; clip[3]=1;
            Matrix4_Transform4(inverse,clip,world);
            if (world[3]<=0) continue;
            VectorScale(world,1/world[3],point);
            for (int j=0;j<3;j++) { mins[j]=point[j]-.1f; maxs[j]=point[j]+.1f; }
            assert(!R_CullBox(point,point));
            assert(!R_CullBox(mins,maxs));
        }
        for (int side=0;side<5;side++) {
            vec3_t point, mins, maxs;
            VectorScale(frustum[side].normal,frustum[side].dist-100,point);
            for (int j=0;j<3;j++) { mins[j]=point[j]-1; maxs[j]=point[j]+1; }
            assert(R_CullBox(mins,maxs));
        }
    }
    assert(samples>100000);
    printf("PASS: %zu bilinear taps covered, coplanar/PVS union, leaf memo/reset, missing-normal fallback, solid probes, transformed and close-up faces\n",samples);
    puts("PASS: cropped CPU frustum retains GPU-visible samples with reflection, oblique clipping and stereo skew; rejects outside boxes");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qssm-tele-scissor-') as directory:
    work = Path(directory)
    (work / 'test.c').write_text(source)
    cflags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags'], text=True))
    subprocess.run(['cc', '-std=gnu11', '-O2', '-DUSE_SDL2', *cflags,
                    '-I', str(ROOT / 'Quake'), str(work / 'test.c'), '-lm',
                    '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
