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
static qboolean lightmap_settings_pending;

void R_LightmapFormatChanged (cvar_t *var)
{
	if (host_initialized && !isDedicated)
		lightmap_settings_pending = true;
}

void R_ApplyLightmapSettings (void)
{
	qboolean saved_loading;
	if (!lightmap_settings_pending || !cl.worldmodel || cls.signon != SIGNONS)
		return;
	lightmap_settings_pending = false;
	saved_loading = scr_disabled_for_loading;
	scr_disabled_for_loading = true;
	GL_BuildLightmaps ();
	GL_BuildBModelVertexBuffer ();
	lightmaps_latecached = false;
	scr_disabled_for_loading = saved_loading;
}
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
static SDL_Mutex *lm_dirty_mutex; // woods #lmrect -- guards modified/rectchange/dirtytiles between the scenecache worker and the main thread's uploads
static int lm_tilew = 16, lm_tilerows; // woods #lmtiles -- set by LM_AllocateStaging from the current atlas size
// Bumped each time a lightmap is flagged modified. R_UploadLightmaps runs once per
// brush entity drawn, so on maps with hundreds of them it skips its scan of every
// lightmap when nothing was flagged since the last one.
static SDL_AtomicInt lm_dirty_serial;
static int lm_uploaded_serial = -1;
int lightmap_rebuilds; // woods #dlightskip -- lets the scenecache tell its baked dlights were wiped

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

// woods #lmtiles -- whole atlas dirty (fresh allocation)
static void LM_MarkAllDirty (struct lightmap_s *lm)
{
	int cols = (LMBLOCK_WIDTH + lm_tilew - 1) / lm_tilew;
	unsigned int full = cols >= 32 ? 0xffffffffu : ((1u << cols) - 1u);
	int i;

	for (i = 0; i < lm_tilerows; i++)
		lm->dirtytiles[i] = full;
	lm->dirtylo = 0;
	lm->dirtyhi = lm_tilerows - 1;
}

// woods #lmtiles -- nothing pending; caller holds lm_dirty_mutex or owns the lightmap
static void LM_ClearDirty (struct lightmap_s *lm)
{
	if (lm->dirtytiles && lm->dirtylo <= lm->dirtyhi)
		memset (lm->dirtytiles + lm->dirtylo, 0, (lm->dirtyhi - lm->dirtylo + 1) * sizeof(*lm->dirtytiles));
	lm->dirtylo = lm_tilerows;
	lm->dirtyhi = -1;
}

static void LM_AllocateStaging (struct lightmap_s *lm)
{
	const size_t bytes = (size_t)4 * (size_t)LMBLOCK_WIDTH * (size_t)LMBLOCK_HEIGHT;

	lm_tilew = q_max (1, (LMBLOCK_WIDTH + 31) / 32);
	lm_tilerows = (LMBLOCK_HEIGHT + LM_TILE_H - 1) / LM_TILE_H;
	lm->dirtytiles = (unsigned int *) calloc (lm_tilerows, sizeof(*lm->dirtytiles));
	if (!lm->dirtytiles)
		Sys_Error ("GL_BuildLightmaps: out of memory on %u dirty tile rows", (unsigned int)lm_tilerows);
	lm->dirtylo = lm_tilerows;
	lm->dirtyhi = -1;

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

/*
=================
R_DrawBrushModel
=================
*/
/*
=================
R_SetupBrushModel / R_BeginBrushModel

Scene-cache skip, culling, dynamic light marking and the entity transform,
shared by R_DrawBrushModel and the translucent runs. False if nothing is
drawn; otherwise modelorg is set, and R_BeginBrushModel has also pushed the
entity matrix (R_PushBrushModelMatrix).
=================
*/
void R_EntityAxisToLocal (const entity_t *e, vec3_t point)
{
	const vec3_t *axis = e->csqc_axis;
	vec3_t inverse[3], local;
	float invdet;

	CrossProduct(axis[1], axis[2], inverse[0]);
	CrossProduct(axis[2], axis[0], inverse[1]);
	CrossProduct(axis[0], axis[1], inverse[2]);
	invdet = 1.0f / DotProduct(axis[0], inverse[0]);
	local[0] = DotProduct(inverse[0], point) * invdet;
	local[1] = DotProduct(inverse[1], point) * invdet;
	local[2] = DotProduct(inverse[2], point) * invdet;
	VectorCopy(local, point);
}

static qboolean R_SetupBrushModel (entity_t *e)
{
	int			k;
	qmodel_t	*clmodel;
	vec3_t		lightorg;
	extern byte *skipsubmodels;

	if (!e->csqc_renderflags && e->model->submodelof == cl.worldmodel &&
		skipsubmodels &&
		skipsubmodels[e->model->submodelidx>>3]&(1u<<(e->model->submodelidx&7)))
		return false;	//its in the scenecache that we're drawing. don't draw it twice (and certainly not the slow way).

	if (R_CullModelForEntity(e))
		return false;

	currententity = e;
	clmodel = e->model;

	vec3_t		forward, right, up;
	qboolean	rotated;

	if (r_refdef.csqc.projection.orthographic)
		VectorScale (vpn, -1, modelorg);
	else
		VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	rotated = (e->angles[0] || e->angles[1] || e->angles[2]);
	if (e->csqc_renderflags & CSQC_RENDER_AXIS)
		R_EntityAxisToLocal(e, modelorg);
	else if (rotated)
	{
		vec3_t	temp;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

// calculate dynamic lighting for bmodel if it's not an
// instanced model
	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value && r_dynamic.value)
	if (e->model->submodelof == cl.worldmodel)	//R_MarkLights has a hacky assumption about cl.worldmodel that could crash if we imported some other model.
	{
		vec3_t mins, maxs;
		qboolean have_bounds = false;

		for (k=0 ; k<MAX_DLIGHTS ; k++)
		{
			float radius, dist2 = 0.0f;
			int axis;

			if ((cl_dlights[k].die < cl.time) ||
				(!cl_dlights[k].radius) ||
				(R_DlightStyleScale(&cl_dlights[k]) <= 0.0f))
				continue;

			if (!have_bounds)
			{
				R_GetEntityBounds (e, mins, maxs);
				have_bounds = true;
			}
			radius = cl_dlights[k].radius;
			for (axis = 0; axis < 3; axis++)
			{
				float delta = 0.0f;
				if (cl_dlights[k].origin[axis] < mins[axis])
					delta = mins[axis] - cl_dlights[k].origin[axis];
				else if (cl_dlights[k].origin[axis] > maxs[axis])
					delta = cl_dlights[k].origin[axis] - maxs[axis];
				dist2 += delta * delta;
			}
			if (dist2 > radius * radius)
				continue;

			VectorSubtract(cl_dlights[k].origin, e->origin, lightorg);
			if (e->csqc_renderflags & CSQC_RENDER_AXIS)
				R_EntityAxisToLocal(e, lightorg);
			else if (rotated)
			{
				vec3_t temp;
				VectorCopy (lightorg, temp);
				lightorg[0] = DotProduct (temp, forward);
				lightorg[1] = -DotProduct (temp, right);
				lightorg[2] = DotProduct (temp, up);
			}
			R_MarkLights (&cl_dlights[k], lightorg, r_framecount, k,
				clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	return true;
}

qboolean R_BrushModelZFix (const entity_t *e)
{
	// Alpha-tested static decals sit close to the world. Keep their original
	// placement, including in mixed-material models that share one transform.
	return gl_zfix.value && (!e->is_static || !e->model->hasfencesurfaces);
}

static void R_PushBrushModelMatrix (entity_t *e)
{
	qboolean zfix = R_BrushModelZFix (e);

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
}

// true when R_PushBrushModelMatrix would build the same matrix for both
static qboolean R_BrushModelSameMatrix (entity_t *a, entity_t *b)
{
	return VectorCompare (a->origin, b->origin) && VectorCompare (a->angles, b->angles) &&
		a->netstate.scale == ENTSCALE_DEFAULT && b->netstate.scale == ENTSCALE_DEFAULT &&
		R_BrushModelZFix (a) == R_BrushModelZFix (b);
}

static qboolean R_BeginBrushModel (entity_t *e)
{
	if (!R_SetupBrushModel (e))
		return false;
	R_PushBrushModelMatrix (e);
	return true;
}

/*
=================
R_ChainBrushModel

Chain the surfaces facing modelorg; true if any of them is not liquid.
lightmapchains also links each surface's poly into its lightmap's list, which
only the r_lightmap debug view draws.
=================
*/
static qboolean R_ChainVisibleSurfaces (qmodel_t *clmodel, qboolean lightmapchains)
{
	int			i;
	msurface_t	*psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
	mplane_t	*pplane;
	float		dot;
	qboolean	chainedsolid = false;

	for (i=0 ; i<clmodel->nummodelsurfaces ; i++, psurf++)
	{
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal);
		if (!r_refdef.csqc.projection.orthographic)
			dot -= pplane->dist;
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_ChainSurface (psurf, chain_model);
			if (lightmapchains)
				R_RenderDynamicLightmaps(clmodel, psurf);
			else
				R_UpdateDynamicLightmap(clmodel, psurf);
			rs_brushpolys++;
			if (!(psurf->flags & SURF_DRAWTURB))
				chainedsolid = true;
		}
	}
	return chainedsolid;
}

static qboolean R_ChainBrushModel (qmodel_t *clmodel)
{
	R_ClearTextureChains (clmodel, chain_model);
	return R_ChainVisibleSurfaces (clmodel, true);
}

/*
=================
R_BrushModelTextures

The ascending texture indices a model's surfaces use, or -1 if there are more
than fit. Inline models share the world's texture array (hundreds of entries),
so their draws clear and walk just these instead of every world texture.
Built once per model load.
=================
*/
static int R_BrushModelTextures (qmodel_t *m, const unsigned short **idx)
{
	int i, j, n;
	msurface_t *s;

	*idx = m->chaintextures;
	if (m->chaintextures_modgen == mod_generation + 1)
		return m->chainnumtextures;

	for (i = 0, n = 0, s = &m->surfaces[m->firstmodelsurface]; i < m->nummodelsurfaces; i++, s++)
	{
		int k = s->texinfo->materialidx;

		if (k < 0 || k >= m->numtextures || k > 0xffff || m->textures[k] != s->texinfo->texture)
		{
			n = -1;
			break;
		}
		for (j = n; j > 0 && m->chaintextures[j-1] > k; j--)
			;
		if (j > 0 && m->chaintextures[j-1] == k)
			continue;
		if (n == MAX_MODEL_CHAIN_TEXTURES)
		{
			n = -1;
			break;
		}
		memmove(&m->chaintextures[j+1], &m->chaintextures[j], (n - j) * sizeof(m->chaintextures[0]));
		m->chaintextures[j] = (unsigned short)k;
		n++;
	}
	m->chainnumtextures = n;
	m->chaintextures_modgen = mod_generation + 1;
	return n;
}

/*
Chain like R_ChainBrushModel, clearing only the chains the draw then walks
(these textures). Other chain_model chains may keep stale entries; every full
walk clears them first. Lightmap poly lists are left alone: the texture-list
path never runs in the r_lightmap debug view, the only one that draws them,
and R_MarkSurfaces resets them every frame.
*/
static qboolean R_ChainModelTextures (qmodel_t *m, const unsigned short *idx, int n)
{
	int i;

	for (i = 0; i < n; i++)
		m->textures[idx[i]]->texturechains[chain_model] = NULL;
	return R_ChainVisibleSurfaces (m, false);
}

void R_DrawBrushModel (entity_t *e)
{
	qmodel_t	*clmodel;
	const unsigned short	*textures = NULL;
	int			numtextures = -1;
	qboolean	chainedsolid;

	if (!R_BeginBrushModel (e))
		return;
	clmodel = e->model;

	if (R_DrawBModelDrawCache (clmodel, e))
	{
		glPopMatrix ();
		return;
	}
	R_FlushOpaqueBModelRun ();
	// debug views draw the lightmap poly lists, which a partial clear leaves stale
	if (!r_drawflat_cheatsafe && !r_fullbright_cheatsafe && !r_lightmap_cheatsafe)
		numtextures = R_BrushModelTextures (clmodel, &textures);
	if (numtextures >= 0)
	{
		chainedsolid = R_ChainModelTextures (clmodel, textures, numtextures);
		R_SetChainTextures (clmodel, textures, numtextures);
	}
	else
		chainedsolid = R_ChainBrushModel (clmodel);
	if (chainedsolid || !R_DrawTextureChains_LiquidOnly (e))
		R_DrawTextureChains (clmodel, e, chain_model);
	R_DrawTextureChains_Water (clmodel, e, chain_model);
	R_SetChainTextures (NULL, NULL, 0);

	glPopMatrix ();
}

/*
=================
Liquid brush runs

Maps can hold hundreds of translucent liquid brush entities (immortal.bsp has
~900 graded-alpha func_walls). Drawn one at a time, each sets up and tears down
the liquid program, buffers and attributes. In the alpha pass, consecutive
entities whose every surface is plain liquid are queued and drawn as a run in
the same order, each with its own transform, alpha and lightmap updates; any
other entity flushes the queue first, so the back-to-front order is kept.

Translucent entities whose surfaces are all plain lightmapped textures (Peril's
start.bsp has ~570 see-through func_walls) queue the same way as solid runs; a
queue holds one kind at a time.
=================
*/
#define MAX_LIQUID_RUN 1024
enum { RUN_LIQUID, RUN_SOLID };
static entity_t *r_liquid_queue[MAX_LIQUID_RUN];
static int r_num_liquid_queue;
static int r_liquid_kind;
static qboolean r_liquid_collecting;

static qboolean R_LiquidOnlyModel (qmodel_t *m)
{
	int i;
	msurface_t *s = &m->surfaces[m->firstmodelsurface];

	if (m->nummodelsurfaces <= 0)
		return false;
	for (i = 0; i < m->nummodelsurfaces; i++, s++)
		if (!(s->flags & SURF_DRAWTURB) || (s->flags & (SURF_DRAWTELE | SURF_DRAWSKY)))
			return false;
	return true;
}

void R_BeginLiquidBrushes (void)
{
	r_num_liquid_queue = 0;
	r_liquid_collecting = true;
}

void R_FlushLiquidBrushes (void)
{
	int i, n = r_num_liquid_queue;
	qboolean started = false;

	r_num_liquid_queue = 0;
	if (n == 1)
	{
		R_DrawBrushModel (r_liquid_queue[0]);
		return;
	}
	// a late model rebuilds every lightmap and the brush vertex buffer, which a
	// run binds once up front; do it before any run starts
	if (lightmaps_latecached)
		R_UploadLightmaps ();
	if (r_liquid_kind == RUN_SOLID)
	{	// consecutive entities with the same transform share one pushed matrix,
		// so the run can carry a batch from one to the next
		entity_t *pushed = NULL;

		for (i = 0; i < n; i++)
		{
			entity_t *e = r_liquid_queue[i];
			const unsigned short *textures;
			int numtextures;

			if (!R_SetupBrushModel (e))
				continue;
			if (!pushed || !R_BrushModelSameMatrix (pushed, e))
			{
				if (pushed)
				{
					R_SolidRunFlush ();
					glPopMatrix ();
				}
				R_PushBrushModelMatrix (e);
				pushed = e;
			}
			numtextures = R_BrushModelTextures (e->model, &textures);
			if (numtextures >= 0)
				R_ChainModelTextures (e->model, textures, numtextures);
			else
				R_ChainBrushModel (e->model);
			if (!started)
			{
				R_SolidRunBegin (ENTALPHA_DECODE(e->alpha));
				started = true;
			}
			if (numtextures >= 0)
				R_SetChainTextures (e->model, textures, numtextures);
			R_SolidRunDrawModel (e->model, e, chain_model);
			R_SetChainTextures (NULL, NULL, 0);
		}
		if (pushed)
		{
			R_SolidRunFlush ();
			glPopMatrix ();
		}
		if (started)
			R_SolidRunEnd ();
		return;
	}
	for (i = 0; i < n; i++)
	{
		entity_t *e = r_liquid_queue[i];
		const unsigned short *textures;
		int numtextures;

		if (!R_BeginBrushModel (e))
			continue;
		numtextures = R_BrushModelTextures (e->model, &textures);
		if (numtextures >= 0)
			R_ChainModelTextures (e->model, textures, numtextures);
		else
			R_ChainBrushModel (e->model);
		if (!started)
		{
			R_LiquidRunBegin (ENTALPHA_DECODE(e->alpha));
			started = true;
		}
		GL_SelectTexture (GL_TEXTURE0);
		R_UploadLightmaps ();
		R_LiquidRunDrawModel (e->model, e, chain_model, numtextures >= 0 ? textures : NULL, numtextures);
		glPopMatrix ();
	}
	if (started)
		R_LiquidRunEnd ();
}

qboolean R_QueueLiquidBrush (entity_t *e)
{
	int kind;

	if (!r_liquid_collecting || e->model->type != mod_brush || e->effects ||
		ENTALPHA_DECODE(e->alpha) >= 1.0f)
		return false;
	if (R_LiquidRunAvailable () && R_LiquidOnlyModel (e->model))
		kind = RUN_LIQUID;
	else if (R_SolidRunModel (e))
		kind = RUN_SOLID;
	else
		return false;
	if (r_num_liquid_queue && (r_liquid_kind != kind || r_num_liquid_queue == MAX_LIQUID_RUN))
		R_FlushLiquidBrushes ();
	r_liquid_kind = kind;
	r_liquid_queue[r_num_liquid_queue++] = e;
	return true;
}

void R_EndLiquidBrushes (void)
{
	R_FlushLiquidBrushes ();
	r_liquid_collecting = false;
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

	if (r_refdef.csqc.projection.orthographic)
		VectorScale (vpn, -1, modelorg);
	else
		VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (e->csqc_renderflags & CSQC_RENDER_AXIS)
		R_EntityAxisToLocal(e, modelorg);
	else if (e->angles[0] || e->angles[1] || e->angles[2])
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
		dot = DotProduct (modelorg, pplane->normal);
		if (!r_refdef.csqc.projection.orthographic)
			dot -= pplane->dist;
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
// in r_world.c). Sets the dirty-tile bits the surface's texel rect covers
// (woods #lmtiles).
// Sets lm->modified and grows lm->rectchange too, all under lm_dirty_mutex so the
// scenecache worker's marks can't be lost to R_UploadLightmap's concurrent
// snapshot-and-clear. Call this AFTER R_BuildLightMap has written the staging
// bytes: a rect must never be visible to the uploader before its texels are
// complete, or the upload consumes the mark and the finished bytes never reach
// the GPU (permanent stale glow when that was a dying dlight's clearing rebuild).
void R_LightmapMarkDirtyRect (struct lightmap_s *lm, int light_s, int light_t, int smax, int tmax)
{
	int l = light_s, t = light_t, r = light_s + smax, b = light_t + tmax;
	int i;
	glRect_t *theRect;

	if (l < 0) l = 0;
	if (t < 0) t = 0;
	if (r > LMBLOCK_WIDTH) r = LMBLOCK_WIDTH;
	if (b > LMBLOCK_HEIGHT) b = LMBLOCK_HEIGHT;
	if (r <= l || b <= t)
		return;

	SDL_LockMutex (lm_dirty_mutex);

	lm->modified = true;
	SDL_AddAtomicInt (&lm_dirty_serial, 1);	// after the flag: an uploader that sees the new serial sees the flag
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

	// woods #lmtiles -- set the covered tile bits; bands are LM_TILE_H rows, columns lm_tilew texels
	{
		int c0 = l / lm_tilew, c1 = (r - 1) / lm_tilew;
		int r0 = t / LM_TILE_H, r1 = (b - 1) / LM_TILE_H;
		unsigned int mask = (c1 - c0 >= 31) ? 0xffffffffu : (((1u << (c1 - c0 + 1)) - 1u) << c0);

		for (i = r0; i <= r1; i++)
			lm->dirtytiles[i] |= mask;
		if (r0 < lm->dirtylo) lm->dirtylo = r0;
		if (r1 > lm->dirtyhi) lm->dirtyhi = r1;
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
	if (fa->flags & SURF_DRAWTILED) //johnfitz -- not a lightmapped surface
		return;

	// add to lightmap chain
	fa->polys->chain = lightmaps[fa->lightmaptexturenum].polys;
	lightmaps[fa->lightmaptexturenum].polys = fa->polys;

	R_UpdateDynamicLightmap (model, fa);
}

/*
================
R_UpdateDynamicLightmap

R_RenderDynamicLightmaps without linking the surface into its lightmap's poly
list: rebuilds the lightmap if its styles or dynamic lights changed.
================
*/
void R_UpdateDynamicLightmap (qmodel_t *model, msurface_t *fa)
{
	byte		*base;
	int			maps;
	int smax, tmax;

	if (fa->flags & SURF_DRAWTILED) //johnfitz -- not a lightmapped surface
		return;

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
			SDL_AddAtomicInt (&lm_dirty_serial, 1);
			lightmaps[texnum].rectchange.l = 0;
			lightmaps[texnum].rectchange.t = 0;
			lightmaps[texnum].rectchange.h = LMBLOCK_HEIGHT;
			lightmaps[texnum].rectchange.w = LMBLOCK_WIDTH;
			LM_MarkAllDirty (&lightmaps[texnum]); // woods #lmtiles -- whole texture dirty
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

	numthreads = q_max (1, SDL_GetNumLogicalCPUCores ());
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
	glpoly_t *poly = fa->polys;
	/* Rebuilding lighting does not change geometry. Keep both the base polygon
	 * and any legacy water subdivisions rather than appending hunk allocations. */
	if (!poly)
	{
		poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (fa->numedges-4) * VERTEXSIZE*sizeof(float));
		fa->polys = poly;
	}
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
	if ((fa->flags & SURF_DRAWTURB) && !gl_glsl_water_able && !poly->next)
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
		/* Rebuild static lighting first. Old marks must not collide with the
		 * next rendered frame or contribute through a stale currententity. */
		surf->dlightframe = -1;
		memset (surf->dlightbits, 0, sizeof(surf->dlightbits));
		surf->cached_dlight = false;
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

	lightmap_settings_pending = false;
	RSceneCache_Shutdown();	//make sure there's nothing poking them off-thread.
	lightmap_rebuilds++; // woods #dlightskip

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
		free(lightmaps[i].dirtytiles); // woods #lmtiles
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

	// Size lightmap atlas dimensions to fit large maps in fewer atlases without
	// allocating oversized 16384px-tall backing buffers, while keeping small maps
	// at 512px wide and guaranteeing every surface fits.
	if (gl_hardware_maxsize > 0)
	{
		size_t total_luxels = 0;
		const int default_width = q_min(gl_hardware_maxsize, LMBLOCK_DEFAULT_WIDTH);
		const int max_width = q_min(gl_hardware_maxsize, LMBLOCK_MAX_WIDTH);
		const int max_height = q_min(gl_hardware_maxsize, LMBLOCK_DEFAULT_HEIGHT);
		int max_surf_w = 0, max_surf_h = 0;
		int k;

		for (j = 1; j < MAX_MODELS; j++)
		{
			m = cl.model_precache[j];
			if (!m || m->type != mod_brush || m->needload || m->name[0] == '*')
				continue;
			for (k = 0; k < m->numsurfaces; k++)
			{
				const msurface_t *surf = &m->surfaces[k];
				int sw, sh;
				if (surf->flags & SURF_DRAWTILED)
					continue;
				sw = surf->extents[0] + 1;
				sh = surf->extents[1] + 1;
				if (sw > max_surf_w)
					max_surf_w = sw;
				if (sh > max_surf_h)
					max_surf_h = sh;
				total_luxels += (size_t)sw * (size_t)((sh + 3) & ~3);
			}
		}
		for (j = 1; j < MAX_MODELS; j++)
		{
			m = cl.model_precache_csqc[j];
			if (!m)
				break;
			if (m->type != mod_brush || m->needload || m->name[0] == '*')
				continue;
			for (k = 0; k < m->numsurfaces; k++)
			{
				const msurface_t *surf = &m->surfaces[k];
				int sw, sh;
				if (surf->flags & SURF_DRAWTILED)
					continue;
				sw = surf->extents[0] + 1;
				sh = surf->extents[1] + 1;
				if (sw > max_surf_w)
					max_surf_w = sw;
				if (sh > max_surf_h)
					max_surf_h = sh;
				total_luxels += (size_t)sw * (size_t)((sh + 3) & ~3);
			}
		}

		LMBLOCK_WIDTH = default_width;
		LMBLOCK_HEIGHT = max_height;
		while (LMBLOCK_WIDTH < gl_hardware_maxsize && LMBLOCK_WIDTH < max_surf_w)
			LMBLOCK_WIDTH <<= 1;
		if (LMBLOCK_WIDTH > gl_hardware_maxsize)
			LMBLOCK_WIDTH = gl_hardware_maxsize;
		while (LMBLOCK_WIDTH < max_width &&
			(double)total_luxels > 0.88 * (double)LMBLOCK_WIDTH * (double)max_height)
		{
			LMBLOCK_WIDTH <<= 1;
		}
		if (LMBLOCK_WIDTH > default_width)
		{
			// Fit total_luxels with ~14% packing headroom so we normally land in one
			// atlas without allocating a full 16384px-tall CPU+GPU buffer.
			size_t target_luxels = total_luxels + (total_luxels / 7u) + (size_t)LMBLOCK_WIDTH;
			size_t need_h = (target_luxels + (size_t)LMBLOCK_WIDTH - 1u) / (size_t)LMBLOCK_WIDTH;
			int h;

			if (need_h < (size_t)max_surf_h)
				need_h = (size_t)max_surf_h;
			if (need_h < 512u)
				need_h = 512u;

			if (need_h >= (size_t)max_height)
				h = max_height;
			else if (gl_texture_NPOT)
				h = (int)((need_h + 511u) & ~(size_t)511u);
			else
			{
				h = 512;
				while ((size_t)h < need_h && h < max_height)
					h <<= 1;
			}
			if (h > max_height)
				h = max_height;
			if (h < max_surf_h)
				h = q_min(max_height, max_surf_h);
			LMBLOCK_HEIGHT = h;
		}
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
		LM_ClearDirty (lm); // woods #lmtiles

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
		Con_DPrintf ("GL_BuildLightmaps: build %.1fms upload %.1fms (%d lightmaps, frame %d)\n",
			(t_built-t_start)*1000.0, (t_uploaded-t_built)*1000.0, lightmap_count, r_framecount);
	}
}

/*
=============================================================

	VBO support

=============================================================
*/

GLuint gl_bmodel_vbo = 0;
GLuint gl_bmodel_lmbounds_vbo = 0;
GLuint gl_bmodel_instance_vbo = 0;
unsigned int gl_bmodel_vbo_generation = 0;	//tb -- bumped whenever vbo_firstvert offsets change, to invalidate per-model draw caches

void GL_DeleteBModelVertexBuffer (void)
{
	R_BModelDrawCache_CleanupAll ();
	//tb -- above the capability check: the index buffers reference gl_bmodel_vbo,
	//and the CPU-side nodes must be released even when GL support is absent.
	GL_BrushShadowCache_Clear ();

	if (lm_scratchpbo)
	{
		if (GL_DeleteBuffersFunc)
			GL_DeleteBuffersFunc (1, &lm_scratchpbo);
		lm_scratchpbo = 0;
	}

	if (!(gl_vbo_able && gl_mtexable && gl_max_texture_units >= 3))
		return;

	GL_DeleteBuffersFunc (1, &gl_bmodel_vbo);
	if (gl_bmodel_lmbounds_vbo)
		GL_DeleteBuffersFunc (1, &gl_bmodel_lmbounds_vbo);
	if (gl_bmodel_instance_vbo)
		GL_DeleteBuffersFunc (1, &gl_bmodel_instance_vbo);
	gl_bmodel_vbo = 0;
	gl_bmodel_lmbounds_vbo = 0;
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
	size_t		numverts, varray_bytes, lmbounds_bytes, varray_index;
	int		i, j;
	qmodel_t	*m;
	float		*varray;
	unsigned short	*lmbounds;
	double		t_start;
	qboolean	profile;

	if (!(gl_vbo_able && gl_mtexable && gl_max_texture_units >= 3))
		return;

	profile = developer.value != 0;
	t_start = profile ? Sys_DoubleTime () : 0;

// ask GL for a name for our VBO
	//tb -- normal map setup comes through here rather than through
	//GL_DeleteBModelVertexBuffer, so the shadow index cache must be released
	//here too. Generation checking stops stale indices being *drawn*, but the
	//old EBOs and nodes would otherwise accumulate across every map change.
	GL_BrushShadowCache_Clear ();

	GL_DeleteBuffersFunc (1, &gl_bmodel_vbo);
	GL_DeleteBuffersFunc (1, &gl_bmodel_lmbounds_vbo);
	gl_bmodel_vbo = GL_GenBufferName ();
	gl_bmodel_lmbounds_vbo = GL_GenBufferName ();

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
	if (numverts > SIZE_MAX / (4 * sizeof(unsigned short)))
		Sys_Error ("GL_BuildBModelVertexBuffer: lightmap bounds buffer too large");
	varray_bytes = VERTEXSIZE * sizeof(float) * numverts;
	lmbounds_bytes = 4 * sizeof(unsigned short) * numverts;
	varray = (float *) malloc (varray_bytes);
	if (!varray && varray_bytes)
		Sys_Error ("GL_BuildBModelVertexBuffer: out of memory (%" SDL_PRIu64 " bytes)", (uint64_t)varray_bytes);
	lmbounds = (unsigned short *) malloc (lmbounds_bytes);
	if (!lmbounds && lmbounds_bytes)
		Sys_Error ("GL_BuildBModelVertexBuffer: out of memory (%" SDL_PRIu64 " bytes)", (uint64_t)lmbounds_bytes);
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
			size_t k;
			const unsigned short bounds[4] = {
				(unsigned short)(s->lightmaptexturenum >= 0 ? s->light_s : 0),
				(unsigned short)(s->lightmaptexturenum >= 0 ? s->light_t : 0),
				(unsigned short)(s->lightmaptexturenum >= 0 ? s->light_s + s->extents[0] : 0),
				(unsigned short)(s->lightmaptexturenum >= 0 ? s->light_t + s->extents[1] : 0)
			};
			s->vbo_firstvert = (int)varray_index;
			memcpy (&varray[VERTEXSIZE * varray_index], s->polys->verts, VERTEXSIZE * sizeof(float) * nv);
			for (k = 0; k < nv; k++)
				memcpy (&lmbounds[4 * (varray_index + k)], bounds, sizeof(bounds));
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
			size_t k;
			const unsigned short bounds[4] = {
				(unsigned short)(s->lightmaptexturenum >= 0 ? s->light_s : 0),
				(unsigned short)(s->lightmaptexturenum >= 0 ? s->light_t : 0),
				(unsigned short)(s->lightmaptexturenum >= 0 ? s->light_s + s->extents[0] : 0),
				(unsigned short)(s->lightmaptexturenum >= 0 ? s->light_t + s->extents[1] : 0)
			};
			s->vbo_firstvert = (int)varray_index;
			memcpy (&varray[VERTEXSIZE * varray_index], s->polys->verts, VERTEXSIZE * sizeof(float) * nv);
			for (k = 0; k < nv; k++)
				memcpy (&lmbounds[4 * (varray_index + k)], bounds, sizeof(bounds));
			varray_index += nv;
		}
	}

// upload to GPU
	GL_BindBufferFunc (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BufferDataFunc (GL_ARRAY_BUFFER, (GLsizeiptr)varray_bytes, varray, GL_STATIC_DRAW);
	GL_BindBufferFunc (GL_ARRAY_BUFFER, gl_bmodel_lmbounds_vbo);
	GL_BufferDataFunc (GL_ARRAY_BUFFER, (GLsizeiptr)lmbounds_bytes, lmbounds, GL_STATIC_DRAW);
	free (varray);
	free (lmbounds);

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
	int			lnum, word;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t, s0, s1, t0, t1;
	int			i;
	int			smax, tmax;
	//johnfitz -- lit support via lordhavoc
	float		cred, cgreen, cblue, brightness, stylescale;
	unsigned	*bl;
	//johnfitz
	vec3_t		lightofs;	//Spike: light surfaces based upon where they are now instead of their default position.

	smax = surf->extents[0]+1;
	tmax = surf->extents[1]+1;

	for (word = 0; word < (int)countof(surf->dlightbits); word++)
	{
		unsigned int bits = surf->dlightbits[word];
		while (bits)
		{
			unsigned int lsb = bits & (0u - bits);
			int bit = 0;
			unsigned int tmp = lsb;
			while (tmp > 1u) { tmp >>= 1; bit++; }
			bits ^= lsb;
			lnum = (word << 5) + bit;
			if (lnum >= MAX_DLIGHTS)
				break;

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
			rad -= fabsf(dist);
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

			// Clamp luxel loop to the region where |td| < minlight and |sd| < minlight.
			// td/sd truncate toward zero before comparing with minlight, so pad by 1 in
			// world-distance space before dividing by lmvecscale.
			if (surf->lmvecscale[1] > 0.0f)
			{
				float tspan = (minlight + 1.0f) / surf->lmvecscale[1];
				float lo = floorf(local[1] - tspan);
				float hi = ceilf(local[1] + tspan) + 1.0f;
				t0 = (lo <= 0.0f) ? 0 : (lo >= (float)tmax ? tmax : (int)lo);
				t1 = (hi <= 0.0f) ? 0 : (hi >= (float)tmax ? tmax : (int)hi);
			}
			else
			{
				t0 = 0;
				t1 = tmax;
			}
			if (surf->lmvecscale[0] > 0.0f)
			{
				float sspan = (minlight + 1.0f) / surf->lmvecscale[0];
				float lo = floorf(local[0] - sspan);
				float hi = ceilf(local[0] + sspan) + 1.0f;
				s0 = (lo <= 0.0f) ? 0 : (lo >= (float)smax ? smax : (int)lo);
				s1 = (hi <= 0.0f) ? 0 : (hi >= (float)smax ? smax : (int)hi);
			}
			else
			{
				s0 = 0;
				s1 = smax;
			}
			if (t0 >= t1 || s0 >= s1)
				continue;

			//johnfitz -- lit support via lordhavoc
			cred = lights[lnum].color[0] * 256.0f * stylescale;
			cgreen = lights[lnum].color[1] * 256.0f * stylescale;
			cblue = lights[lnum].color[2] * 256.0f * stylescale;
			//johnfitz
			for (t = t0 ; t < t1 ; t++)
			{
				td = (int)((local[1] - t)*surf->lmvecscale[1]);
				if (td < 0)
					td = -td;
				if (td >= minlight)
					continue;
				bl = blocklights + (t * smax + s0) * 3;
				for (s = s0 ; s < s1 ; s++)
				{
					sd = (int)((local[0] - s)*surf->lmvecscale[0]);
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
	state->dynamic = r_dynamic.value != 0;
	state->ambient_light = 0;
	if (!(cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cls.demoplayback))
		state->ambient_light = ((unsigned)CLAMP(0.0f, r_ambient.value, 255.0f)) << 8;
	memcpy(state->lightstyles, d_lightstylevalue, sizeof(state->lightstyles));
}

static void R_BuildLightMapImpl (qmodel_t *model, msurface_t *surf, byte *dest, int stride, entity_t *currentent, int framecount, dlight_t *lights,
	int format, int overbright, qboolean dynamic, unsigned ambient_light, const int *lightstyles);

void R_BuildLightMapForState (qmodel_t *model, msurface_t *surf, byte *dest, int stride, entity_t *currentent, int framecount, dlight_t *lights, const r_lightmap_buildstate_t *state)
{
	R_BuildLightMapImpl(model, surf, dest, stride, currentent, framecount, lights,
		state->format, state->overbright, state->dynamic, state->ambient_light, state->lightstyles);
}

static void R_BuildLightMapImpl (qmodel_t *model, msurface_t *surf, byte *dest, int stride, entity_t *currentent, int framecount, dlight_t *lights,
	int format, int overbright, qboolean dynamic, unsigned ambient_light, const int *lightstyles)
{
	const qboolean add_dlights = dynamic && surf->dlightframe == framecount;

	int			smax, tmax;
	unsigned		r, g, b;
	int			i, j, size;
	unsigned	scale;
	int			maps;
	unsigned	*bl;
	unsigned	*blocklights;	//moved this to stack, so workers working on the worldmodel won't fight the main thread processing the submodels.

	surf->cached_dlight = add_dlights;

	smax = surf->extents[0]+1;
	tmax = surf->extents[1]+1;
	size = smax*tmax;

	blocklights = alloca(size*3*sizeof(*blocklights));	//alloca is unsafe, but at least we memset it... should probably memset in stack order in the hopes of getting a standard stack-overflow-segfault instead of poking completely outside what the system thinks is the stack in the case of massive surfs...

	if (model->lightdata)
	{
		qboolean initialized = false;

		if (ambient_light) // woods #rambient
		{
			bl = blocklights;
			for (i = 0; i < size; ++i) {
				*bl++ = ambient_light;
				*bl++ = ambient_light;
				*bl++ = ambient_light;
			}
			initialized = true;
		}

	// add all the lightmaps
		if (!surf->samples)
		{
			if (!initialized)
				memset (&blocklights[0], 0, size * 3 * sizeof (unsigned int));
		}
		else if (model->flags & MOD_HDRLIGHTING)
		{
			uint32_t	*lightmap = surf->samples;
			if (!initialized)
				memset (&blocklights[0], 0, size * 3 * sizeof (unsigned int));
			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != INVALID_LIGHTSTYLE ;
				 maps++)
			{
				scale = lightstyles[surf->styles[maps]];
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
				scale = lightstyles[surf->styles[maps]];
				surf->cached_light[maps] = scale;	// 8.8 fraction
				//johnfitz -- lit support via lordhavoc
				bl = blocklights;
				if (!initialized)
				{
					for (i=0 ; i<size ; i++)
					{
						*bl++ = *lightmap++ * scale;
						*bl++ = *lightmap++ * scale;
						*bl++ = *lightmap++ * scale;
					}
					initialized = true;
				}
				else
				{
					for (i=0 ; i<size ; i++)
					{
						*bl++ += *lightmap++ * scale;
						*bl++ += *lightmap++ * scale;
						*bl++ += *lightmap++ * scale;
					}
				}
				//johnfitz
			}
			if (!initialized)
				memset (&blocklights[0], 0, size * 3 * sizeof (unsigned int));
		}

	// add all the dynamic lights
		if (add_dlights)
			R_AddDynamicLights (surf, blocklights, currentent, lights, lightstyles);
	}
	else
	{
	// set to full bright if no light data
		for (i=0 ; i<size * 3; i++) // woods -- fix lightmap initialization for full bright surfaces
			blocklights[i] = 0xffff;	//don't use memset, it oversaturates FAR too much with hdr...
	}

// bound, invert, and shift
// store:
	switch (format)
	{
	case GL_RGB9_E5:
		{
			// 2^(24 - be) / identity, where be = e + 15 in [0..30]
			static const float pow2_24_minus_be[31] = {
				(float)(1u<<24), (float)(1u<<23), (float)(1u<<22), (float)(1u<<21),
				(float)(1u<<20), (float)(1u<<19), (float)(1u<<18), (float)(1u<<17),
				(float)(1u<<16), (float)(1u<<15), (float)(1u<<14), (float)(1u<<13),
				(float)(1u<<12), (float)(1u<<11), (float)(1u<<10), (float)(1u<<9),
				(float)(1u<<8),  (float)(1u<<7),  (float)(1u<<6),  (float)(1u<<5),
				(float)(1u<<4),  (float)(1u<<3),  (float)(1u<<2),  (float)(1u<<1),
				1.0f,            1.0f/2.0f,       1.0f/4.0f,       1.0f/8.0f,
				1.0f/16.0f,      1.0f/32.0f,      1.0f/64.0f
			};
			const float inv_identity = 1.0f / (float)(1u << ((overbright ? 8 : 7) + 8));
			stride -= smax * 4;
			bl = blocklights;
			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
				{
					unsigned int maxbl = q_max(q_max(bl[0], bl[1]), bl[2]);
					if (!maxbl)
					{
						*(unsigned int *)dest = 0;
					}
					else
					{
						union { float f; uint32_t u; } conv;
						int be;
						float inv_scale;
						unsigned int mr, mg, mb;

						conv.f = (float)maxbl * inv_identity;
						if (!overbright && conv.f > 1.0f)
							conv.f = 1.0f;
						be = ((int)(((conv.u - 1u) >> 23) & 0xff) - 127) + 16;
						if (be < 0) be = 0;
						else if (be > 30) be = 30;

						inv_scale = pow2_24_minus_be[be] * inv_identity;
						mr = (unsigned int)(bl[0] * inv_scale + 0.5f);
						mg = (unsigned int)(bl[1] * inv_scale + 0.5f);
						mb = (unsigned int)(bl[2] * inv_scale + 0.5f);
						if (mr > 0x1ffu) mr = 0x1ffu;
						if (mg > 0x1ffu) mg = 0x1ffu;
						if (mb > 0x1ffu) mb = 0x1ffu;
						*(unsigned int *)dest = ((unsigned int)be << 27) | mr | (mg << 9) | (mb << 18);
					}
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

				*(unsigned int*)dest = (3u<<30) | (b<<20) | (g<<10) | r;
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
	unsigned ambient_light = 0;
	if (!(cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cls.demoplayback))
		ambient_light = ((unsigned)CLAMP(0.0f, r_ambient.value, 255.0f)) << 8;

	R_BuildLightMapImpl(model, surf, dest, stride, currentent, framecount, lights,
		gl_lightmap_format, !!gl_overbright.value, r_dynamic.value != 0, ambient_light, d_lightstylevalue);
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
				GL_UNSIGNED_INT_2_10_10_10_REV, src);
	else
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, t, w, h, gl_lightmap_format,
				GL_UNSIGNED_BYTE, src);
}

static size_t *lm_upload_offsets; // woods #lmtiles -- main thread only, grown with lm_upload_rects
static glRect_t *lm_upload_rects;
static unsigned int *lm_upload_tiles;
static int lm_upload_maxrects, lm_upload_maxtiles;

static qboolean R_UploadLightmapScratch (const struct lightmap_s *lm, const glRect_t *rects, int count)
{
	size_t *offsets = lm_upload_offsets, bytes = 0;
	const size_t stride = (size_t)LMBLOCK_WIDTH * lightmap_bytes;
	byte *dst;
	int i;

	if (!gl_vbo_able || !GL_GenBuffersFunc || !GL_BufferDataFunc || !GL_MapBufferFunc || !GL_UnmapBufferFunc)
		return false;
	if (!lm_scratchpbo)
		GL_GenBuffersFunc(1, &lm_scratchpbo);
	if (!lm_scratchpbo)
		return false;

	for (i = 0; i < count; i++)
	{
		offsets[i] = bytes;
		bytes += (size_t)rects[i].w * rects[i].h * lightmap_bytes;
	}
	GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lm_scratchpbo);
	GL_BufferDataFunc(GL_PIXEL_UNPACK_BUFFER_ARB, bytes, NULL, GL_STREAM_DRAW);
	dst = GL_MapBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY);
	if (dst)
	{
		for (i = 0; i < count; i++)
		{
			const glRect_t *rect = &rects[i];
			const size_t rowbytes = (size_t)rect->w * lightmap_bytes;
			const byte *src = lm->pbodata + rect->t * stride + rect->l * lightmap_bytes;
			int row;

			if (rect->l == 0 && rect->w == LMBLOCK_WIDTH)
				memcpy(dst + offsets[i], src, (size_t)rect->h * stride);
			else
				for (row = 0; row < rect->h; row++, src += stride)
					memcpy(dst + offsets[i] + row * rowbytes, src, rowbytes);
		}
		if (GL_UnmapBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB))
		{
			for (i = 0; i < count; i++)
				R_TexSubImageLightmap(rects[i].l, rects[i].t, rects[i].w, rects[i].h,
					(const GLvoid *)(uintptr_t)offsets[i]);
			GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			return true;
		}
	}
	GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
	return false;
}

// woods #lmtiles -- grow the main-thread upload scratch arrays
static void LM_ReserveUpload (int rects, int tiles)
{
	if (rects > lm_upload_maxrects)
	{
		int n = q_max (rects, lm_upload_maxrects * 2);
		glRect_t *r = (glRect_t *) realloc (lm_upload_rects, n * sizeof(*r));
		size_t *o = (size_t *) realloc (lm_upload_offsets, n * sizeof(*o));
		if (r) lm_upload_rects = r;
		if (o) lm_upload_offsets = o;
		if (!r || !o)
			Sys_Error ("R_UploadLightmap: out of memory (%d rects)", n);
		lm_upload_maxrects = n;
	}
	if (tiles > lm_upload_maxtiles)
	{
		unsigned int *t = (unsigned int *) realloc (lm_upload_tiles, tiles * sizeof(*t));
		if (!t)
			Sys_Error ("R_UploadLightmap: out of memory (%d tile rows)", tiles);
		lm_upload_tiles = t;
		lm_upload_maxtiles = tiles;
	}
}

static void R_UploadLightmap(int lmap)
{
	struct lightmap_s *lm = &lightmaps[lmap];
	glRect_t *rects;
	unsigned int *tiles;
	int i, lo, hi, valid;

	if (!lm->modified)
		return;

	// woods #lmrect -- snapshot-and-clear under the same lock
	// R_LightmapMarkDirtyRect sets bits under it, so a mark from the scenecache
	// worker either lands before the snapshot (uploaded now, its staging bytes
	// are complete because marks happen after R_BuildLightMap) or after the
	// clear (stays set for the next upload). Without this, clearing mid-job
	// dropped the worker's marks and left dead dlights baked into the GPU texture.
	SDL_LockMutex(lm_dirty_mutex);
	lm->modified = false;
	lo = lm->dirtylo;
	hi = lm->dirtyhi;
	if (lo <= hi)
	{
		LM_ReserveUpload (0, hi - lo + 1);
		memcpy (lm_upload_tiles, lm->dirtytiles + lo, (hi - lo + 1) * sizeof(*lm_upload_tiles));
	}
	LM_ClearDirty (lm);
	lm->rectchange.l = LMBLOCK_WIDTH;
	lm->rectchange.t = LMBLOCK_HEIGHT;
	lm->rectchange.h = 0;
	lm->rectchange.w = 0;
	SDL_UnlockMutex(lm_dirty_mutex);

	if (lo > hi)
		return;

	// woods #lmtiles -- coalesce dirty tile bands into a small number of rects
	// to minimize glTexSubImage2D overhead on drivers with high per-call cost.
	tiles = lm_upload_tiles;
	valid = 0;
	{
		qboolean open = false;
		int r0 = 0, r1 = 0, sc0 = 0, sc1 = 0;
		size_t dirty_texels = 0;

		for (i = lo; i <= hi; i++)
		{
			unsigned int m = tiles[i - lo];
			int c0, c1, rw, rh;

			if (!m)
				continue;
			c0 = 0;
			while (!(m & (1u << c0)))
				c0++;
			c1 = 31;
			while (!(m & (1u << c1)))
				c1--;
			rw = q_min ((c1 + 1) * lm_tilew, LMBLOCK_WIDTH) - c0 * lm_tilew;
			rh = q_min ((i + 1) * LM_TILE_H, LMBLOCK_HEIGHT) - i * LM_TILE_H;
			if (rw <= 0 || rh <= 0)
				continue;

			if (open)
			{
				int nc0 = q_min (sc0, c0);
				int nc1 = q_max (sc1, c1);
				int mw = q_min ((nc1 + 1) * lm_tilew, LMBLOCK_WIDTH) - nc0 * lm_tilew;
				int mh = q_min ((i + 1) * LM_TILE_H, LMBLOCK_HEIGHT) - r0 * LM_TILE_H;
				size_t merged_area = (size_t)mw * (size_t)mh;
				size_t new_dirty = dirty_texels + (size_t)rw * (size_t)rh;
				size_t waste = merged_area > new_dirty ? merged_area - new_dirty : 0;

				if (waste <= 32768 || waste * 2 <= merged_area)
				{
					sc0 = nc0;
					sc1 = nc1;
					r1 = i;
					dirty_texels = new_dirty;
					continue;
				}

				LM_ReserveUpload (valid + 1, 0);
				lm_upload_rects[valid].l = sc0 * lm_tilew;
				lm_upload_rects[valid].t = r0 * LM_TILE_H;
				lm_upload_rects[valid].w = q_min ((sc1 + 1) * lm_tilew, LMBLOCK_WIDTH) - lm_upload_rects[valid].l;
				lm_upload_rects[valid].h = q_min ((r1 + 1) * LM_TILE_H, LMBLOCK_HEIGHT) - lm_upload_rects[valid].t;
				valid++;
			}

			open = true;
			r0 = r1 = i;
			sc0 = c0;
			sc1 = c1;
			dirty_texels = (size_t)rw * (size_t)rh;
		}
		if (open)
		{
			LM_ReserveUpload (valid + 1, 0);
			lm_upload_rects[valid].l = sc0 * lm_tilew;
			lm_upload_rects[valid].t = r0 * LM_TILE_H;
			lm_upload_rects[valid].w = q_min ((sc1 + 1) * lm_tilew, LMBLOCK_WIDTH) - lm_upload_rects[valid].l;
			lm_upload_rects[valid].h = q_min ((r1 + 1) * LM_TILE_H, LMBLOCK_HEIGHT) - lm_upload_rects[valid].t;
			valid++;
		}
	}
	rects = lm_upload_rects;

	if (!valid)
		return;
	if (!lm->pbohandle && R_UploadLightmapScratch(lm, rects, valid))
	{
		rs_dynamiclightmaps++;
		return;
	}

	// The persistent PBO and client-memory fallback both use the atlas stride.
	if (lm->pbohandle)
		GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lm->pbohandle);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, LMBLOCK_WIDTH);
	for (i = 0; i < valid; i++)
	{
		const glRect_t *rect = &rects[i];
		size_t offset = ((size_t)rect->t * LMBLOCK_WIDTH + rect->l) * lightmap_bytes;
		const GLvoid *src = lm->pbohandle ? (const GLvoid *)(uintptr_t)offset : lm->pbodata + offset;

		R_TexSubImageLightmap(rect->l, rect->t, rect->w, rect->h, src);
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	if (lm->pbohandle)
		GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

	rs_dynamiclightmaps++;
}

// true if R_UploadLightmaps would change a lightmap texture now
qboolean R_LightmapUploadPending (void)
{
	if (lightmaps_latecached)
		return true;
	return !lightmaps_skipupdates && SDL_GetAtomicInt (&lm_dirty_serial) != lm_uploaded_serial;
}

void R_UploadLightmaps (void)
{
	int lmap, serial;

	if (lightmaps_latecached)
	{
		GL_BuildLightmaps ();
		GL_BuildBModelVertexBuffer ();
		lightmaps_latecached=false;
	}

	if (lightmaps_skipupdates)
		return;

	// read before the scan: a lightmap flagged during it bumps the serial again
	serial = SDL_GetAtomicInt (&lm_dirty_serial);
	if (serial == lm_uploaded_serial)
		return;
	lm_uploaded_serial = serial;

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
			SDL_LockMutex (lm_dirty_mutex);
			LM_ClearDirty (&lightmaps[lmap]); // woods #lmtiles
			SDL_UnlockMutex (lm_dirty_mutex);
		}
		else
		{
			GL_Bind (lightmaps[lmap].texture);
			R_UploadLightmap(lmap);
		}
	}
}

void R_ClearCachedDynamicLights (void)
{
	qmodel_t **precaches[] = {cl.model_precache, cl.model_precache_csqc};
	r_lightmap_buildstate_t state;
	qboolean skipupdates;
	size_t list;
	int modelindex, i;

	if (!lightmap_count)
		return;

	// Finish queued lighting before changing the shared atlas staging memory.
#ifndef SDL_THREADS_DISABLED
	RSceneCache_WaitForWorker ();
#endif
	R_LightmapBuildState_Snapshot (&state);
	lightmap_rebuilds++; // invalidate the scene cache's last lighting snapshot

	for (list = 0; list < countof(precaches); list++)
	for (modelindex = 1; modelindex < MAX_MODELS; modelindex++)
	{
		qmodel_t *model = precaches[list][modelindex];

		// Each parent BSP owns the surface array for all of its inline models.
		if (!model || model->type != mod_brush || model->needload || model->name[0] == '*')
			continue;
		for (i = 0; i < model->numsurfaces; i++)
		{
			msurface_t *surf = &model->surfaces[i];
			struct lightmap_s *lm;
			byte *base;

			if (!surf->cached_dlight || (surf->flags & SURF_DRAWTILED) ||
				(unsigned int)surf->lightmaptexturenum >= (unsigned int)lightmap_count)
				continue;
			lm = &lightmaps[surf->lightmaptexturenum];
			base = lm->pbodata + (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * lightmap_bytes;
			R_BuildLightMapForState (model, surf, base, LMBLOCK_WIDTH * lightmap_bytes,
				NULL, r_framecount, cl_dlights, &state);
			R_LightmapMarkDirtyRect (lm, surf->light_s, surf->light_t, surf->extents[0] + 1, surf->extents[1] + 1);
		}
	}

	// Publish the cleared rectangles before either rendering path draws them.
	skipupdates = lightmaps_skipupdates;
	lightmaps_skipupdates = false;
	R_UploadLightmaps ();
	lightmaps_skipupdates = skipupdates;
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

	lightmap_rebuilds++; // woods #dlightskip -- drops dlights the scenecache worker baked in

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
							GL_UNSIGNED_INT_2_10_10_10_REV, NULL);
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
							GL_UNSIGNED_INT_2_10_10_10_REV, lightmaps[i].pbodata);
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
		LM_ClearDirty (&lightmaps[i]); // woods #lmtiles -- whole texture just uploaded
	}
}

extern vec3_t	lightcolor; // woods #shadow
extern	vec3_t	lightspot; // woods #shadow
extern qboolean GL_DrawAliasShadowCheck (entity_t* e); // woods #shadow

// brush shadow projection: BRUSH_SHADOW_* in glquake.h -- woods #shadow

#define SHADOW_COMPUTED (1 << 0) // woods #shadow
#define SHADOW_VALID    (1 << 1) // woods #shadow

/*
==============================================================================

BRUSH SHADOW INDEX CACHE -- tb

The shadow silhouette is the model's static geometry, so it can be drawn from
gl_bmodel_vbo with a prebuilt index buffer instead of a glBegin/glEnd pair per
surface and a glVertex3fv per vertex.  On a busy ad_tears view that was ~47k GL
calls a frame for 43 entities.

This deliberately does NOT reuse R_BModelDrawCache.  The two paths agree on
rejecting entalpha < 1, but the cache additionally requires gl_cull, no
ent->effects at all (the shadow path only rejects EF_NOSHADOW), a live world
program, non-cheat-safe modes and supported lightmapped surfaces -- so borrowing it
would silently drop shadows this path draws.  Every surface the immediate path
would have drawn goes in the index buffer, in the same fan order.

State stays fixed-function (matrix stack, glColor, blend) exactly as before --
only the submission changes.  Immediate mode remains the fallback when VBOs are
unavailable or the buffer could not be built.
==============================================================================
*/
cvar_t r_shadows_buffered = {"r_shadows_buffered","1",CVAR_ARCHIVE};

typedef struct bshadowcache_s
{
	struct bshadowcache_s *next;
	qmodel_t	*model;
	unsigned int generation;	//gl_bmodel_vbo_generation when built
	GLuint		ebo;
	int			numindexes;
} bshadowcache_t;

static bshadowcache_t *bshadowcaches;

void GL_BrushShadowCache_Clear (void)
{	//safe to call regardless of GL capability: the CPU nodes are always freed.
	while (bshadowcaches)
	{
		bshadowcache_t *c = bshadowcaches;
		bshadowcaches = c->next;
		if (c->ebo && GL_DeleteBuffersFunc)
			GL_DeleteBuffersFunc (1, &c->ebo);
		free (c);
	}
}

static bshadowcache_t *GL_BrushShadowCache_Get (qmodel_t *model)
{
	bshadowcache_t *c;
	unsigned int *idx;
	size_t total, count;
	int i;
	msurface_t *surf;

	for (c = bshadowcaches; c; c = c->next)
		if (c->model == model)
		{
			if (c->generation == gl_bmodel_vbo_generation)
				return c->numindexes ? c : NULL;
			break;	//stale: vbo_firstvert offsets moved, rebuild below
		}

	if (!gl_vbo_able || !gl_bmodel_vbo || !GL_GenBuffersFunc || !GL_BufferDataFunc)
		return NULL;
	if (model->firstmodelsurface < 0 || model->nummodelsurfaces < 0 ||
		model->firstmodelsurface > model->numsurfaces - model->nummodelsurfaces)
		return NULL;

	//every surface is fan-triangulated, matching the immediate path's GL_POLYGON
	//R_SurfaceVertCount, not polys->numverts: GL_BuildBModelVertexBuffer sizes
	//each surface's VBO region with it, so using the raw poly count could emit
	//indices that run past this surface into the next one's vertices.
	total = 0;
	surf = &model->surfaces[model->firstmodelsurface];
	for (i = 0; i < model->nummodelsurfaces; i++, surf++)
	{
		size_t add;
		int nv = R_SurfaceVertCount (surf);
		if (!surf->polys || nv < 3)
			continue;
		//check each addition: a post-loop guard can only see a total that has
		//already wrapped. glDrawElements takes a signed GLsizei, so cap there.
		add = (size_t)(nv - 2) * 3;
		if (add > (size_t)INT_MAX - total)
			return NULL;
		total += add;
	}
	if (!total || total > SIZE_MAX / sizeof(unsigned int))
		return NULL;	//the INT_MAX cap is enforced per-addition above

	if (!c)
	{
		c = (bshadowcache_t *)calloc (1, sizeof(*c));
		if (!c)
			return NULL;
		c->model = model;
		c->next = bshadowcaches;
		bshadowcaches = c;
	}

	idx = (unsigned int *)malloc (total * sizeof(*idx));
	if (!idx)
		return NULL;

	count = 0;
	surf = &model->surfaces[model->firstmodelsurface];
	for (i = 0; i < model->nummodelsurfaces; i++, surf++)
	{
		int k, first = surf->vbo_firstvert, nv = R_SurfaceVertCount (surf);
		if (!surf->polys || nv < 3)
			continue;
		for (k = 2; k < nv; k++)
		{
			idx[count++] = (unsigned int)first;
			idx[count++] = (unsigned int)(first + k - 1);
			idx[count++] = (unsigned int)(first + k);
		}
	}

	if (!c->ebo)
		GL_GenBuffersFunc (1, &c->ebo);
	if (!c->ebo)
	{	//generation failed; leave the cache empty so the caller uses immediate mode
		free (idx);
		c->numindexes = 0;
		c->generation = gl_bmodel_vbo_generation;
		return NULL;
	}
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, c->ebo);
	GL_BufferDataFunc (GL_ELEMENT_ARRAY_BUFFER, count * sizeof(*idx), idx,
					   GL_STATIC_DRAW);
	free (idx);

	c->numindexes = (int)count;
	c->generation = gl_bmodel_vbo_generation;
	return c->numindexes ? c : NULL;
}

qboolean GL_BrushShadowCache_Info (qmodel_t *model, GLuint *ebo_out, int *numindexes_out)
{
	bshadowcache_t *c;

	if (!r_shadows_buffered.value || !gl_vbo_able || !gl_bmodel_vbo || !model)
		return false;
	c = GL_BrushShadowCache_Get (model);
	if (!c || !c->ebo || c->numindexes <= 0)
		return false;
	if (ebo_out)
		*ebo_out = c->ebo;
	if (numindexes_out)
		*numindexes_out = c->numindexes;
	return true;
}

/*
GL_DrawBrushShadow -- woods #shadow

tb: split into prepare / state / draw so the shadow queue (R_QueueBrushShadow in
r_alias.c) can draw brush shadows that overlap no other shadow with the
fixed-function state set once, instead of a dozen enable/disable and buffer
calls around every entity's single draw.
*/
static const float r_brush_shadowmatrix[16] = {
	1,              0,              0,              0,
	0,              1,              0,              0,
	BRUSH_SHADOW_SKEW_X,  BRUSH_SHADOW_SKEW_Y,  BRUSH_SHADOW_VSCALE,  0,
	0,                    0,                    BRUSH_SHADOW_HEIGHT,  1
};

qboolean GL_PrepareBrushShadow (entity_t *e, brushshadow_rec_t *rec)
{
	qmodel_t	*clmodel;
	float		entalpha, shade;

	if (!r_shadows_bmodels.value)
		return false;

	clmodel = e->model;

	if (R_CullModelForEntity(e))
	{
		return false;
	}

	if (e == &cl.viewent ||
		(e->effects & EF_NOSHADOW) ||
		(e->model->flags & MOD_NOSHADOW) ||
		clmodel == cl.worldmodel ||
		!clmodel->nummodelsurfaces) 
	{
		return false;
	}

	entalpha = ENTALPHA_DECODE(e->alpha);

	if (entalpha < 1) {
		return false;
	}

	if (r_shadows_groundcheck.value && e->model->type == mod_brush) {
		if (!(e->shadow_state & SHADOW_COMPUTED))
			GL_DrawAliasShadowCheck(e);

		if (!(e->shadow_state & SHADOW_VALID))
			return false;
	}

	// Determine lighting at entity origin
	R_LightPointCachedAlias(e, e->origin, 0);
	shade = ((lightcolor[0] + lightcolor[1] + lightcolor[2]) / 3) / 128.0f;
	rec->e = e;
	rec->lheight = e->origin[2] - lightspot[2];
	// Draw fully black, but alpha scaled by shade and the r_shadows cvar
	rec->alpha = entalpha * shade * r_shadows.value;
	return true;
}

void GL_BrushShadowBeginState (void)
{
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);

	// Enable polygon offset to prevent z-fighting
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1, -2);

	if (r_shadows_buffered.value && gl_vbo_able && gl_bmodel_vbo)
	{
		GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
		glEnableClientState (GL_VERTEX_ARRAY);
		glVertexPointer (3, GL_FLOAT, VERTEXSIZE * sizeof(float), (void *)0);
	}
}

void GL_BrushShadowEndState (void)
{
	if (r_shadows_buffered.value && gl_vbo_able && gl_bmodel_vbo)
	{
		glDisableClientState (GL_VERTEX_ARRAY);
		GL_BindBuffer (GL_ARRAY_BUFFER, 0);
		GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	// Restore states
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	if (gl_cull.value)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
}

void GL_DrawBrushShadowPrepared (const brushshadow_rec_t *rec)
{
	entity_t	*e = rec->e;
	qmodel_t	*clmodel = e->model;

	glPushMatrix();

	// Apply entity transformations
	R_RotateForEntity(e->origin, e->angles, e);

	// Move down to floor, apply shadow projection, then move back
	glTranslatef(0, 0, -rec->lheight);
	glMultMatrixf(r_brush_shadowmatrix);
	glTranslatef(0, 0, rec->lheight);

	glColor4f(0, 0, 0, rec->alpha);

	// Draw the model geometry as a flat polygon silhouette
	{
		msurface_t* surf = &clmodel->surfaces[clmodel->firstmodelsurface];
		int i;

		// BeginState bound the shared bmodel VBO and vertex array when this is usable
		bshadowcache_t *shcache = (r_shadows_buffered.value && gl_vbo_able && gl_bmodel_vbo) ?
		                          GL_BrushShadowCache_Get (clmodel) : NULL;

		if (shcache)
		{   //one indexed draw from the shared bmodel VBO
			GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, shcache->ebo);
			glDrawElements (GL_TRIANGLES, shcache->numindexes,
			                GL_UNSIGNED_INT, (void *)0);
		}
		else
		for (i = 0; i < clmodel->nummodelsurfaces; i++, surf++)
		{
			glpoly_t* p = surf->polys;
			int k, nv = R_SurfaceVertCount (surf);
			float* v;

			if (!p || nv < 3)
				continue;
			v = p->verts[0];
			glBegin(GL_POLYGON);
			for (k = 0; k < nv; k++, v += VERTEXSIZE)
			{
				glVertex3fv(v);
			}
			glEnd();
		}
	}

	glPopMatrix();
}

void GL_DrawBrushShadow (entity_t* e) // woods #shadow
{
	brushshadow_rec_t rec;

	if (!GL_PrepareBrushShadow (e, &rec))
		return;
	if (R_QueueBrushShadow (&rec))
		return;
	GL_BrushShadowBeginState ();
	GL_DrawBrushShadowPrepared (&rec);
	GL_BrushShadowEndState ();
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
