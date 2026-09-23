"""Exercise production per-frame view caches: the fat PVS and view-contents leafs.

Big maps such as immortal.bsp rebuilt the same answers every frame. This
compiles the real SV_FatPVS (with its leaf-set key) from Quake/sv_main.c and the
real Mod_PointInLeafMargin / R_EntityPointInLeaf from Quake/gl_model.c and
Quake/gl_rmain.c against random BSP trees, and checks that:

* SV_FatPVS always equals a from-scratch merge of every non-solid leaf within
  8 units (all bits set when there are none), skips the merge when the same
  leafs come back, recomputes after a caller merges more leafs into the buffer
  (skyroom), after the model generation changes (map reload into the same
  memory), for a different model, and when too many leafs are near to key;
* Mod_PointInLeafMargin lands where Mod_PointInLeaf does and reports the
  nearest tested plane, and R_EntityPointInLeaf returns exactly that leaf on
  long random walks (tiny to large steps, big coordinates, non-axial planes,
  points on planes) while reusing it most of the time, and starts over when
  the entity's model or the model generation changes.

Compile with CC, or run from a Visual Studio developer shell on Windows.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sv_main = (ROOT / "Quake/sv_main.c").read_text()
gl_model = (ROOT / "Quake/gl_model.c").read_text()
gl_rmain = (ROOT / "Quake/gl_rmain.c").read_text()


def function(text, declaration, after=0):
    start = text.index(declaration, after)
    return text[start:text.index("\n}", start) + 2]


key = sv_main.index("#define FATPVS_MAX_KEY_LEAFS")
fatpvs = sv_main[key:sv_main.index("static void SV_AddToFatPVS", key)]
fatpvs += function(sv_main, "static void SV_AddToFatPVS (", key)
fatpvs += "\n" + function(sv_main, "static int SV_FindFatPVSLeafs (", key)
fatpvs += "\n" + function(sv_main, "byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel) //", key)
margin = function(gl_model, "mleaf_t *Mod_PointInLeafMargin (")
slack = next(l for l in gl_rmain.splitlines() if l.startswith("#define R_POINTINLEAF_SLACK"))
entleaf = function(gl_rmain, "static mleaf_t *R_EntityPointInLeaf (")

source = r'''
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define false 0
#define true 1
#define CONTENTS_EMPTY -1
#define CONTENTS_SOLID -2
#define CONTENTS_WATER -3
#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define VectorSubtract(a,b,c) do{(c)[0]=(a)[0]-(b)[0];(c)[1]=(a)[1]-(b)[1];(c)[2]=(a)[2]-(b)[2];}while(0)
#define VectorCopy(a,b) do{(b)[0]=(a)[0];(b)[1]=(a)[1];(b)[2]=(a)[2];}while(0)
#define q_max(a,b) ((a) > (b) ? (a) : (b))
#define Q_memset memset
#define CHECK(cond, message) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", message, __LINE__); exit(1); } } while (0)
typedef int qboolean;
typedef unsigned char byte;
typedef float vec3_t[3];
typedef struct mplane_s { vec3_t normal; float dist; byte type; } mplane_t;
typedef struct mnode_s { int contents; mplane_t *plane; struct mnode_s *children[2]; int id; } mnode_t;
typedef mnode_t mleaf_t;
typedef struct qmodel_s { mnode_t *nodes; int numnodes; struct { int firstclipnode; } hulls[1]; mleaf_t *leafs; int numleafs; } qmodel_t;
typedef struct entity_s { qmodel_t *model; qmodel_t *contentsmodel; int contentsgeneration; vec3_t contentspos; float contentsradius; mleaf_t *contentsleaf; } entity_t;
static void Sys_Error (const char *e, ...) { printf("FAIL: Sys_Error %s\n", e); exit(1); }
int mod_generation;
static int	fatbytes;
static byte	*fatpvs;
static int	fatpvs_capacity;
static qboolean fatpvs_any;
/* vis rows: one random row per leaf id, per "map load" */
#define MAXLEAFS 1024
static byte rows[MAXLEAFS][MAXLEAFS / 8 + 8];
static int leafpvs_calls;
static byte *Mod_LeafPVS (mleaf_t *leaf, qmodel_t *model) { (void)model; leafpvs_calls++; return rows[leaf->id]; }
'''
source += fatpvs + "\n" + margin + "\n" + slack + "\n" + entleaf + "\n"
source += r'''
static unsigned rng = 12345;
static unsigned rnd (void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static float frand (float lo, float hi) { return lo + (hi - lo) * (rnd() & 0xffff) / 65535.0f; }

static mnode_t nodes[8192]; static int numnodes;
static mplane_t planes[8192]; static int numplanes;
static mnode_t leafs[MAXLEAFS]; static int numleafs;

/* a random tree: the planes of each subtree pass near 'center', so leaf
   regions are small there and large far away, like real maps */
static mnode_t *build (int depth, const float *center, float spread, int axial_only)
{
	mnode_t *n;
	if (depth == 0 || numleafs >= MAXLEAFS - 2 || numnodes >= 8190)
	{
		n = &leafs[numleafs]; n->id = numleafs++;
		n->contents = (rnd() % 4 == 0) ? CONTENTS_SOLID : (rnd() % 3 == 0) ? CONTENTS_WATER : CONTENTS_EMPTY;
		return n;
	}
	n = &nodes[numnodes++];
	n->contents = 0;
	n->plane = &planes[numplanes++];
	if (axial_only || rnd() % 2)
	{
		int t = rnd() % 3;
		n->plane->type = (byte)t;
		n->plane->normal[0] = n->plane->normal[1] = n->plane->normal[2] = 0;
		n->plane->normal[t] = 1;
		n->plane->dist = center[t] + frand(-spread, spread);
	}
	else
	{
		float len;
		int k;
		for (k = 0; k < 3; k++) n->plane->normal[k] = frand(-1, 1);
		len = sqrtf(DotProduct(n->plane->normal, n->plane->normal));
		for (k = 0; k < 3; k++) n->plane->normal[k] /= len;
		n->plane->type = 3;
		n->plane->dist = DotProduct(center, n->plane->normal) + frand(-spread, spread);
	}
	{
		float c0[3], c1[3];
		int k;
		for (k = 0; k < 3; k++) { c0[k] = center[k] + n->plane->normal[k] * spread * 0.5f; c1[k] = center[k] - n->plane->normal[k] * spread * 0.5f; }
		n->children[0] = build(depth - 1, c0, spread * 0.6f, axial_only);
		n->children[1] = build(depth - 1, c1, spread * 0.6f, axial_only);
	}
	return n;
}

static void make_model (qmodel_t *m, int depth, const float *center, float spread, int axial_only)
{
	numnodes = numplanes = 0; numleafs = 1;	/* leaf 0: the shared solid leaf */
	leafs[0].contents = CONTENTS_SOLID; leafs[0].id = 0;
	build(depth, center, spread, axial_only);
	m->nodes = nodes; m->numnodes = numnodes; m->hulls[0].firstclipnode = 0;
	m->leafs = leafs; m->numleafs = numleafs;
}

static void fill_rows (void)
{
	int i, j;
	for (i = 0; i < MAXLEAFS; i++)
		for (j = 0; j < (int)sizeof(rows[i]); j++)
			rows[i][j] = (byte)(rnd() & rnd() & 0xff);
}

/* the original leaf walk, before margins */
static mleaf_t *ref_point_in_leaf (const float *p, qmodel_t *m)
{
	mnode_t *node = m->nodes;
	while (node->contents >= 0)
	{
		mplane_t *plane = node->plane;
		float d = plane->type < 3 ? p[plane->type] - plane->dist : DotProduct(p, plane->normal) - plane->dist;
		node = d > 0 ? node->children[0] : node->children[1];
	}
	return node;
}

/* the original SV_FatPVS: OR every non-solid leaf within 8 units */
static byte ref[MAXLEAFS / 8 + 8];
static int ref_any, ref_bytes;
static void ref_add (const float *org, mnode_t *node)
{
	while (1)
	{
		mplane_t *plane; float d;
		if (node->contents < 0)
		{
			if (node->contents != CONTENTS_SOLID)
			{
				int i; ref_any = 1;
				for (i = 0; i < ref_bytes - 3; i += 4) *(uint32_t *)&ref[i] |= *(uint32_t *)&rows[node->id][i];
			}
			return;
		}
		plane = node->plane;
		d = plane->type < 3 ? org[plane->type] - plane->dist : DotProduct(org, plane->normal) - plane->dist;
		if (d > 8) node = node->children[0];
		else if (d < -8) node = node->children[1];
		else { ref_add(org, node->children[0]); node = node->children[1]; }
	}
}
static byte *ref_fatpvs (const float *org, qmodel_t *m)
{
	ref_bytes = (m->numleafs + 31) / 8;
	memset(ref, 0, sizeof(ref)); ref_any = 0;
	ref_add(org, m->nodes);
	if (!ref_any) memset(ref, 0xff, ref_bytes);
	return ref;
}

/* SV_FatPVS first: it sets fatbytes for the model */
static int fat_matches (const float *org, qmodel_t *m)
{
	byte *got = SV_FatPVS((float *)org, m);
	return !memcmp(got, ref_fatpvs(org, m), fatbytes);
}

int main (void)
{
	static qmodel_t world, other;
	float center[3] = {0, 0, 0}, org[3];
	int i, t, hits = 0, calls = 0;

	/* ---- fat PVS ---- */
	fill_rows();
	make_model(&world, 9, center, 2000, 0);
	for (t = 0; t < 20000; t++)
	{
		int before;
		/* clustered points, so the same leaf sets come back */
		if (t % 50 == 0) for (i = 0; i < 3; i++) org[i] = frand(-2500, 2500);
		else for (i = 0; i < 3; i++) org[i] += frand(-6, 6);
		before = leafpvs_calls;
		{
			byte *got = SV_FatPVS(org, &world);
			byte *want = ref_fatpvs(org, &world);
			CHECK(!memcmp(got, want, fatbytes), "fat PVS must equal a from-scratch merge");
		}
		calls++;
		if (leafpvs_calls == before) hits++;
		if (t % 997 == 0)
		{	/* a caller merges the skyroom's leafs into the buffer */
			float sky[3] = {frand(-2500, 2500), frand(-2500, 2500), frand(-2500, 2500)};
			SV_AddToFatPVS(sky, world.nodes, &world);
			CHECK(fat_matches(org, &world), "a merged buffer must not be reused");
		}
		if (t % 1999 == 0)
		{	/* map reload into the same memory: same pointers, new vis data */
			fill_rows(); mod_generation++;
			CHECK(fat_matches(org, &world), "a new model generation must recompute");
		}
	}
	CHECK(hits > calls / 2, "repeated leaf sets should reuse the buffer");
	CHECK(hits < calls, "new leaf sets must recompute");

	/* a different model with the same leaf pointers must recompute */
	other = world; other.numleafs = world.numleafs - 1;
	for (i = 0; i < 3; i++) org[i] = 10;
	SV_FatPVS(org, &world);
	i = leafpvs_calls;
	{
		byte *got = SV_FatPVS(org, &other);	/* sets fatbytes for this model */
		CHECK(!memcmp(got, ref_fatpvs(org, &other), fatbytes), "another model must recompute");
	}
	CHECK(leafpvs_calls > i, "another model must not reuse the buffer");

	/* more leafs near the point than the key holds: exact, never reused */
	{
		static qmodel_t fan;
		mnode_t *root = NULL, **link = &root;
		numnodes = numplanes = 0; numleafs = 1;
		for (i = 0; i < 100; i++)
		{	/* 100 planes through the origin, each with its own empty leaf on one side */
			mnode_t *n = &nodes[numnodes++];
			mnode_t *leaf = &leafs[numleafs]; leaf->id = numleafs++; leaf->contents = CONTENTS_EMPTY;
			n->contents = 0; n->plane = &planes[numplanes++];
			n->plane->type = (byte)(i % 3); n->plane->normal[0] = n->plane->normal[1] = n->plane->normal[2] = 0; n->plane->normal[i % 3] = 1; n->plane->dist = 0;
			n->children[0] = leaf; *link = n; link = &n->children[1];
		}
		{ mnode_t *leaf = &leafs[numleafs]; leaf->id = numleafs++; leaf->contents = CONTENTS_EMPTY; *link = leaf; }
		fan.nodes = root; fan.numnodes = numnodes; fan.hulls[0].firstclipnode = 0; fan.leafs = leafs; fan.numleafs = numleafs;
		for (i = 0; i < 3; i++) org[i] = 0;
		CHECK(fat_matches(org, &fan), "an overflowing leaf set must still be exact");
		i = leafpvs_calls;
		CHECK(fat_matches(org, &fan), "an overflowing leaf set must still be exact");
		CHECK(leafpvs_calls > i, "an overflowing leaf set is never reused");
	}

	/* ---- view-contents leafs ---- */
	{
		int model_i;
		long long reuse = 0, total = 0;
		for (model_i = 0; model_i < 60; model_i++)
		{
			static qmodel_t m, m2;
			static entity_t ent;
			float c[3], p[3], mar;
			int step;
			for (i = 0; i < 3; i++) c[i] = model_i % 3 == 0 ? frand(-60000, 60000) : frand(-3000, 3000);
			make_model(&m, 2 + model_i % 8, c, 50 + (float)(rnd() % 4000), model_i % 5 == 0);
			memset(&ent, 0, sizeof(ent)); ent.model = &m;
			for (i = 0; i < 3; i++) p[i] = c[i] + frand(-4000, 4000);
			for (step = 0; step < 20000; step++)
			{
				float scale = (step % 7 == 0) ? 50.0f : (step % 3 == 0) ? 0.01f : 2.0f;
				mleaf_t *want, *got, *direct;
				for (i = 0; i < 3; i++) p[i] += frand(-scale, scale);
				if (step % 4999 == 0)
				{	/* land exactly on a plane */
					mplane_t *pl = m.nodes[0].plane;
					if (pl->type < 3) p[pl->type] = pl->dist;
				}
				want = ref_point_in_leaf(p, &m);
				direct = Mod_PointInLeafMargin(p, &m, &mar);
				CHECK(direct == want, "Mod_PointInLeafMargin must land where Mod_PointInLeaf does");
				CHECK(mar >= 0, "margin is a distance");
				i = ent.contentsleaf != NULL && ent.contentsmodel == ent.model;
				{
					vec3_t d; float r2 = ent.contentsradius * ent.contentsradius;
					VectorSubtract(p, ent.contentspos, d);
					if (i && DotProduct(d, d) < r2) reuse++;
				}
				got = R_EntityPointInLeaf(&ent, p);
				total++;
				CHECK(got == want, "the cached leaf must be exactly Mod_PointInLeaf's");
				if (step == 10000)
				{	/* the entity switches model, then the model generation changes */
					make_model(&m2, 3, c, 500, 0);
					ent.model = &m2;
					CHECK(R_EntityPointInLeaf(&ent, p) == ref_point_in_leaf(p, &m2), "a new model must be walked");
					ent.model = &m;
					mod_generation++;
					make_model(&m, 2 + model_i % 8, c, 50 + (float)(rnd() % 4000), model_i % 5 == 0);
					CHECK(R_EntityPointInLeaf(&ent, p) == ref_point_in_leaf(p, &m), "a new generation must be walked");
				}
			}
		}
		CHECK(reuse > total / 4, "small moves should reuse the leaf");
		CHECK(reuse < total, "large moves must walk again");
	}
	puts("PASS: fat PVS merges, leaf-set reuse, skyroom merges, reloads, overflow; point-in-leaf margins and cached view-contents leafs on random trees");
	return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="qssm-viewcache-") as tmp:
    work = Path(tmp)
    (work / "test.c").write_text(source)
    cc = shlex.split(os.environ.get("CC", "cl" if os.name == "nt" else "cc"))
    binary = work / ("test.exe" if os.name == "nt" else "test")
    if Path(cc[0]).stem.lower() == "cl":
        command = [*cc, "/nologo", "/W3", "/O2", "test.c", f"/Fe:{binary}"]
    else:
        command = [*cc, "-std=c99", "-Wall", "-Wextra", "-O2", "test.c", "-o", str(binary), "-lm"]
    subprocess.run(command, cwd=work, check=True)
    subprocess.run([str(binary)], check=True)
