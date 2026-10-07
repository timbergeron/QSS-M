"""Bit-exact item colors against frozen legacy grid, with one hue classification per sample."""
import os
from pathlib import Path
import shlex,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
text=(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'Quake/gl_texmgr.c').read_text(encoding='utf-8')
begin=text.index('static int TexMgr_ItemColorBucket (')
end=text.index('/* Sample while RGBA',begin)
actual=text[begin:end].replace('static int TexMgr_ItemColorBucket (','static int TexMgr_ItemColorBucket_impl (',1)
split=actual.index('/* A grid')
actual=actual[:split]+r"""
static int TexMgr_ItemColorBucket(const byte *p) {classifications++;return TexMgr_ItemColorBucket_impl(p);}
"""+actual[split:]
REFERENCE='static size_t Legacy_ItemColorIndex (size_t sample, size_t width, size_t height)\n{\n\tsize_t columns = q_min(width, (size_t)64), rows = q_min(height, (size_t)64);\n\tsize_t x = ((sample % columns) * 2 + 1) * width / (columns * 2);\n\tsize_t y = ((sample / columns) * 2 + 1) * height / (rows * 2);\n\treturn y * width + x;\n}\n\nstatic qboolean Legacy_SampleItemColor (const byte *pixels, size_t width, size_t height, vec3_t color)\n{\n\tdouble weights[25] = {0}, sums[25][3] = {{0}};\n\tdouble best_distance = DBL_MAX;\n\tint best = -1, bucket, c;\n\tsize_t i, selected = 0;\n\t/* Bound upload-time sampling even for large replacement skins. */\n\tsize_t count = q_min(width, (size_t)64) * q_min(height, (size_t)64);\n\tfor (i = 0; i < count; i++)\n\t{\n\t\tsize_t index = Legacy_ItemColorIndex(i, width, height);\n\t\tconst byte *p = pixels + index * 4;\n\t\tint hi = q_max(p[0], q_max(p[1], p[2]));\n\t\tint lo = q_min(p[0], q_min(p[1], p[2]));\n\t\tdouble weight;\n\t\tbucket = TexMgr_ItemColorBucket_impl(p);\n\t\tif (bucket < 0)\n\t\t\tcontinue;\n\t\tweight = (hi - lo + 1) * (double)hi;\n\t\tweights[bucket] += weight;\n\t\tfor (c = 0; c < 3; c++)\n\t\t\tsums[bucket][c] += p[c] * weight;\n\t}\n\tfor (bucket = 0; bucket < 25; bucket++)\n\t\tif (weights[bucket] > 0 && (best < 0 || weights[bucket] > weights[best]))\n\t\t\tbest = bucket;\n\tif (best < 0)\n\t\treturn false;\n\tfor (c = 0; c < 3; c++)\n\t\tsums[best][c] /= weights[best];\n\tfor (i = 0; i < count; i++)\n\t{\n\t\tsize_t index = Legacy_ItemColorIndex(i, width, height);\n\t\tconst byte *p = pixels + index * 4;\n\t\tdouble distance = 0;\n\t\tif (TexMgr_ItemColorBucket_impl(p) != best)\n\t\t\tcontinue;\n\t\tfor (c = 0; c < 3; c++)\n\t\t{\n\t\t\tdouble d = p[c] - sums[best][c];\n\t\t\tdistance += d * d;\n\t\t}\n\t\tif (distance < best_distance)\n\t\t{\n\t\t\tbest_distance = distance;\n\t\t\tselected = index;\n\t\t}\n\t}\n\tfor (c = 0; c < 3; c++)\n\t\tcolor[c] = pixels[selected * 4 + c] / 255.0f;\n\treturn true;\n}\n\n'
source=r"""
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
typedef unsigned char byte;typedef int qboolean;typedef float vec3_t[3];
#define q_min(a,b) ((a)<(b)?(a):(b))
#define q_max(a,b) ((a)>(b)?(a):(b))
#define false 0
#define true 1
#define CHECK(c,m) do {if(!(c)){puts("FAIL: " m);exit(1);}}while(0)
static size_t classifications;
"""+actual+REFERENCE+r"""
static unsigned int seed=12345;
static unsigned int random_word(void) {seed=seed*1664525u+1013904223u;return seed;}
static void fixture(size_t width,size_t height,int mode) {
 size_t n=width*height*4,i,count=(q_min(width,64u)*q_min(height,64u));
 byte *p=(byte*)malloc(n?n:1);vec3_t a={0},b={0};int va,vb;
 CHECK(p,"fixture allocation");
 for(i=0;i<n;i++) p[i]=(byte)(random_word()>>24);
 for(i=0;i<n;i+=4) {if(mode==1)p[i+3]=255;else if(mode==2)p[i+3]=0;else if(mode==3)p[i]=p[i+1]=p[i+2]=p[i+3]=255;else if(mode==4)p[i]=p[i+1]=p[i+2]=0;}
 classifications=0;va=TexMgr_SampleItemColor(p,width,height,a);
 vb=Legacy_SampleItemColor(p,width,height,b);
 CHECK(va==vb&&(!va||!memcmp(a,b,sizeof(a))),"exact legacy validity, chosen texel and float RGB");
 CHECK(classifications<=count,"classify each grid sample at most once");
 free(p);
}
int main(void) {
 size_t dims[][2]={{0,0},{1,1},{63,65},{64,64},{65,63},{129,17},{16,513},{2048,11},{11,2048},{257,193}};
 size_t i;int mode;
 for(i=0;i<sizeof(dims)/sizeof(dims[0]);i++)for(mode=0;mode<5;mode++)fixture(dims[i][0],dims[i][1],mode);
 for(i=0;i<256;i++)fixture(1+random_word()%512,1+random_word()%512,(int)(i%5));
 puts("PASS: exact legacy item color for 306 random, narrow, tall, transparent, gray and black fixtures; at most one hue classification per sample");
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='qssm-color-grid-') as tmp:
 work=Path(tmp);(work/'test.c').write_text(source)
 cc=shlex.split(os.environ.get('CC','cl' if os.name=='nt' else 'cc'));binary=work/('test.exe' if os.name=='nt' else 'test')
 args=[*cc,'/nologo','/W3','/O2','test.c',f'/Fe:{binary}'] if Path(cc[0]).stem.lower()=='cl' else [*cc,'-std=c99','-Wall','-Wextra','-O2','test.c','-o',str(binary)]
 subprocess.run(args,cwd=work,check=True);subprocess.run([str(binary)],cwd=work,check=True)
