/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_brush.c: brush model rendering. renamed from r_surf.c

#include "quakedef.h"

extern cvar_t gl_fullbrights, r_drawflat, gl_overbright, r_oldwater; //johnfitz
extern cvar_t r_brokenturbbias; // to replicate a QuakeSpasm bug.
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix
extern cvar_t r_ambient; // woods #rambient
cvar_t r_lightmap_format = {"r_lightmap_format","", CVAR_ARCHIVE};

int		gl_lightmap_format;
int		lightmap_bytes;
qboolean lightmaps_latecached;
qboolean lightmaps_skipupdates;

#define MAX_SANITY_LIGHTMAPS (1u<<20)
struct lightmap_s	*lightmaps;
int					lightmap_count;
static int			last_lightmap_allocated;
int LMBLOCK_WIDTH, LMBLOCK_HEIGHT;

// woods #lmrect -- scratch stream PBO for dynamic lightmap uploads. Writing
// texels straight from client memory makes Apple's GL-on-Metal block in
// prepareResourceForCPUAccess until the GPU stops reading the texture (a full
// pipeline sync per touched atlas per frame); staging through an orphaned PBO
// turns the update into a GPU-side blit instead.
static GLuint lm_scratchpbo; // reset by GL_BuildLightmaps on map load / video restart
static SDL_mutex *lm_dirty_mutex; // woods #lmrect -- guards modified/rectchange/dirtyrects between the scenecache worker and the main thread's uploads

static void R_TexSubImageLightmap (int x, int t, int w, int h, const GLvoid *src);

static qboolean LM_CanUsePersistentPBO (void)
{
	return !COM_CheckParm ("-nolightmappbo")
		&& gl_vbo_able
		&& GL_GenBuffersFunc
		&& GL_BindBufferFunc
		&& GL_DeleteBuffersFunc
		&& GL_BufferStorageFunc
		&& GL_MapBufferRangeFunc
		&& GL_UnmapBufferFunc;
}

static void LM_AllocateStaging (struct lightmap_s *lm)
{
	const size_t bytes = (size_t)4 * (size_t)LMBLOCK_WIDTH * (size_t)LMBLOCK_HEIGHT;

	if (LM_CanUsePersistentPBO ())
	{
		GL_GenBuffersFunc (1, &lm->pbohandle);
		if (lm->pbohandle)
		{
			GL_BindBufferFunc (GL_PIXEL_UNPACK_BUFFER_ARB, lm->pbohandle);
			GL_BufferStorageFunc (GL_PIXEL_UNPACK_BUFFER_ARB, bytes, NULL,
					GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT | GL_CLIENT_STORAGE_BIT);
			lm->pbodata = GL_MapBufferRangeFunc (GL_PIXEL_UNPACK_BUFFER_ARB, 0, bytes,
					GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
			GL_BindBufferFunc (GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			if (lm->pbodata)
				return;

			GL_DeleteBuffersFunc (1, &lm->pbohandle);
			lm->pbohandle = 0;
		}
	}

	lm->pbodata = (byte *) calloc (1, bytes);
	if (!lm->pbodata)
		Sys_Error ("GL_BuildLightmaps: out of memory on %u bytes", (unsigned int)bytes);
}


/*
===============
R_TextureAnimation -- johnfitz -- added "frame" param to eliminate use of "currententity" global

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation (texture_t *base, int frame)
{
	int		relative;
	int		count;

	if (frame)
		if (base->alternate_anims)
			base = base->alternate_anims;

	if (!base->anim_total)
		return base;

	relative = (int)(cl.time*10) % base->anim_total;

	count = 0;
	while (base->anim_min > relative || base->anim_max <= relative)
	{
		base = base->anim_next;
		if (!base)
			Sys_Error ("R_TextureAnimation: broken cycle");
		if (++count > 100)
			Sys_Error ("R_TextureAnimation: infinite cycle");
	}

	return base;
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly (glpoly_t *p)
{
	float	*v;
	int		i;

	glBegin (GL_POLYGON);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		glTexCoord2f (v[3], v[4]);
		glVertex3fv (v);
	}
	glEnd ();
}

/*
================
DrawGLTriangleFan -- johnfitz -- like DrawGLPoly but for r_showtris
================
*/
void DrawGLTriangleFan (glpoly_t *p)
{
	float	*v;
	int		i;

	glBegin (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
	{
		glVertex3fv (v);
	}
	glEnd ();
}

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

static qboolean R_IsStaticEntity (const entity_t *e)
{
	return e->is_static;
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	int			i, k;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	qmodel_t	*clmodel;
	vec3_t		lightorg;
	qboolean	zfix;
	extern byte *skipsubmodels;

	if (e->model->submodelof == cl.worldmodel &&
		skipsubmodels &&
		skipsubmodels[e->model->submodelidx>>3]&(1u<<(e->model->submodelidx&7)))
		return;	//its in the scenecache that we're drawing. don't draw it twice (and certainly not the slow way).

	if (R_CullModelForEntity(e))
		return;

	currententity = e;
	clmodel = e->model;
	zfix = gl_zfix.value && !R_IsStaticEntity (e);

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

// calculate dynamic lighting for bmodel if it's not an
// instanced model
	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value)
	if (e->model->submodelof == cl.worldmodel)	//R_MarkLights has a hacky assumption about cl.worldmodel that could crash if we imported some other model.
	{
		for (k=0 ; k<MAX_DLIGHTS ; k++)
		{
			if ((cl_dlights[k].die < cl.time) ||
				(!cl_dlights[k].radius) ||
				(R_DlightStyleScale(&cl_dlights[k]) <= 0.0f))
				continue;

			VectorSubtract(cl_dlights[k].origin, e->origin, lightorg);
			R_MarkLights (&cl_dlights[k], lightorg, r_framecount, k,
				clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	glPushMatrix ();
	e->angles[0] = -e->angles[0];	// stupid quake bug
	if (zfix)
	{
		e->origin[0] -= DIST_EPSILON;
		e->origin[1] -= DIST_EPSILON;
		e->origin[2] -= DIST_EPSILON;
	}
	R_RotateForEntity (e->origin, e->angles, e);
	if (zfix)
	{
		e->origin[0] += DIST_EPSILON;
		e->origin[1] += DIST_EPSILON;
		e->origin[2] += DIST_EPSILON;
	}
	e->angles[0] = -e->angles[0];	// stupid quake bug

	if (R_DrawBModelDrawCache (clmodel, e))
	{
		glPopMatrix ();
		return;
	}
	R_ClearTextureChains (clmodel, chain_model);
	for (i=0 ; i<clmodel->nummodelsurfaces ; i++, psurf++)
	{
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_ChainSurface (psurf, chain_model);
			R_RenderDynamicLightmaps(clmodel, psurf);
			rs_brushpolys++;
		}
	}

	R_DrawTextureChains (clmodel, e, chain_model);
	R_DrawTextureChains_Water (clmodel, e, chain_model);

	glPopMatrix ();
}

/*
=================
R_DrawBrushModel_ShowTris -- johnfitz
=================
*/
void R_DrawBrushModel_ShowTris (entity_t *e)
{
	int			i;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	qmodel_t	*clmodel;
	glpoly_t	*p;

	if (R_CullModelForEntity(e))
		return;

	currententity = e;
	clmodel = e->model;

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	glPushMatrix ();
	e->angles[0] = -e->angles[0];	// stupid quake bug
	R_RotateForEntity (e->origin, e->angles, e);
	e->angles[0] = -e->angles[0];	// stupid quake bug

	//
	// draw it
	//
	for (i=0 ; i<clmodel->nummodelsurfaces ; i++, psurf++)
	{
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if ((psurf->flags & SURF_DRAWTURB) && !gl_glsl_water_able)
				for (p = psurf->polys->next; p; p = p->next)
					DrawGLTriangleFan (p);
			else
				DrawGLTriangleFan (psurf->polys);
		}
	}

	glPopMatrix ();
}

/*
=============================================================

	LIGHTMAPS

=============================================================
*/

// woods #lmrect -- shared by all three dynamic-lightmap dirty paths
// (R_RenderDynamicLightmaps here, plus the bmodel-drawcache and scenecache copies
// in r_world.c). Accumulates the surface's exact texel rect, growing whichever
// existing rect wastes the least area once the list fills.
// Sets lm->modified and grows lm->rectchange too, all under lm_dirty_mutex so the
// scenecache worker's marks can't be lost to R_UploadLightmap's concurrent
// snapshot-and-clear. Call this AFTER R_BuildLightMap has written the staging
// bytes: a rect must never be visible to the uploader before its texels are
// complete, or the upload consumes the mark and the finished bytes never reach
// the GPU (permanent stale glow when that was a dying dlight's clearing rebuild).
void R_LightmapMarkDirtyRect (struct lightmap_s *lm, int light_s, int light_t, int smax, int tmax)
{
	int l = light_s, t = light_t, r = light_s + smax, b = light_t + tmax;
	int i, best, bestwaste;
	glRect_t *theRect;

	if (l < 0) l = 0;
	if (t < 0) t = 0;
	if (r > LMBLOCK_WIDTH) r = LMBLOCK_WIDTH;
	if (b > LMBLOCK_HEIGHT) b = LMBLOCK_HEIGHT;
	if (r <= l || b <= t)
		return;

	SDL_LockMutex (lm_dirty_mutex);

	lm->modified = true;
	theRect = &lm->rectchange;
	if (t < theRect->t) {
		if (theRect->h)
			theRect->h += theRect->t - t;
		theRect->t = t;
	}
	if (l < theRect->l) {
		if (theRect->w)
			theRect->w += theRect->l - l;
		theRect->l = l;
	}
	if ((theRect->w + theRect->l) < r)
		theRect->w = r - theRect->l;
	if ((theRect->h + theRect->t) < b)
		theRect->h = b - theRect->t;

	best = -1; bestwaste = INT_MAX;
	for (i = 0; i < lm->numdirtyrects; i++)
	{
		glRect_t *dr = &lm->dirtyrects[i];
		int ul = q_min(l, (int)dr->l), ut = q_min(t, (int)dr->t);
		int ur = q_max(r, dr->l + dr->w), ub = q_max(b, dr->t + dr->h);
		int waste = (ur-ul)*(ub-ut) - dr->w*dr->h - (r-l)*(b-t);
		if (waste < bestwaste)
		{
			bestwaste = waste;
			best = i;
		}
	}
	// merge when the grown rect adds little dead area (or we're out of slots);
	// adjacent surfaces of the same dlight typically collapse into one rect.
	if (best >= 0 && (bestwaste <= 4096 || lm->numdirtyrects == LM_DIRTYRECTS))
	{
		glRect_t *dr = &lm->dirtyrects[best];
		int ul = q_min(l, (int)dr->l), ut = q_min(t, (int)dr->t);
		int ur = q_max(r, dr->l + dr->w), ub = q_max(b, dr->t + dr->h);
		dr->l = ul; dr->t = ut;
		dr->w = ur-ul; dr->h = ub-ut;
	}
	else
	{
		glRect_t *dr = &lm->dirtyrects[lm->numdirtyrects++];
		dr->l = l; dr->t = t;
		dr->w = r-l; dr->h = b-t;
	}

	SDL_UnlockMutex (lm_dirty_mutex);
}

/*
================
R_RenderDynamicLightmaps
called during rendering
================
*/
void R_RenderDynamicLightmaps (qmodel_t *model, msurface_t *fa)
{
	byte		*base;
	int			maps;
	int smax, tmax;

	if (fa->flags & SURF_DRAWTILED) //johnfitz -- not a lightmapped surface
		return;

	// add to lightmap chain
	fa->polys->chain = lightmaps[fa->lightmaptexturenum].polys;
	lightmaps[fa->lightmaptexturenum].polys = fa->polys;

	// check for lightmap modification
	for (maps=0; maps < MAXLIGHTMAPS && fa->styles[maps] != INVALID_LIGHTSTYLE; maps++)
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	if (fa->dlightframe == r_framecount	// dynamic this frame
		|| fa->cached_dlight)			// dynamic previously
	{
dynamic:
		if (r_dynamic.value)
		{
			struct lightmap_s *lm = &lightmaps[fa->lightmaptexturenum];
			smax = fa->extents[0]+1;
			tmax = fa->extents[1]+1;
			base = lm->pbodata;
			base += fa->light_t * LMBLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
			R_BuildLightMap (model, fa, base, LMBLOCK_WIDTH*lightmap_bytes, currententity, r_framecount, cl_dlights);
			R_LightmapMarkDirtyRect (lm, fa->light_s, fa->light_t, smax, tmax); // woods #lmrect -- after the bytes, see its comment
		}
	}
}

/*
========================
AllocBlock -- returns a texture number and the position inside it

tb -- rewritten as a skyline allocator.  The per-column scan was
O(LMBLOCK_WIDTH * w) for every surface, which dominated lightmap build time
on big maps (~120ms on ad_tears).  The skyline is kept as run-length
segments of equal height; candidate positions only need to be evaluated at
segment starts, and adjacent equal heights merge so the segment count stays
tiny.
========================
*/
typedef struct lmseg_s
{
	int	x;	// first column; the segment extends to the next segment's x (or LMBLOCK_WIDTH)
	int	h;	// skyline height over those columns
} lmseg_t;

static lmseg_t	*lmsegs, *lmsegs_scratch;
static int	lm_numsegs, lm_maxsegs;

static void LM_ResetSkyline (void)
{
	if (!lmsegs)
	{
		lm_maxsegs = 1024;
		lmsegs = (lmseg_t *) malloc (lm_maxsegs*sizeof(*lmsegs));
		lmsegs_scratch = (lmseg_t *) malloc (lm_maxsegs*sizeof(*lmsegs));
		if (!lmsegs || !lmsegs_scratch)
			Sys_Error ("LM_ResetSkyline: out of memory");
	}
	lm_numsegs = 1;
	lmsegs[0].x = 0;
	lmsegs[0].h = 0;
}

static void LM_SkylinePlace (int x0, int w, int newh)
{
	int		x1 = x0 + w;
	int		i, n, m, oldh_at_x1;
	lmseg_t	*out;

	if (lm_numsegs + 2 > lm_maxsegs)
	{
		int newmax = lm_maxsegs * 2;
		lmseg_t *newsegs = (lmseg_t *) malloc (newmax*sizeof(*lmsegs));
		lmseg_t *newscratch = (lmseg_t *) malloc (newmax*sizeof(*lmsegs_scratch));

		if (!newsegs || !newscratch)
		{
			free (newsegs);
			free (newscratch);
			Sys_Error ("LM_SkylinePlace: out of memory");
		}
		memcpy (newsegs, lmsegs, lm_numsegs*sizeof(*lmsegs));
		memcpy (newscratch, lmsegs_scratch, lm_numsegs*sizeof(*lmsegs_scratch));
		free (lmsegs);
		free (lmsegs_scratch);
		lmsegs = newsegs;
		lmsegs_scratch = newscratch;
		lm_maxsegs = newmax;
	}
	out = lmsegs_scratch;
	n = 0;

	// old skyline height at column x1, for the segment we cut in half
	oldh_at_x1 = 0;
	for (i = 0; i < lm_numsegs; i++)
	{
		int ex = (i+1 < lm_numsegs) ? lmsegs[i+1].x : LMBLOCK_WIDTH;
		if (lmsegs[i].x <= x1 && x1 < ex)
		{
			oldh_at_x1 = lmsegs[i].h;
			break;
		}
	}

	for (i = 0; i < lm_numsegs && lmsegs[i].x < x0; i++)
	{
		out[n].x = lmsegs[i].x;
		out[n].h = lmsegs[i].h;
		n++;
	}
	out[n].x = x0;
	out[n].h = newh;
	n++;
	if (x1 < LMBLOCK_WIDTH)
	{
		out[n].x = x1;
		out[n].h = oldh_at_x1;
		n++;
		for (i = 0; i < lm_numsegs; i++)
		{
			if (lmsegs[i].x <= x1)
				continue;
			out[n].x = lmsegs[i].x;
			out[n].h = lmsegs[i].h;
			n++;
		}
	}

	// copy back, merging adjacent segments of equal height
	m = 0;
	for (i = 0; i < n; i++)
	{
		if (m && lmsegs[m-1].h == out[i].h)
			continue;
		lmsegs[m].x = out[i].x;
		lmsegs[m].h = out[i].h;
		m++;
	}
	lm_numsegs = m;
}

int AllocBlock (int w, int h, int *x, int *y)
{
	int		s, e;
	int		best, bestx;
	int		texnum;

	h = (h + 3) & ~3;	//tb -- quantize so segment tops align and merge; keeps the skyline flat and the scan short
	if (h > LMBLOCK_HEIGHT)
		h = LMBLOCK_HEIGHT;	// don't let quantization push a just-fitting block past the atlas

	// ericw -- rather than searching starting at lightmap 0 every time,
	// start at the last lightmap we allocated a surface in.
	// This makes AllocBlock much faster on large levels (can shave off 3+ seconds
	// of load time on a level with 180 lightmaps), at a cost of not quite packing
	// lightmaps as tightly vs. not doing this (uses ~5% more lightmaps)
	for (texnum=last_lightmap_allocated ; texnum<MAX_SANITY_LIGHTMAPS ; texnum++)
	{
		if (texnum == lightmap_count)
		{
			struct lightmap_s *newlightmaps;
			lightmap_count++;
			newlightmaps = (struct lightmap_s *) realloc(lightmaps, sizeof(*lightmaps)*lightmap_count);
			if (!newlightmaps)
				Sys_Error ("AllocBlock: out of memory (%d lightmaps)", lightmap_count);
			lightmaps = newlightmaps;
			memset(&lightmaps[texnum], 0, sizeof(lightmaps[texnum]));
			LM_AllocateStaging (&lightmaps[texnum]);
			//as we're only tracking one texture, we don't need multiple copies of the skyline any more.
			LM_ResetSkyline ();

			lightmaps[texnum].modified = true;
			lightmaps[texnum].rectchange.l = 0;
			lightmaps[texnum].rectchange.t = 0;
			lightmaps[texnum].rectchange.h = LMBLOCK_HEIGHT;
			lightmaps[texnum].rectchange.w = LMBLOCK_WIDTH;
			lightmaps[texnum].numdirtyrects = 1; // woods #lmrect -- whole texture dirty
			lightmaps[texnum].dirtyrects[0].l = 0;
			lightmaps[texnum].dirtyrects[0].t = 0;
			lightmaps[texnum].dirtyrects[0].w = LMBLOCK_WIDTH;
			lightmaps[texnum].dirtyrects[0].h = LMBLOCK_HEIGHT;
		}
		best = LMBLOCK_HEIGHT;
		bestx = -1;

		for (s = 0; s < lm_numsegs; s++)
		{
			int pos = lmsegs[s].x;
			int maxh = 0;

			if (pos > LMBLOCK_WIDTH - w)
				break;
			for (e = s; e < lm_numsegs && lmsegs[e].x < pos + w; e++)
			{
				if (lmsegs[e].h >= best)
				{
					maxh = best;
					break;
				}
				if (lmsegs[e].h > maxh)
					maxh = lmsegs[e].h;
			}
			if (maxh < best)
			{
				best = maxh;
				bestx = pos;
			}
		}

		if (bestx < 0 || best + h > LMBLOCK_HEIGHT)
			continue;

		*x = bestx;
		*y = best;
		LM_SkylinePlace (bestx, w, best + h);
		lightmaps[texnum].upload_height = q_max (lightmaps[texnum].upload_height, best + h);

		last_lightmap_allocated = texnum;
		return texnum;
	}

	Sys_Error ("AllocBlock: full");
	return 0; //johnfitz -- shut up compiler
}


static mvertex_t	*r_pcurrentvertbase;
static qmodel_t		*currentmodel;

/*
========================
GL_CreateSurfaceLightmap
========================
*/

//tb -- deferred lightmap fill: at level load GL_BuildLightmaps queues every
//surface here after AllocBlock, then fills them all with worker threads.
//R_BuildLightMap is thread-safe (stack blocklights, disjoint dest rects);
//the lightmaps[] array must not be resized while workers run, so the fill
//only starts after every AllocBlock call is done.
typedef struct lm_filljob_s
{
	qmodel_t	*model;
	msurface_t	*surf;
	glpoly_t	*poly;	// non-NULL: build poly verts too (BuildSurfaceVerts)
} lm_filljob_t;

static lm_filljob_t	*lm_filljobs;
static int		lm_numfilljobs, lm_maxfilljobs;
static qboolean		lm_defer_fill;

static void BuildSurfaceVerts (qmodel_t *model, msurface_t *fa, glpoly_t *poly);	//forward

static void LM_ClearFillJobs (void)
{
	free (lm_filljobs);
	lm_filljobs = NULL;
	lm_numfilljobs = lm_maxfilljobs = 0;
}

typedef struct lm_fillctx_s
{
	int		first, stride;
	entity_t	*ent;
	int		framecount;
	dlight_t	*lights;
	const r_lightmap_buildstate_t *buildstate;
} lm_fillctx_t;

static void LM_FillRange (const lm_fillctx_t *ctx)
{
	int i;
	for (i = ctx->first; i < lm_numfilljobs; i += ctx->stride)
	{
		msurface_t *surf = lm_filljobs[i].surf;

		if (lm_filljobs[i].poly)
			BuildSurfaceVerts (lm_filljobs[i].model, surf, lm_filljobs[i].poly);

		if (surf->lightmaptexturenum >= 0)
		{
			byte *base = lightmaps[surf->lightmaptexturenum].pbodata;
			base += (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * lightmap_bytes;
			R_BuildLightMapForState (lm_filljobs[i].model, surf, base, LMBLOCK_WIDTH*lightmap_bytes, ctx->ent, ctx->framecount, ctx->lights, ctx->buildstate);
		}
	}
}

static int LM_FillThread (void *arg)
{
	LM_FillRange ((const lm_fillctx_t *)arg);
	return 0;
}

#define LM_MAX_FILL_THREADS	8
#define LM_MIN_JOBS_PER_THREAD	64

static void LM_QueueFillJob (qmodel_t *model, msurface_t *surf, glpoly_t *poly)
{
	if (lm_numfilljobs == lm_maxfilljobs)
	{
		lm_filljob_t *newjobs;
		int newmax = lm_maxfilljobs ? lm_maxfilljobs*2 : 4096;

		newjobs = (lm_filljob_t *) realloc (lm_filljobs, newmax*sizeof(*lm_filljobs));
		if (!newjobs)
			Sys_Error ("LM_QueueFillJob: out of memory (%d fill jobs)", newmax);
		lm_filljobs = newjobs;
		lm_maxfilljobs = newmax;
	}
	lm_filljobs[lm_numfilljobs].model = model;
	lm_filljobs[lm_numfilljobs].surf = surf;
	lm_filljobs[lm_numfilljobs].poly = poly;
	lm_numfilljobs++;
}

static void LM_RunDeferredFill (void)
{
	SDL_Thread	*threads[LM_MAX_FILL_THREADS];
	lm_fillctx_t	ctx[LM_MAX_FILL_THREADS];
	r_lightmap_buildstate_t buildstate;
	int		i, numthreads;

	R_LightmapBuildState_Snapshot (&buildstate);

	numthreads = q_max (1, SDL_GetCPUCount ());
	numthreads = q_min (numthreads, LM_MAX_FILL_THREADS);
	numthreads = q_min (numthreads, q_max (1, lm_numfilljobs / LM_MIN_JOBS_PER_THREAD));

	for (i = 0; i < numthreads; i++)
	{
		ctx[i].first = i;
		ctx[i].stride = numthreads;
		ctx[i].ent = currententity;
		ctx[i].framecount = r_framecount;
		ctx[i].lights = cl_dlights;
		ctx[i].buildstate = &buildstate;
	}

	if (numthreads <= 1)
	{
		if (lm_numfilljobs)
			LM_FillRange (&ctx[0]);
	}
	else
	{
		for (i = 1; i < numthreads; i++)
		{
			threads[i] = SDL_CreateThread (LM_FillThread, "lm_fill", &ctx[i]);
			if (!threads[i])
				Con_DPrintf ("LM_RunDeferredFill: SDL_CreateThread failed: %s\n", SDL_GetError());
		}
		LM_FillRange (&ctx[0]);
		for (i = 1; i < numthreads; i++)
		{
			if (threads[i])
				SDL_WaitThread (threads[i], NULL);
			else
				LM_FillRange (&ctx[i]);
		}
	}

	LM_ClearFillJobs ();
}

void GL_CreateSurfaceLightmap (qmodel_t *model, msurface_t *surf)
{
	int		smax, tmax;
	byte	*base;

	if (surf->flags & SURF_DRAWTILED)
	{
		surf->lightmaptexturenum = -1;
		return;
	}

	smax = surf->extents[0]+1;
	tmax = surf->extents[1]+1;

	surf->lightmaptexturenum = AllocBlock (smax, tmax, &surf->light_s, &surf->light_t);

	if (lm_defer_fill)
		return;	// GL_BuildModel queues the fill; LM_RunDeferredFill does it on workers

	base = lightmaps[surf->lightmaptexturenum].pbodata;
	base += (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * lightmap_bytes;
	R_BuildLightMap (model, surf, base, LMBLOCK_WIDTH*lightmap_bytes, currententity, r_framecount, cl_dlights);
}

#ifdef MACBOOK_ARM_HACK // ezquake 22f39e2 by nano -- woods #collinear
#define EPSILON 1e-6

// Check if triangle has a ~zero area
// https://en.wikipedia.org/wiki/Collinearity
static qboolean R_ArePointsColinear(const float *v1, const float *v2, const float *v3)
{
	vec3_t d0, d1, cross;

	VectorSubtract(v2, v1, d0);
	VectorSubtract(v3, v2, d1);

	// Prevent T-junctions by only removing vertices that are very close to their neighbors.
	// If edges are long, we keep the vertex even if it's collinear.
	if (DotProduct(d0, d0) > 1.0f || DotProduct(d1, d1) > 1.0f)
		return false;

	CrossProduct(d0, d1, cross);

	if (DotProduct(cross, cross) >= EPSILON)
		return false;

	// Check texture coordinates (indices 3, 4)
	d0[0] = v2[3] - v1[3];
	d0[1] = v2[4] - v1[4];
	d0[2] = 0;

	d1[0] = v3[3] - v2[3];
	d1[1] = v3[4] - v2[4];
	d1[2] = 0;

	CrossProduct(d0, d1, cross);

	if (DotProduct(cross, cross) >= EPSILON)
		return false;

	// Check lightmap coordinates (indices 5, 6)
	d0[0] = v2[5] - v1[5];
	d0[1] = v2[6] - v1[6];
	d0[2] = 0;

	d1[0] = v3[5] - v2[5];
	d1[1] = v3[6] - v2[6];
	d1[2] = 0;

	CrossProduct(d0, d1, cross);

	if (DotProduct(cross, cross) >= EPSILON)
		return false;

	return true;
}

static void R_RemoveColinearVertices(glpoly_t* poly, float new_verts[][VERTEXSIZE])
{
	int i, v1_index, v2_index, v3_index, new_numverts = 0;
	int numverts = poly->numverts;

	v1_index = numverts - 1;
	v2_index = 0;
	v3_index = 1;

	for (i = 0; i < numverts; i++) {
		float* v1 = poly->verts[v1_index];
		float* v2 = poly->verts[v2_index];
		float* v3 = poly->verts[v3_index];

		if (!R_ArePointsColinear(v1, v2, v3)) {
			memcpy(new_verts[new_numverts], v2, sizeof(float) * VERTEXSIZE);
			new_numverts++;
		}

		v1_index = v2_index;
		v2_index = v3_index;
		v3_index = (v3_index + 1) % numverts;
	}

	// never shrink below a renderable triangle; index/VBO code assumes >= 3 verts
	if (new_numverts >= 3) {
		memcpy(poly->verts, new_verts, new_numverts * sizeof(float) * VERTEXSIZE);
		poly->numverts = new_numverts;
	}
}
#endif

/*
================
R_SurfaceVertCount -- woods #collinear

Number of verts actually stored in the surface's poly (collinear removal may
have shrunk it below numedges). Clamped to numedges so it never exceeds the
per-surface space counted for the bmodel VBO. Every place that fans this
surface into triangle indices must use this count, not numedges, or the fan
picks up stale verts left behind by the compaction.
================
*/
int R_SurfaceVertCount (const msurface_t *s)
{
	if (s->polys && s->polys->numverts < s->numedges)
		return s->polys->numverts;
	return s->numedges;
}

/*
================
BuildSurfaceDisplayList -- called at level load time
================
*/
static glpoly_t *AllocSurfacePoly (msurface_t *fa)
{
	glpoly_t *poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (fa->numedges-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	fa->polys = poly;
	poly->numverts = fa->numedges;
	return poly;
}

//tb -- the vertex math from BuildSurfaceDisplayList, split out so deferred
//fill jobs can run it on worker threads (no Hunk_Alloc, no globals)
static void BuildSurfaceVerts (qmodel_t *model, msurface_t *fa, glpoly_t *poly)
{
#ifdef MACBOOK_ARM_HACK // woods #collinear
	extern cvar_t r_remove_collinear_vertices;
#endif
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	float		*vec;
	float		s, t, s0, t0;

	pedges = model->edges;
	lnumverts = fa->numedges;

	if ((fa->flags & SURF_DRAWTURB) && r_brokenturbbias.value)
	{
		// match Mod_PolyForUnlitSurface
		s0 = t0 = 0.f;
	}
	else
	{
		s0 = fa->texinfo->vecs[0][3];
		t0 = fa->texinfo->vecs[1][3];
	}

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = model->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = model->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = model->vertexes[r_pedge->v[1]].position;
		}
		s = DotProduct (vec, fa->texinfo->vecs[0]) + s0;
		s /= fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + t0;
		t /= fa->texinfo->texture->height;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		// Q64 RERELEASE texture shift
		if (fa->texinfo->texture->shift > 0)
		{
			poly->verts[i][3] /= ( 2 * fa->texinfo->texture->shift);
			poly->verts[i][4] /= ( 2 * fa->texinfo->texture->shift);
		}

		//
		// lightmap texture coordinates
		//
		s = DotProduct (vec, fa->lmvecs[0]) + fa->lmvecs[0][3] + 0.5;
		s += fa->light_s;
		s /= LMBLOCK_WIDTH;

		t = DotProduct (vec, fa->lmvecs[1]) + fa->lmvecs[1][3] + 0.5;
		t += fa->light_t;
		t /= LMBLOCK_HEIGHT;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	//johnfitz -- removed gl_keeptjunctions code

	poly->numverts = lnumverts;

#ifdef MACBOOK_ARM_HACK // woods #collinear
	// Some GPUs misbehave if fed triangles of empty size.
	if (r_remove_collinear_vertices.value) {
		if (poly->numverts > 4) {
			float (*new_verts)[VERTEXSIZE] = Q_malloc(poly->numverts * sizeof(float[VERTEXSIZE]));
			R_RemoveColinearVertices(poly, new_verts);
			free(new_verts);
		}
		else {
			float new_verts[4][VERTEXSIZE];
			R_RemoveColinearVertices(poly, new_verts);
		}
	}
#endif
}

static void BuildSurfaceDisplayList (msurface_t *fa)
{
	glpoly_t	*poly;

	poly = AllocSurfacePoly (fa);
	BuildSurfaceVerts (currentmodel, fa, poly);

	//oldwater is lame. subdivide it now.
	if ((fa->flags & SURF_DRAWTURB) && !gl_glsl_water_able)
		GL_SubdivideSurface (fa);
}

/*
   Makes sure the model is good to go.
*/
void GL_BuildModel (qmodel_t *m)
{
	int i;
	if (m->name[0] == '*')
		return;
	r_pcurrentvertbase = m->vertexes;
	currentmodel = m;
	for (i=0 ; i<m->numsurfaces ; i++)
	{
		msurface_t *surf = m->surfaces + i;
		//johnfitz -- rewritten to use SURF_DRAWTILED instead of the sky/water flags
		GL_CreateSurfaceLightmap (m, surf);
		if (lm_defer_fill && !((surf->flags & SURF_DRAWTURB) && !gl_glsl_water_able))
		{
			//tb -- allocate now (Hunk_Alloc isn't thread-safe), fill verts + lightmap on workers
			LM_QueueFillJob (m, surf, AllocSurfacePoly (surf));
		}
		else
		{
			BuildSurfaceDisplayList (surf);	// legacy oldwater path needs serial GL_SubdivideSurface
			if (lm_defer_fill)
				LM_QueueFillJob (m, surf, NULL);
		}
		//johnfitz
	}

//	GL_BuildBModelVertexBuffer();
}

/*
==================
GL_BuildLightmaps -- called at level load time

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void GL_BuildLightmaps (void)
{
	char	name[24];
	int		i, j;
	struct lightmap_s *lm;
	qmodel_t	*m;
	double	t_start, t_built, t_uploaded;
	qboolean profile, partial_uploads_enabled;
	size_t	full_upload_bytes, occupied_upload_bytes, transferred_upload_bytes;
	int		partial_upload_count;

	RSceneCache_Shutdown();	//make sure there's nothing poking them off-thread.

	if (!lm_dirty_mutex)
		lm_dirty_mutex = SDL_CreateMutex(); // woods #lmrect -- safe here: worker just shut down, so first use is single-threaded

	// woods #lmrect -- the upload scratch PBO belongs to the (possibly recreated) GL context
	if (lm_scratchpbo)
	{
		if (GL_DeleteBuffersFunc)
			GL_DeleteBuffersFunc(1, &lm_scratchpbo);
		lm_scratchpbo = 0;
	}

	lm_defer_fill = false;	//tb -- drop any fill jobs orphaned by a Host_Error mid-build
	LM_ClearFillJobs ();

	profile = developer.value != 0;
	t_start = profile ? Sys_DoubleTime () : 0;
	partial_uploads_enabled = !COM_CheckParm ("-nofastlightmapupload");
	full_upload_bytes = occupied_upload_bytes = transferred_upload_bytes = 0;
	partial_upload_count = 0;

	r_framecount = 1; // no dlightcache

	//Spike -- wipe out all the lightmap data (johnfitz -- the gltexture objects were already freed by Mod_ClearAll)
	for (i=0; i < lightmap_count; i++)
	{
		if (lightmaps[i].texture)
			TexMgr_FreeTexture(lightmaps[i].texture);
		if (lightmaps[i].pbohandle)
		{
			GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lightmaps[i].pbohandle);
			GL_UnmapBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB);
			GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			GL_DeleteBuffersFunc(1, &lightmaps[i].pbohandle);
		}
		else
			free(lightmaps[i].pbodata);
	}
	free(lightmaps);
	lightmaps = NULL;
	last_lightmap_allocated = 0;
	lightmap_count = 0;

	if ((!q_strcasecmp(r_lightmap_format.string, "rgb9_e5") || !q_strcasecmp(r_lightmap_format.string, "e5bgr9") || !q_strcasecmp(r_lightmap_format.string, "rgb9e5")) && gl_texture_e5bgr9)
		gl_lightmap_format = GL_RGB9_E5;
	else if ((!q_strcasecmp(r_lightmap_format.string, "rgb10_a2") || !q_strcasecmp(r_lightmap_format.string, "rgb10a2") || !q_strcasecmp(r_lightmap_format.string, "rgb10")) && gl_packed_pixels)
		gl_lightmap_format = GL_RGB10_A2;
	else if ( !q_strcasecmp(r_lightmap_format.string, "rgbx8") || !q_strcasecmp(r_lightmap_format.string, "rgba8") || !q_strcasecmp(r_lightmap_format.string, "rgbx") || !q_strcasecmp(r_lightmap_format.string, "rgba"))
		gl_lightmap_format = GL_RGBA;
	else if ( !q_strcasecmp(r_lightmap_format.string, "bgrx8") || !q_strcasecmp(r_lightmap_format.string, "bgra8") || !q_strcasecmp(r_lightmap_format.string, "bgrx") || !q_strcasecmp(r_lightmap_format.string, "bgra"))
		gl_lightmap_format = GL_BGRA;
	else
	{	//requested format unavailable
		if (*r_lightmap_format.string)
			Con_Warning("r_lightmap_format: unsupported format, using default\n");

		if (gl_texture_e5bgr9)// && cl.worldmodel && (cl.worldmodel->flags&MOD_HDRLIGHTING))
			gl_lightmap_format = GL_RGB9_E5; //requires gl3, allowing for hdr lighting (both highs and lows).
		else if (gl_packed_pixels)
			gl_lightmap_format = GL_RGB10_A2;	//upper 2 bits used for extra 4-fold overbright. using a glsl multiplier. available with gl1.1 apparently... but also glsl.
		else
			gl_lightmap_format = GL_RGBA;//FIXME: hardcoded for now!
	}

	switch (gl_lightmap_format)
	{
	case GL_RGB9_E5:
		lightmap_bytes = 4;
		break;
	case GL_RGB10_A2:
		lightmap_bytes = 4;
		break;
	case GL_RGBA:
		lightmap_bytes = 4;
		break;
	case GL_BGRA:
		lightmap_bytes = 4;
		break;
	default:
		Sys_Error ("GL_BuildLightmaps: bad lightmap format");
	}

	lm_defer_fill = true;	//tb -- queue surface fills, then run them on worker threads below
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];
		if (!m)
			continue;
		GL_BuildModel(m);
	}
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache_csqc[j];
		if (!m)
			break;
		GL_BuildModel(m);
	}
	lm_defer_fill = false;
	if (profile)
	{
		double t_fill = Sys_DoubleTime ();
		Con_DPrintf ("GL_BuildLightmaps: surfaces %.1fms\n", (t_fill-t_start)*1000.0);
		LM_RunDeferredFill ();
		Con_DPrintf ("GL_BuildLightmaps: fill %.1fms\n", (Sys_DoubleTime()-t_fill)*1000.0);
	}
	else
		LM_RunDeferredFill ();

	t_built = profile ? Sys_DoubleTime () : 0;

	//
	// upload all lightmaps that were filled
	//
	for (i=0; i<lightmap_count; i++)
	{
		size_t full_bytes, occupied_bytes, saved_bytes;
		qboolean use_partial_upload;

		lm = &lightmaps[i];
		if (lm->upload_height <= 0 || lm->upload_height > LMBLOCK_HEIGHT)
			Sys_Error ("GL_BuildLightmaps: bad occupied height %d for lightmap %d", lm->upload_height, i);

		full_bytes = (size_t)LMBLOCK_WIDTH * (size_t)LMBLOCK_HEIGHT * (size_t)lightmap_bytes;
		occupied_bytes = (size_t)LMBLOCK_WIDTH * (size_t)lm->upload_height * (size_t)lightmap_bytes;
		saved_bytes = full_bytes - occupied_bytes;
		use_partial_upload = partial_uploads_enabled
			&& saved_bytes >= 1024u * 1024u
			&& saved_bytes >= full_bytes / 4u;

		full_upload_bytes += full_bytes;
		occupied_upload_bytes += occupied_bytes;
		transferred_upload_bytes += use_partial_upload ? occupied_bytes : full_bytes;
		if (use_partial_upload)
			partial_upload_count++;

		lm->modified = false;
		lm->rectchange.l = LMBLOCK_WIDTH;
		lm->rectchange.t = LMBLOCK_HEIGHT;
		lm->rectchange.w = 0;
		lm->rectchange.h = 0;
		lm->numdirtyrects = 0; // woods #lmrect

		//johnfitz -- use texture manager
		sprintf(name, "lightmap%07i",i);

		if (use_partial_upload)
		{
			// With an unpack PBO bound, NULL is offset zero rather than "no data".
			// Allocate the full texture first with no PBO, then upload only the
			// occupied rows directly from the persistent PBO or client memory.
			if (GL_BindBufferFunc)
				GL_BindBufferFunc (GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			lm->texture = TexMgr_LoadImage (NULL, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
					SRC_LIGHTMAP, NULL, "", (src_offset_t)lm->pbodata, TEXPREF_LINEAR | TEXPREF_NOPICMIP | TEXPREF_PERSIST);
			GL_Bind (lm->texture);
			if (lm->pbohandle)
			{
				GL_BindBufferFunc (GL_PIXEL_UNPACK_BUFFER_ARB, lm->pbohandle);
				R_TexSubImageLightmap (0, 0, LMBLOCK_WIDTH, lm->upload_height, (const GLvoid *)(uintptr_t)0);
				GL_BindBufferFunc (GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			}
			else
				R_TexSubImageLightmap (0, 0, LMBLOCK_WIDTH, lm->upload_height, lm->pbodata);
		}
		else if (lm->pbohandle)
		{
			GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lm->pbohandle);
			lm->texture = TexMgr_LoadImage (NULL, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
							SRC_LIGHTMAP, NULL, "", (src_offset_t)lm->pbodata, TEXPREF_LINEAR | TEXPREF_NOPICMIP | TEXPREF_PERSIST);
			GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
		}
		else
		{
			lm->texture = TexMgr_LoadImage (NULL, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
					SRC_LIGHTMAP, lm->pbodata, "", (src_offset_t)lm->pbodata, TEXPREF_LINEAR | TEXPREF_NOPICMIP | TEXPREF_PERSIST);
		}
		//johnfitz
	}

	//johnfitz -- warn about exceeding old limits
	//GLQuake limit was 64 textures of 128x128. Estimate how many 128x128 textures we would need
	//given that we are using lightmap_count of LMBLOCK_WIDTH x LMBLOCK_HEIGHT
	i = lightmap_count * ((LMBLOCK_WIDTH / 128) * (LMBLOCK_HEIGHT / 128));
	if (i > 64)
		Con_DWarning("%i lightmaps exceeds standard limit of 64.\n",i);
	//johnfitz

	if (profile)
	{
		t_uploaded = Sys_DoubleTime ();
		Con_DPrintf ("GL_BuildLightmaps: occupied %.1f/%.1f MiB, transferred %.1f MiB (%d/%d partial%s)\n",
			occupied_upload_bytes / (1024.0 * 1024.0), full_upload_bytes / (1024.0 * 1024.0),
			transferred_upload_bytes / (1024.0 * 1024.0), partial_upload_count, lightmap_count,
			partial_uploads_enabled ? "" : ", disabled");
		Con_DPrintf ("GL_BuildLightmaps: build %.1fms upload %.1fms (%d lightmaps)\n",
			(t_built-t_start)*1000.0, (t_uploaded-t_built)*1000.0, lightmap_count);
	}
}

/*
=============================================================

	VBO support

=============================================================
*/

GLuint gl_bmodel_vbo = 0;
GLuint gl_bmodel_instance_vbo = 0;
unsigned int gl_bmodel_vbo_generation = 0;	//tb -- bumped whenever vbo_firstvert offsets change, to invalidate per-model draw caches

void GL_DeleteBModelVertexBuffer (void)
{
	R_BModelDrawCache_CleanupAll ();

	if (!(gl_vbo_able && gl_mtexable && gl_max_texture_units >= 3))
		return;

	GL_DeleteBuffersFunc (1, &gl_bmodel_vbo);
	if (gl_bmodel_instance_vbo)
		GL_DeleteBuffersFunc (1, &gl_bmodel_instance_vbo);
	gl_bmodel_vbo = 0;
	gl_bmodel_instance_vbo = 0;

	GL_ClearBufferBindings ();
}

/*
==================
GL_BuildBModelVertexBuffer

Deletes gl_bmodel_vbo if it already exists, then rebuilds it with all
surfaces from world + all brush models
==================
*/
void GL_BuildBModelVertexBuffer (void)
{
	size_t		numverts, varray_bytes, varray_index;
	int		i, j;
	qmodel_t	*m;
	float		*varray;
	double		t_start;
	qboolean	profile;

	if (!(gl_vbo_able && gl_mtexable && gl_max_texture_units >= 3))
		return;

	profile = developer.value != 0;
	t_start = profile ? Sys_DoubleTime () : 0;

// ask GL for a name for our VBO
	GL_DeleteBuffersFunc (1, &gl_bmodel_vbo);
	GL_GenBuffersFunc (1, &gl_bmodel_vbo);

// count all verts in all models
	numverts = 0;
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			if (m->surfaces[i].numedges < 0 || (size_t)m->surfaces[i].numedges > (size_t)INT_MAX - numverts)
				Sys_Error ("GL_BuildBModelVertexBuffer: too many vertices");
			numverts += (size_t)m->surfaces[i].numedges;
		}
	}
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache_csqc[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			if (m->surfaces[i].numedges < 0 || (size_t)m->surfaces[i].numedges > (size_t)INT_MAX - numverts)
				Sys_Error ("GL_BuildBModelVertexBuffer: too many vertices");
			numverts += (size_t)m->surfaces[i].numedges;
		}
	}

// build vertex array
	if (numverts > SIZE_MAX / (VERTEXSIZE * sizeof(float)))
		Sys_Error ("GL_BuildBModelVertexBuffer: vertex buffer too large");
	varray_bytes = VERTEXSIZE * sizeof(float) * numverts;
	varray = (float *) malloc (varray_bytes);
	if (!varray && varray_bytes)
		Sys_Error ("GL_BuildBModelVertexBuffer: out of memory (%" SDL_PRIu64 " bytes)", (uint64_t)varray_bytes);
	varray_index = 0;

	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			msurface_t *s = &m->surfaces[i];
			size_t nv = (size_t)R_SurfaceVertCount (s);	// woods #collinear -- poly may hold fewer verts than numedges
			s->vbo_firstvert = (int)varray_index;
			memcpy (&varray[VERTEXSIZE * varray_index], s->polys->verts, VERTEXSIZE * sizeof(float) * nv);
			varray_index += nv;
		}
	}
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache_csqc[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			msurface_t *s = &m->surfaces[i];
			size_t nv = (size_t)R_SurfaceVertCount (s);	// woods #collinear -- poly may hold fewer verts than numedges
			s->vbo_firstvert = (int)varray_index;
			memcpy (&varray[VERTEXSIZE * varray_index], s->polys->verts, VERTEXSIZE * sizeof(float) * nv);
			varray_index += nv;
		}
	}

// upload to GPU
	GL_BindBufferFunc (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BufferDataFunc (GL_ARRAY_BUFFER, (GLsizeiptr)varray_bytes, varray, GL_STATIC_DRAW);
	free (varray);

	gl_bmodel_vbo_generation++;	//tb -- vbo_firstvert offsets just changed; invalidate bmodel draw caches

// invalidate the cached bindings
	GL_ClearBufferBindings ();

	if (profile)
		Con_DPrintf ("GL_BuildBModelVertexBuffer: %.1fms (%u verts)\n",
			(Sys_DoubleTime()-t_start)*1000.0, (unsigned int)numverts);
}

/*
===============
R_AddDynamicLights
===============
*/
static void R_AddDynamicLights (msurface_t *surf, unsigned *blocklights, entity_t *currentent, dlight_t *lights, const int *lightstyles)
{
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	//johnfitz -- lit support via lordhavoc
	float		cred, cgreen, cblue, brightness, stylescale;
	unsigned	*bl;
	//johnfitz
	vec3_t		lightofs;	//Spike: light surfaces based upon where they are now instead of their default position.

	smax = surf->extents[0]+1;
	tmax = surf->extents[1]+1;

	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		if (! (surf->dlightbits[lnum >> 5] & (1U << (lnum & 31))))
			continue;		// not lit by this light

		rad = lights[lnum].radius;
		if (lights[lnum].style < 0 || lights[lnum].style >= MAX_LIGHTSTYLES)
			stylescale = 1.0f;
		else
			stylescale = lightstyles[lights[lnum].style] * (1.0f / 256.0f);
		if (stylescale <= 0.0f)
			continue;
		if (currentent->currentangles[0] || currentent->currentangles[1] || currentent->currentangles[2])
		{
			vec3_t temp, axis[3];
			VectorSubtract(lights[lnum].origin, currentent->origin, temp);
			AngleVectors(currentent->currentangles, axis[0], axis[1], axis[2]);
			lightofs[0] = +DotProduct(temp, axis[0]);
			lightofs[1] = -DotProduct(temp, axis[1]);
			lightofs[2] = +DotProduct(temp, axis[2]);
		}
		else
			VectorSubtract(lights[lnum].origin, currentent->origin, lightofs);
		dist = DotProduct (lightofs, surf->plane->normal) - surf->plane->dist;
		rad -= fabs(dist);
		minlight = lights[lnum].minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = lightofs[i] -
					surf->plane->normal[i]*dist;
		}

		local[0] = DotProduct (impact, surf->lmvecs[0]) + surf->lmvecs[0][3];
		local[1] = DotProduct (impact, surf->lmvecs[1]) + surf->lmvecs[1][3];

		//johnfitz -- lit support via lordhavoc
		bl = blocklights;
		cred = lights[lnum].color[0] * 256.0f * stylescale;
		cgreen = lights[lnum].color[1] * 256.0f * stylescale;
		cblue = lights[lnum].color[2] * 256.0f * stylescale;
		//johnfitz
		for (t = 0 ; t<tmax ; t++)
		{
			td = (local[1] - t)*surf->lmvecscale[1];
			if (td < 0)
				td = -td;
			for (s=0 ; s<smax ; s++)
			{
				sd = (local[0] - s)*surf->lmvecscale[0];
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);
				if (dist < minlight)
				//johnfitz -- lit support via lordhavoc
				{
					brightness = rad - dist;
					bl[0] += (int) (brightness * cred);
					bl[1] += (int) (brightness * cgreen);
					bl[2] += (int) (brightness * cblue);
				}
				bl += 3;
				//johnfitz
			}
		}
	}
}


/*
===============
R_BuildLightMap -- johnfitz -- revised for lit support via lordhavoc

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_LightmapBuildState_Snapshot (r_lightmap_buildstate_t *state)
{
	state->format = gl_lightmap_format;
	state->overbright = !!gl_overbright.value;
	state->ambient_light = 0;
	if (!(cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cls.demoplayback))
		state->ambient_light = ((unsigned)CLAMP(0.0f, r_ambient.value, 255.0f)) << 8;
	memcpy(state->lightstyles, d_lightstylevalue, sizeof(state->lightstyles));
}

void R_BuildLightMapForState (qmodel_t *model, msurface_t *surf, byte *dest, int stride, entity_t *currentent, int framecount, dlight_t *lights, const r_lightmap_buildstate_t *state)
{
	const int overbright = state->overbright;

	int			smax, tmax;
	unsigned		r, g, b;
	int			i, j, size;
	unsigned	scale;
	int			maps;
	unsigned	*bl;
	unsigned	*blocklights;	//moved this to stack, so workers working on the worldmodel won't fight the main thread processing the submodels.

	surf->cached_dlight = (surf->dlightframe == framecount);

	smax = surf->extents[0]+1;
	tmax = surf->extents[1]+1;
	size = smax*tmax;

	blocklights = alloca(size*3*sizeof(*blocklights));	//alloca is unsafe, but at least we memset it... should probably memset in stack order in the hopes of getting a standard stack-overflow-segfault instead of poking completely outside what the system thinks is the stack in the case of massive surfs...

	if (model->lightdata)
	{
	// clear to no light
		memset (&blocklights[0], 0, size * 3 * sizeof (unsigned int)); //johnfitz -- lit support via lordhavoc

		if (state->ambient_light) // woods #rambient
		{
			unsigned ambient_light = state->ambient_light;

			if (ambient_light) {
				bl = blocklights;
				for (i = 0; i < size; ++i) {
					*bl++ = ambient_light;
					*bl++ = ambient_light;
					*bl++ = ambient_light;
				}
			}
		}

	// add all the lightmaps
		if (!surf->samples)
			;	//unlit surfaces are black... FIXME: unless lit water (could be new-qbsp + old-light)...
		else if (model->flags & MOD_HDRLIGHTING)
		{
			uint32_t	*lightmap = surf->samples;
			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != INVALID_LIGHTSTYLE ;
				 maps++)
			{
				scale = state->lightstyles[surf->styles[maps]];
				surf->cached_light[maps] = scale;	// 8.8 fraction
				bl = blocklights;		//it sucks that blocklights is an int array. we can still massively overbright though, just not underbright quite as accurately (still quite a bit more than rgb8 precision there).
				for (i=0 ; i<size ; i++)
				{
					static const float rgb9e5tab[32] = {	//multipliers for the 9-bit mantissa, according to the biased mantissa
						//aka: pow(2, biasedexponent - bias-bits) where bias is 15 and bits is 9
						1.0/(1<<24),	1.0/(1<<23),	1.0/(1<<22),	1.0/(1<<21),	1.0/(1<<20),	1.0/(1<<19),	1.0/(1<<18),	1.0/(1<<17),
						1.0/(1<<16),	1.0/(1<<15),	1.0/(1<<14),	1.0/(1<<13),	1.0/(1<<12),	1.0/(1<<11),	1.0/(1<<10),	1.0/(1<<9),
						1.0/(1<<8),		1.0/(1<<7),		1.0/(1<<6),		1.0/(1<<5),		1.0/(1<<4),		1.0/(1<<3),		1.0/(1<<2),		1.0/(1<<1),
						1.0,			1.0*(1<<1),		1.0*(1<<2),		1.0*(1<<3),		1.0*(1<<4),		1.0*(1<<5),		1.0*(1<<6),		1.0*(1<<7),
					};
					uint32_t e5bgr9 = *lightmap++;
					float e = rgb9e5tab[e5bgr9>>27] * (1<<7) * scale;	//we're converting to a scale that holds overbrights, so 1->128, its 2->255ish
					*bl++ += e*((e5bgr9>> 0)&0x1ff);	//red
					*bl++ += e*((e5bgr9>> 9)&0x1ff);	//green
					*bl++ += e*((e5bgr9>>18)&0x1ff);	//blue
				}
			}
		}
		else
		{
			byte	*lightmap = surf->samples;
			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != INVALID_LIGHTSTYLE ;
				 maps++)
			{
				scale = state->lightstyles[surf->styles[maps]];
				surf->cached_light[maps] = scale;	// 8.8 fraction
				//johnfitz -- lit support via lordhavoc
				bl = blocklights;
				for (i=0 ; i<size ; i++)
				{
					*bl++ += *lightmap++ * scale;
					*bl++ += *lightmap++ * scale;
					*bl++ += *lightmap++ * scale;
				}
				//johnfitz
			}
		}

	// add all the dynamic lights
		if (surf->dlightframe == framecount)
			R_AddDynamicLights (surf, blocklights, currentent, lights, state->lightstyles);
	}
	else
	{
	// set to full bright if no light data
		for (i=0 ; i<size * 3; i++) // woods -- fix lightmap initialization for full bright surfaces
			blocklights[i] = 0xffff;	//don't use memset, it oversaturates FAR too much with hdr...
	}

// bound, invert, and shift
// store:
	switch (state->format)
	{
	case GL_RGB9_E5:
		{
			int e;
			float m;
			float scale, identity = 1u<<((overbright?8:7)+8);	//overbright is redundant with this, but its easier to leave it than conditionally block it.
			stride -= smax * 4;
			bl = blocklights;
			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
				{
					e = 0;
					m = q_max(q_max(bl[0], bl[1]), bl[2])/identity;
					if (!overbright && m > 1.0)
						m = 1.0; //clamp it to a logical 1.
					if (m >= 0.5)
					{	//positive exponent
						while (m > (1<<(e)) && e < 30-15)	//don't do nans.
							e++;
					}
					else
					{	//negative exponent...
						while (m < 1/(1<<-e) && e > -15)	//don't do denormals.
							e--;
					}
					scale = pow(2, e-9);
					scale *= identity;
					*(unsigned int *)dest = ((e+15)<<27) |
											CLAMP(0, (int)(bl[0]/scale + 0.5), 0x1ff)<<0 |
											CLAMP(0, (int)(bl[1]/scale + 0.5), 0x1ff)<<9 |
											CLAMP(0, (int)(bl[2]/scale + 0.5), 0x1ff)<<18;
					bl += 3;
					dest += 4;
				}
			}
		}
		break;
	case GL_RGB10_A2:
		stride -= smax * 4;
		bl = blocklights;
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				if (overbright)
				{
					r = *bl++ >> 8;
					g = *bl++ >> 8;
					b = *bl++ >> 8;

					r = (r > 1023)? 1023 : r;
					g = (g > 1023)? 1023 : g;
					b = (b > 1023)? 1023 : b;
				}
				else
				{
					r = *bl++ >> 7;
					g = *bl++ >> 7;
					b = *bl++ >> 7;

					// artifically clamp to 255 so gl_overbright 0 renders as expected in the wide10bits case
					r = (r > 255) ? 255 : r;
					g = (g > 255) ? 255 : g;
					b = (b > 255) ? 255 : b;
				}

				*(unsigned int*)dest = (r<<22) | (g<<12) | (b<<2) | 3;
				dest += 4;
			}
		}
		break;
	case GL_RGBA:
		stride -= smax * 4;
		bl = blocklights;
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				if (overbright)
				{
					r = *bl++ >> 8;
					g = *bl++ >> 8;
					b = *bl++ >> 8;
				}
				else
				{
					r = *bl++ >> 7;
					g = *bl++ >> 7;
					b = *bl++ >> 7;
				}
				*dest++ = (r > 255)? 255 : r;
				*dest++ = (g > 255)? 255 : g;
				*dest++ = (b > 255)? 255 : b;
				*dest++ = 255;
			}
		}
		break;
	case GL_BGRA:
		stride -= smax * 4;
		bl = blocklights;
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				if (overbright)
				{
					r = *bl++ >> 8;
					g = *bl++ >> 8;
					b = *bl++ >> 8;
				}
				else
				{
					r = *bl++ >> 7;
					g = *bl++ >> 7;
					b = *bl++ >> 7;
				}
				*dest++ = (b > 255)? 255 : b;
				*dest++ = (g > 255)? 255 : g;
				*dest++ = (r > 255)? 255 : r;
				*dest++ = 255;
			}
		}
		break;
	default:
		Sys_Error ("R_BuildLightMap: bad lightmap format");
	}
}

void R_BuildLightMap (qmodel_t *model, msurface_t *surf, byte *dest, int stride, entity_t *currentent, int framecount, dlight_t *lights)
{
	r_lightmap_buildstate_t state;

	R_LightmapBuildState_Snapshot(&state);
	R_BuildLightMapForState(model, surf, dest, stride, currentent, framecount, lights, &state);
}

/*
===============
R_UploadLightmap -- johnfitz -- uploads the modified lightmap to opengl if necessary

assumes lightmap texture is already bound
===============
*/
static void R_TexSubImageLightmap (int x, int t, int w, int h, const GLvoid *src) // woods #lmrect
{
	if (gl_lightmap_format == GL_RGB9_E5)
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, t, w, h, GL_RGB,
				GL_UNSIGNED_INT_5_9_9_9_REV, src);
	else if (gl_lightmap_format == GL_RGB10_A2)
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, t, w, h, GL_RGBA,
				GL_UNSIGNED_INT_10_10_10_2, src);
	else
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, t, w, h, gl_lightmap_format,
				GL_UNSIGNED_BYTE, src);
}

static void R_UploadLightmapRows (struct lightmap_s *lm, int x, int t, int w, int h) // woods #lmbands #lmrect
{
	size_t offset = ((size_t)t*LMBLOCK_WIDTH + x)*lightmap_bytes;

	if (lm->pbohandle)
	{	// persistently-mapped PBO, bound by the caller: src is a byte offset; rows use the atlas stride
		if (w != LMBLOCK_WIDTH)
			glPixelStorei(GL_UNPACK_ROW_LENGTH, LMBLOCK_WIDTH);
		R_TexSubImageLightmap(x, t, w, h, (const GLvoid*)(uintptr_t)offset);
		if (w != LMBLOCK_WIDTH)
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		return;
	}

	if (gl_vbo_able && GL_GenBuffersFunc && GL_BufferDataFunc && GL_MapBufferFunc && GL_UnmapBufferFunc)
	{
		size_t rowbytes = (size_t)w*lightmap_bytes;
		byte *dst;

		if (!lm_scratchpbo)
			GL_GenBuffersFunc(1, &lm_scratchpbo);
		if (lm_scratchpbo)
		{
			GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lm_scratchpbo);
			GL_BufferDataFunc(GL_PIXEL_UNPACK_BUFFER_ARB, rowbytes*h, NULL, GL_STREAM_DRAW);	//orphan; never stalls
			dst = (byte *)GL_MapBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY);
			if (dst)
			{
				const byte *srcrow = lm->pbodata + offset;
				int r;
				for (r = 0; r < h; r++, dst += rowbytes, srcrow += (size_t)LMBLOCK_WIDTH*lightmap_bytes)
					memcpy(dst, srcrow, rowbytes);
				GL_UnmapBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB);
				R_TexSubImageLightmap(x, t, w, h, NULL);	//rows are packed tight, ROW_LENGTH stays 0
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
				return;
			}
			GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
		}
	}

	// fallback: direct client-memory upload
	if (w != LMBLOCK_WIDTH)
		glPixelStorei(GL_UNPACK_ROW_LENGTH, LMBLOCK_WIDTH);
	R_TexSubImageLightmap(x, t, w, h, (const GLvoid*)(lm->pbodata + offset));
	if (w != LMBLOCK_WIDTH)
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

static void R_UploadLightmap(int lmap)
{
	struct lightmap_s *lm = &lightmaps[lmap];
	glRect_t rects[LM_DIRTYRECTS]; // woods #lmrect
	int i, n;

	if (!lm->modified)
		return;

	// woods #lmrect -- snapshot-and-clear under the same lock
	// R_LightmapMarkDirtyRect appends under, so a mark from the scenecache
	// worker either lands before the snapshot (uploaded now, its staging bytes
	// are complete because marks happen after R_BuildLightMap) or after the
	// clear (stays in the list for the next upload). Without this, clearing the
	// list mid-job dropped the worker's marks and left dead dlights baked into
	// the GPU texture.
	SDL_LockMutex(lm_dirty_mutex);
	lm->modified = false;
	n = lm->numdirtyrects;
	if (n > LM_DIRTYRECTS)
		n = LM_DIRTYRECTS;
	if (n > 0)
		memcpy(rects, lm->dirtyrects, n * sizeof(rects[0]));
	lm->numdirtyrects = 0;
	lm->rectchange.l = LMBLOCK_WIDTH;
	lm->rectchange.t = LMBLOCK_HEIGHT;
	lm->rectchange.h = 0;
	lm->rectchange.w = 0;
	SDL_UnlockMutex(lm_dirty_mutex);

	if (n <= 0)
		return; // nothing dirty (modified flag without rects; shouldn't happen)

	if (lm->pbohandle)
		GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lm->pbohandle);

	// woods #lmrect -- upload each accumulated dirty rect; bytes stay
	// proportional to the texels actually rebuilt rather than full-width bands
	// of the (up to 512x16384) atlas, which made per-frame dlight updates cost
	// megabytes of texture traffic on Apple's GL-on-Metal.
	for (i = 0; i < n; i++)
	{
		int x = rects[i].l, t = rects[i].t;
		int w = rects[i].w, h = rects[i].h;
		if (x >= LMBLOCK_WIDTH || t >= LMBLOCK_HEIGHT)
			continue;
		w = q_min(w, LMBLOCK_WIDTH - x);
		h = q_min(h, LMBLOCK_HEIGHT - t);
		if (w <= 0 || h <= 0)
			continue;
		R_UploadLightmapRows(lm, x, t, w, h);
	}

	if (lm->pbohandle)
		GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

	rs_dynamiclightmaps++;
}

void R_UploadLightmaps (void)
{
	int lmap;

	if (lightmaps_latecached)
	{
		GL_BuildLightmaps ();
		GL_BuildBModelVertexBuffer ();
		lightmaps_latecached=false;
	}

	if (lightmaps_skipupdates)
		return;

	for (lmap = 0; lmap < lightmap_count; lmap++)
	{
		if (!lightmaps[lmap].modified)
			continue;

		if (!lightmaps[lmap].texture)
		{
			char	name[24];
			sprintf(name, "lightmap%07i",lmap);
			if (lightmaps[lmap].pbohandle)
			{
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lightmaps[lmap].pbohandle);
				lightmaps[lmap].texture = TexMgr_LoadImage (NULL, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
								SRC_LIGHTMAP, NULL, "", (src_offset_t)lightmaps[lmap].pbodata, TEXPREF_LINEAR | TEXPREF_NOPICMIP | TEXPREF_PERSIST);
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			}
			else
			{
				lightmaps[lmap].texture = TexMgr_LoadImage (NULL, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
								SRC_LIGHTMAP, lightmaps[lmap].pbodata, "", (src_offset_t)lightmaps[lmap].pbodata, TEXPREF_LINEAR | TEXPREF_NOPICMIP | TEXPREF_PERSIST);
			}
			lightmaps[lmap].modified = false;
			lightmaps[lmap].rectchange.l = LMBLOCK_WIDTH;
			lightmaps[lmap].rectchange.t = LMBLOCK_HEIGHT;
			lightmaps[lmap].rectchange.h = 0;
			lightmaps[lmap].rectchange.w = 0;
			lightmaps[lmap].numdirtyrects = 0; // woods #lmrect
		}
		else
		{
			GL_Bind (lightmaps[lmap].texture);
			R_UploadLightmap(lmap);
		}
	}
}

/*
================
R_RebuildAllLightmaps -- johnfitz -- called when gl_overbright gets toggled
================
*/
void R_RebuildAllLightmaps (void)
{
	int			i, j;
	qmodel_t	*mod;
	msurface_t	*fa;
	byte		*base;

	if (!cl.worldmodel) // is this the correct test?
		return;

	//for each surface in each model, rebuild lightmap with new scale
	for (i=1; i<MAX_MODELS; i++)
	{
		if (!(mod = cl.model_precache[i]))
			continue;
		fa = &mod->surfaces[mod->firstmodelsurface];
		for (j=0; j<mod->nummodelsurfaces; j++, fa++)
		{
			if (fa->flags & SURF_DRAWTILED)
				continue;
			base = lightmaps[fa->lightmaptexturenum].pbodata;
			base += fa->light_t * LMBLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
			R_BuildLightMap (mod, fa, base, LMBLOCK_WIDTH*lightmap_bytes, currententity, r_framecount, cl_dlights);
		}
	}

	//for each lightmap, upload it
	for (i=0; i<lightmap_count; i++)
	{
		if (!lightmaps[i].texture)
		{
			char	name[24];
			sprintf(name, "lightmap%07i",i);
			if (lightmaps[i].pbohandle)
			{
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lightmaps[i].pbohandle);
				lightmaps[i].texture = TexMgr_LoadImage (NULL, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
								SRC_LIGHTMAP, NULL, "", (src_offset_t)lightmaps[i].pbodata, TEXPREF_LINEAR | TEXPREF_NOPICMIP | TEXPREF_PERSIST);
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			}
			else
			{
				lightmaps[i].texture = TexMgr_LoadImage (NULL, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
								SRC_LIGHTMAP, lightmaps[i].pbodata, "", (src_offset_t)lightmaps[i].pbodata, TEXPREF_LINEAR | TEXPREF_NOPICMIP | TEXPREF_PERSIST);
			}
		}
		else
		{
			GL_Bind (lightmaps[i].texture);
			if (lightmaps[i].pbohandle)
			{
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lightmaps[i].pbohandle);
				if (gl_lightmap_format == GL_RGB9_E5)
					glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, GL_RGB,
							GL_UNSIGNED_INT_5_9_9_9_REV, NULL);
				else if (gl_lightmap_format == GL_RGB10_A2)
					glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, GL_RGBA,
							GL_UNSIGNED_INT_10_10_10_2, NULL);
				else
					glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, gl_lightmap_format,
							GL_UNSIGNED_BYTE, NULL);
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			}
			else
			{
				if (gl_lightmap_format == GL_RGB9_E5)
					glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, GL_RGB,
							GL_UNSIGNED_INT_5_9_9_9_REV, lightmaps[i].pbodata);
				else if (gl_lightmap_format == GL_RGB10_A2)
					glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, GL_RGBA,
							GL_UNSIGNED_INT_10_10_10_2, lightmaps[i].pbodata);
				else
					glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, gl_lightmap_format,
							GL_UNSIGNED_BYTE, lightmaps[i].pbodata);
			}
		}

		lightmaps[i].modified = false;
		lightmaps[i].rectchange.l = LMBLOCK_WIDTH;
		lightmaps[i].rectchange.t = LMBLOCK_HEIGHT;
		lightmaps[i].rectchange.h = 0;
		lightmaps[i].rectchange.w = 0;
		lightmaps[i].numdirtyrects = 0; // woods #lmrect -- whole texture just uploaded
	}
}

extern vec3_t	lightcolor; // woods #shadow
extern	vec3_t	lightspot; // woods #shadow
extern qboolean GL_DrawAliasShadowCheck (entity_t* e); // woods #shadow

#define SHADOW_SKEW_X -0.7 //skew along x axis. -0.7 to mimic glquake shadows -- woods #shadow
#define SHADOW_SKEW_Y 0.2 //skew along y axis. 0 to mimic glquake shadows -- woods #shadow
#define SHADOW_VSCALE 0 //0=completely flat -- woods #shadow
#define SHADOW_HEIGHT 0.1 //how far above the floor to render the shadow -- woods #shadow

#define SHADOW_COMPUTED (1 << 0) // woods #shadow
#define SHADOW_VALID    (1 << 1) // woods #shadow

void GL_DrawBrushShadow (entity_t* e) // woods #shadow
{
    qmodel_t* clmodel;
    float     entalpha;
    float     shade, lheight;
    float     shadowmatrix[16] = {
        1,              0,              0,              0,
        0,              1,              0,              0,
        SHADOW_SKEW_X,  SHADOW_SKEW_Y,  SHADOW_VSCALE,  0,
        0,              0,              SHADOW_HEIGHT,   1
    };

	if (!r_shadows_bmodels.value)
		return;

	clmodel = e->model;

	if (R_CullModelForEntity(e))
	{
		return;
	}

	if (e == &cl.viewent ||
		(e->effects & EF_NOSHADOW) ||
		(e->model->flags & MOD_NOSHADOW) ||
		clmodel == cl.worldmodel ||
		!clmodel->nummodelsurfaces) 
	{
		return;
	}

	entalpha = ENTALPHA_DECODE(e->alpha);

	if (entalpha < 1) {
		return;
	}

	if (r_shadows_groundcheck.value && e->model->type == mod_brush) {
		if (!(e->shadow_state & SHADOW_COMPUTED))
			GL_DrawAliasShadowCheck(e);

		if (!(e->shadow_state & SHADOW_VALID))
			return;
	}

    // Determine lighting at entity origin
    R_LightPoint(e->origin);
    shade = ((lightcolor[0] + lightcolor[1] + lightcolor[2]) / 3) / 128.0f;
    lheight = e->origin[2] - lightspot[2];

    clmodel = e->model;

    glPushMatrix();

    // Apply entity transformations
    R_RotateForEntity(e->origin, e->angles, e);

    // Move down to floor, apply shadow projection, then move back
    glTranslatef(0, 0, -lheight);
    glMultMatrixf(shadowmatrix);
    glTranslatef(0, 0, lheight);

    // Set up rendering states for shadow
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    
    // Enable polygon offset to prevent z-fighting
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1, -2);

    // Draw fully black, but alpha scaled by shade and the r_shadows cvar
    glColor4f(0, 0, 0, entalpha * shade * r_shadows.value);

    // Draw the model geometry as a flat polygon silhouette
    {
        msurface_t* surf = &clmodel->surfaces[clmodel->firstmodelsurface];
        int i;

        for (i = 0; i < clmodel->nummodelsurfaces; i++, surf++)
        {
            glpoly_t* p = surf->polys;
            float* v = p->verts[0];
            int k;

            glBegin(GL_POLYGON);
            for (k = 0; k < p->numverts; k++, v += VERTEXSIZE)
            {
                glVertex3fv(v);
            }
            glEnd();
        }
    }

    // Restore states
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);

    glPopMatrix();
}

static float    r_ambient_prev = FLT_MAX; // woods #rambient
static qboolean r_ambient_warned = false; // woods #rambient

void R_Ambient_OnChange_f(cvar_t* var) // woods #rambient
{
	/* Block during live online deathmatch; explain once. */
	if (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cls.demoplayback)
	{
		if (var->value != 0.0f) {
			if (!r_ambient_warned) {
				Con_Printf("r_ambient is disabled during online deathmatch.\n");
				r_ambient_warned = true;
			}
			/* Force back to 0 without spamming rebuilds. */
			if (r_ambient_prev != 0.0f)
				r_ambient_prev = 0.0f;
			Cvar_SetValueQuick(var, 0.0f); /* may re-enter; we early-return */
		}
		return;
	}
	else
	{
		/* Outside DM: allow again; reset one-shot warning */
		r_ambient_warned = false;
	}

	/* Clamp using CLAMP macro (0..255 expected by 8.8 ambient path). */
	const float clamped = (float)CLAMP(0.0f, var->value, 255.0f);
	if (clamped != var->value) {
		Cvar_SetValueQuick(var, clamped);
		return; /* let the re-invocation handle rebuild with clamped value */
	}

	/* Avoid redundant rebuilds. */
	if (r_ambient_prev == clamped)
		return;
	r_ambient_prev = clamped;

	/* If world isn’t ready yet (early init / between maps), skip. */
	if (!cl.worldmodel)
		return;

	/* Rebuild all atlases so ambient is baked into lightmaps immediately. */
	R_RebuildAllLightmaps();
	Con_DPrintf("r_ambient changed to %.2f — rebuilt lightmaps.\n", clamped);
}
