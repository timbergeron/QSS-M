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
// r_world.c: world model rendering

#include "quakedef.h"
#include "view.h" // woods #fxaa

extern cvar_t gl_fullbrights, r_drawflat, gl_overbright, r_oldskyleaf, r_showtris; //johnfitz
extern cvar_t r_flatlightstyles;
cvar_t r_scenecache = {"r_scenecache",""};	//spike, an attempt to cope with abusive maps a bit better.

typedef struct grass_presence_cache_s grass_presence_cache_t;

byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);
static qboolean RSceneCache_Queue(byte *vis);
static void RSceneCache_Draw(qboolean water);
void RSceneCache_Shutdown(void);
static qboolean R_GrassBladesActive (void);
static qboolean R_GrassSurfaceCanHaveBlades (const msurface_t *s);
static grass_presence_cache_t *R_GrassGetPresenceCache (qmodel_t *model, qboolean create);
static qboolean R_GrassPresenceCacheHasBladeSurfaces (const grass_presence_cache_t *cache);
static qboolean R_GrassSurfaceCanHaveBladesCached (const grass_presence_cache_t *cache, qmodel_t *model, const msurface_t *s);
#ifndef SDL_THREADS_DISABLED
static void R_GrassMarkSurfaceVisibleCached (grass_presence_cache_t *cache, qmodel_t *model, const msurface_t *s);
#endif
static int r_grass_scenecache_visframe;
extern qboolean lightmaps_skipupdates;
extern char	skybox_name[1024]; // woods -- #fastsky2
extern qboolean externalskyloaded; // woods -- #fastsky2

extern cvar_t r_skyspeed; // woods #skyspeed

static float Sky_GetTime (void) // woods #skyspeed
{
	float clamped_skyspeed = CLAMP(0, r_skyspeed.value, 100);
	return cl.time * clamped_skyspeed;
}

//==============================================================================
//
// SETUP CHAINS
//
//==============================================================================

// woods #caustics

typedef enum {
	ABOVE_WATER,
	IS_WATER,
	UNDER_WATER,
} surfacetype;

 GLuint causticsTexLoc;
 GLuint useCausticsTexLoc;

/*
================
R_ClearTextureChains -- ericw 

clears texture chains for all textures used by the given model, and also
clears the lightmap chains
================
*/
void R_ClearTextureChains (qmodel_t *mod, texchain_t chain)
{
	int i;

	// set all chains to null
	for (i=0 ; i<mod->numtextures ; i++)
		if (mod->textures[i])
			mod->textures[i]->texturechains[chain] = NULL;

	// clear lightmap chains
	for (i=0 ; i<lightmap_count ; i++)
		lightmaps[i].polys = NULL;
}

/*
================
R_ChainSurface -- ericw -- adds the given surface to its texture chain
================
*/
void R_ChainSurface (msurface_t *surf, texchain_t chain)
{
	surf->texturechain = surf->texinfo->texture->texturechains[chain];
	surf->texinfo->texture->texturechains[chain] = surf;
}

/*
================
R_BackFaceCull -- johnfitz -- returns true if the surface is facing away from vieworg
================
*/
qboolean R_BackFaceCull (msurface_t *surf)
{
	double dot;

	if (surf->plane->type < 3)
		dot = r_refdef.vieworg[surf->plane->type] - surf->plane->dist;
	else
		dot = DotProduct (r_refdef.vieworg, surf->plane->normal) - surf->plane->dist;

	if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK))
		return true;

	return false;
}

#ifndef SDL_THREADS_DISABLED
static void R_MarkGrassSurfaces (byte *vis)
{
	grass_presence_cache_t *presencecache;
	mleaf_t *leaf;
	msurface_t *surf, **mark;
	int i, j;

	if (!vis || !R_GrassBladesActive() || r_drawflat_cheatsafe || r_lightmap_cheatsafe)
		return;
	presencecache = R_GrassGetPresenceCache(cl.worldmodel, true);
	if (!R_GrassPresenceCacheHasBladeSurfaces(presencecache))
		return;

	r_grass_scenecache_visframe = r_visframecount;

	leaf = &cl.worldmodel->leafs[1];
	for (i = 0; i < cl.worldmodel->numleafs; i++, leaf++)
	{
		if (!(vis[i >> 3] & (1 << (i & 7))))
			continue;
		if (R_CullBox(leaf->minmaxs, leaf->minmaxs + 3))
			continue;

		if (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value)
		{
			for (j = 0, mark = leaf->firstmarksurface; j < leaf->nummarksurfaces; j++, mark++)
			{
				surf = *mark;
				if (R_GrassSurfaceCanHaveBladesCached(presencecache, cl.worldmodel, surf))
					R_GrassMarkSurfaceVisibleCached(presencecache, cl.worldmodel, surf);
			}
		}
	}
}
#endif

/*
===============
R_MarkSurfaces -- johnfitz -- mark surfaces based on PVS and rebuild texture chains
===============
*/
void R_MarkSurfaces (void)
{
	byte		*vis;
	mleaf_t		*leaf;
	msurface_t	*surf, **mark;
	int			i, j;
	qboolean	nearwaterportal;

	// clear lightmap chains
	for (i=0 ; i<lightmap_count ; i++)
		lightmaps[i].polys = NULL;

	// check this leaf for water portals
	// TODO: loop through all water surfs and use distance to leaf cullbox
	nearwaterportal = r_scenecache.value!=0;
	for (i=0, mark = r_viewleaf->firstmarksurface; i < r_viewleaf->nummarksurfaces; i++, mark++)
		if ((*mark)->flags & SURF_DRAWTURB)
			nearwaterportal = true;

	// choose vis data
	if (r_novis.value || r_viewleaf->contents == CONTENTS_SOLID || r_viewleaf->contents == CONTENTS_SKY)
		vis = Mod_NoVisPVS (cl.worldmodel);
	else if (nearwaterportal)
		vis = SV_FatPVS (r_origin, cl.worldmodel);
	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);

	r_visframecount++;

	// set all chains to null
	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
		if (cl.worldmodel->textures[i])
			cl.worldmodel->textures[i]->texturechains[chain_world] = NULL;

#ifndef SDL_THREADS_DISABLED
	if (RSceneCache_Queue(vis))
	{
		R_MarkGrassSurfaces(vis);
		return;
	}
	lightmaps_skipupdates = false;
#endif

	//need to do this somewhere...
	R_PushDlights ();

	// iterate through leaves, marking surfaces
	leaf = &cl.worldmodel->leafs[1];
	for (i=0 ; i<cl.worldmodel->numleafs ; i++, leaf++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			if (R_CullBox(leaf->minmaxs, leaf->minmaxs + 3))
				continue;

			if (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value)
				for (j=0, mark = leaf->firstmarksurface; j<leaf->nummarksurfaces; j++, mark++)
				{
					surf = *mark;
					if (surf->visframe != r_visframecount)
					{
						surf->visframe = r_visframecount;
						if (!R_CullBox(surf->mins, surf->maxs) && !R_BackFaceCull (surf))
						{
							rs_brushpolys++; //count wpolys here
							R_ChainSurface(surf, chain_world);
							R_RenderDynamicLightmaps(cl.worldmodel, surf);
						}
					}
				}

			// add static models
			if (leaf->efrags)
				R_StoreEfrags (&leaf->efrags);
		}
	}
}

//==============================================================================
//
// DRAW CHAINS
//
//==============================================================================

/*
=============
R_BeginTransparentDrawing -- ericw
=============
*/
static void R_BeginTransparentDrawing (float entalpha)
{
	if (entalpha < 1.0f)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
		
		if (vid_fxaa.value > 0 && GL_BlendFuncSeparateFunc) // woods #fxaa use separate alpha blending when FXAA is enabled to preserve transparency info
		{
			// RGB: normal alpha blending, Alpha: copy source alpha
			GL_BlendFuncSeparateFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
		}
		else
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Standard alpha blending
		}
		
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f (1,1,1,entalpha);
	}
}

/*
=============
R_EndTransparentDrawing -- ericw
=============
*/
static void R_EndTransparentDrawing (float entalpha)
{
	if (entalpha < 1.0f)
	{
		glDepthMask (GL_TRUE);
		glDisable (GL_BLEND);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor3f (1, 1, 1);
	}
}

/*
================
R_DrawTextureChains_ShowTris -- johnfitz
================
*/
void R_DrawTextureChains_ShowTris (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	glpoly_t	*p;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];
		if (!t)
			continue;

		if (!gl_glsl_water_able && t->texturechains[chain] && (t->texturechains[chain]->flags & SURF_DRAWTURB))
		{
			for (s = t->texturechains[chain]; s; s = s->texturechain)
				for (p = s->polys->next; p; p = p->next)
				{
					DrawGLTriangleFan (p);
				}
		}
		else
		{
			for (s = t->texturechains[chain]; s; s = s->texturechain)
			{
				DrawGLTriangleFan (s->polys);
			}
		}
	}
}

/*
================
R_DrawTextureChains_Drawflat -- johnfitz
================
*/
void R_DrawTextureChains_Drawflat (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	glpoly_t	*p;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];
		if (!t)
			continue;

		if (!gl_glsl_water_able  && t->texturechains[chain] && (t->texturechains[chain]->flags & SURF_DRAWTURB))
		{
			for (s = t->texturechains[chain]; s; s = s->texturechain)
				for (p = s->polys->next; p; p = p->next)
				{
					srand((unsigned int) (uintptr_t) p);
					glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
					DrawGLPoly (p);
					rs_brushpasses++;
				}
		}
		else
		{
			for (s = t->texturechains[chain]; s; s = s->texturechain)
			{
				srand((unsigned int) (uintptr_t) s->polys);
				glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
				DrawGLPoly (s->polys);
				rs_brushpasses++;
			}
		}
	}
	glColor3f (1,1,1);
	srand ((int) (cl.time * 1000));
}

/*
================
R_DrawTextureChains_Glow -- johnfitz
================
*/
void R_DrawTextureChains_Glow (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	gltexture_t	*glt;
	qboolean	bound;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(glt = R_TextureAnimation(t, ent != NULL ? ent->frame : 0)->fullbright))
			continue;

		bound = false;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_Bind (glt);
				bound = true;
			}
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
	}
}

//==============================================================================
//
// VBO SUPPORT
//
//==============================================================================

static unsigned int R_NumTriangleIndicesForSurf (msurface_t *s)
{
	return 3 * (s->numedges - 2);
}

/*
================
R_TriangleIndicesForSurf

Writes out the triangle indices needed to draw s as a triangle list.
The number of indices it will write is given by R_NumTriangleIndicesForSurf.
================
*/
static void R_TriangleIndicesForSurf (msurface_t *s, unsigned int *dest)
{
	int i;
	for (i=2; i<s->numedges; i++)
	{
		*dest++ = s->vbo_firstvert;
		*dest++ = s->vbo_firstvert + i - 1;
		*dest++ = s->vbo_firstvert + i;
	}
}

#define MAX_BATCH_SIZE 65536

static unsigned int vbo_indices[MAX_BATCH_SIZE];
static unsigned int num_vbo_indices;

/*
================
R_ClearBatch
================
*/
static void R_ClearBatch ()
{
	num_vbo_indices = 0;
}

/*
================
R_FlushBatch

Draw the current batch if non-empty and clears it, ready for more R_BatchSurface calls.
================
*/
static void R_FlushBatch (surfacetype surftype) // woods #caustics
{
	if (num_vbo_indices > 0)
	{
		if (surftype == UNDER_WATER && gl_caustics.value && underwatertexture) // woods #caustics
		{
			GL_SelectTexture(GL_TEXTURE3);
			GL_Bind(underwatertexture);
			GL_Uniform1iFunc(useCausticsTexLoc, 1);
		}
		else
			GL_Uniform1iFunc(useCausticsTexLoc, 0);
		
		glDrawElements (GL_TRIANGLES, num_vbo_indices, GL_UNSIGNED_INT, vbo_indices);
		num_vbo_indices = 0;
	}
}

/*
================
R_BatchSurface

Add the surface to the current batch, or just draw it immediately if we're not
using VBOs.
================
*/
static void R_BatchSurface (msurface_t *s, surfacetype surftype) // woods #caustics
{
	unsigned int num_surf_indices;

	num_surf_indices = R_NumTriangleIndicesForSurf (s);
	if (num_surf_indices-1u<=MAX_BATCH_SIZE)	//ericw's qbsp bugs out sometimes. don't crash.
	{
		if (num_vbo_indices + num_surf_indices > MAX_BATCH_SIZE)
			R_FlushBatch(surftype); // woods #caustics

		R_TriangleIndicesForSurf (s, &vbo_indices[num_vbo_indices]);
		num_vbo_indices += num_surf_indices;
	}
}

/*
================
R_DrawTextureChains_Multitexture -- johnfitz
================
*/
void R_DrawTextureChains_Multitexture (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i, j;
	msurface_t	*s;
	texture_t	*t;
	float		*v;
	qboolean	bound;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

		bound = false;
		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_Bind ((R_TextureAnimation(t, ent != NULL ? ent->frame : 0))->gltexture);
					
				if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
					glEnable (GL_ALPHA_TEST); // Flip alpha test back on
					
				GL_EnableMultitexture(); // selects TEXTURE1
				bound = true;
			}
			GL_Bind (lightmaps[s->lightmaptexturenum].texture);
			glBegin(GL_POLYGON);
			v = s->polys->verts[0];
			for (j=0 ; j<s->polys->numverts ; j++, v+= VERTEXSIZE)
			{
				GL_MTexCoord2fFunc (GL_TEXTURE0_ARB, v[3], v[4]);
				GL_MTexCoord2fFunc (GL_TEXTURE1_ARB, v[5], v[6]);
				glVertex3fv (v);
			}
			glEnd ();
			rs_brushpasses++;
		}
		GL_DisableMultitexture(); // selects TEXTURE0

		if (bound && t->texturechains[chain]->flags & SURF_DRAWFENCE)
			glDisable (GL_ALPHA_TEST); // Flip alpha test back off
	}
}

/*
================
R_DrawTextureChains_NoTexture -- johnfitz

draws surfs whose textures were missing from the BSP
================
*/
void R_DrawTextureChains_NoTexture (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	qboolean	bound;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_NOTEXTURE))
			continue;

		bound = false;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_Bind (t->gltexture);
				bound = true;
			}
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
	}
}

/*
================
R_DrawTextureChains_TextureOnly -- johnfitz
================
*/
void R_DrawTextureChains_TextureOnly (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	qboolean	bound;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
			continue;

		bound = false;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_Bind ((R_TextureAnimation(t, ent != NULL ? ent->frame : 0))->gltexture);
					
				if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
					glEnable (GL_ALPHA_TEST); // Flip alpha test back on
					
				bound = true;
			}
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}

		if (bound && t->texturechains[chain]->flags & SURF_DRAWFENCE)
			glDisable (GL_ALPHA_TEST); // Flip alpha test back off
	}
}

/*
================
GL_WaterAlphaForEntitySurface -- ericw
 
Returns the water alpha to use for the entity and surface combination.
================
*/
float GL_WaterAlphaForEntitySurface (entity_t *ent, msurface_t *s)
{
	float entalpha;
	if (ent == NULL || ent->alpha == ENTALPHA_DEFAULT)
		entalpha = GL_WaterAlphaForSurface(s);
	else
		entalpha = ENTALPHA_DECODE(ent->alpha);
	return entalpha;
}


static GLuint r_world_program;
extern GLuint gl_bmodel_vbo;

// uniforms used in frag shader
static GLuint texLoc;
static GLuint LMTexLoc;
static GLuint fullbrightTexLoc;
static GLuint useFullbrightTexLoc;
static GLuint useOverbrightLoc;
static GLuint useAlphaTestLoc;
static GLuint useLightmapWideLoc;
static GLuint useLightmapOnlyLoc;
static GLuint alphaLoc;
GLuint clTimeLoc; // woods #caustics
static GLuint causticsOpacityLoc; // woods #caustics
static GLint useGrassLoc; // woods #grass
static GLint grassAmountLoc; // woods #grass
static GLint grassTimeLoc; // woods #grass
static GLint grassBaseColorLoc; // woods #grass
static GLint grassTipColorLoc; // woods #grass
static GLint grassMovementLoc; // woods #grass
static GLint fogModeLoc;
static GLuint r_grass_program; // woods #grass
static GLint grassGeomAmountLoc; // woods #grass
static GLint grassGeomTimeLoc; // woods #grass
static GLint grassGeomMovementLoc; // woods #grass
static GLint grassGeomFadeDistLoc; // woods #grass
static GLint grassGeomFogModeLoc;
static GLint grassGeomEyePosLoc;
static GLint grassGeomStaticModeLoc;
static GLint grassGeomStaticNormalLoc;
static GLint grassGeomStaticTangentLoc;
static GLint grassGeomStaticLodLoc;
static GLint grassGeomStaticCellWeightLoc;
static GLint grassGeomDLightCountLoc;
static GLint grassGeomDLightPosRadiusLoc;
static GLint grassGeomDLightColorMinLoc;

#define GRASS_SHADER_DLIGHTS 4 // woods #grass -- keep in sync with GLSL define


static struct
{
	GLuint program;

	GLuint light_scale;
	GLuint alpha_scale;
	GLuint time;
	GLuint eyepos;
	GLuint fogalpha;
	GLuint colour;
	GLint fogmode;
	GLint skyfogcolor;
} r_water[4];	//

static void GLWorld_DeleteShaderPrograms (void)
{
	int i;

	GL_DeleteProgramTracked(&r_world_program);
	GL_DeleteProgramTracked(&r_grass_program);
	for (i = 0; i < countof(r_water); i++)
		GL_DeleteProgramTracked(&r_water[i].program);

	r_world_program = 0;
	texLoc = 0;
	LMTexLoc = 0;
	fullbrightTexLoc = 0;
	causticsTexLoc = 0;
	useFullbrightTexLoc = 0;
	useOverbrightLoc = 0;
	useAlphaTestLoc = 0;
	useCausticsTexLoc = 0;
	useLightmapWideLoc = 0;
	useLightmapOnlyLoc = 0;
	alphaLoc = 0;
	clTimeLoc = 0;
	causticsOpacityLoc = 0;
	useGrassLoc = -1;
	grassAmountLoc = -1;
	grassTimeLoc = -1;
	grassBaseColorLoc = -1;
	grassTipColorLoc = -1;
	grassMovementLoc = -1;
	fogModeLoc = -1;

	r_grass_program = 0;
	grassGeomAmountLoc = -1;
	grassGeomTimeLoc = -1;
	grassGeomMovementLoc = -1;
	grassGeomFadeDistLoc = -1;
	grassGeomFogModeLoc = -1;
	grassGeomEyePosLoc = -1;
	grassGeomStaticModeLoc = -1;
	grassGeomStaticNormalLoc = -1;
	grassGeomStaticTangentLoc = -1;
	grassGeomStaticLodLoc = -1;
	grassGeomStaticCellWeightLoc = -1;
	grassGeomDLightCountLoc = -1;
	grassGeomDLightPosRadiusLoc = -1;
	grassGeomDLightColorMinLoc = -1;

	for (i = 0; i < countof(r_water); i++)
	{
		r_water[i].program = 0;
		r_water[i].light_scale = 0;
		r_water[i].alpha_scale = 0;
		r_water[i].time = 0;
		r_water[i].eyepos = 0;
		r_water[i].fogalpha = 0;
		r_water[i].colour = 0;
		r_water[i].fogmode = -1;
		r_water[i].skyfogcolor = -1;
	}
}

#define vertAttrIndex 0
#define texCoordsAttrIndex 1
#define LMCoordsAttrIndex 2
#define GRASS_BLADE_MODE_CPU 1
#define GRASS_BLADE_MODE_SHADER 2
#define GRASS_DENSITY_MAX 500.0f
#define GRASS_DIST_MAX 8192.0f
#define GRASS_DEFAULT_AMOUNT 1.0f
#define GRASS_DEFAULT_BLADES 2.0f
#define GRASS_DEFAULT_DENSITY 0.35f
#define GRASS_DEFAULT_HEIGHT 18.0f
#define GRASS_DEFAULT_DIST 1024.0f
#define GRASS_DEFAULT_MOVEMENT 0.35f
#define GRASS_DEFAULT_LOD 1.0f
#define GRASS_CUSTOM_VALUE_MAX 7
#define GRASS_VERTEX_BATCH_MAX 65532
#define GRASS_SURFACE_BLADE_MAX 262144
#define GRASS_SURFACE_CELL_SCAN_MAX 1048576.0
#define GRASS_LIGHT_CACHE_SIZE 256
#define GRASS_LIGHT_CACHE_PROBES 8
#define GRASS_LIGHT_CACHE_CELL 64.0f
#define GRASS_TIME_WRAP 4096.0
#define GRASS_SHADER_LOD_LEVELS 4

typedef struct grass_settings_s
{
	float amount;
	float blades;
	float density;
	float height;
	float dist;
	float movement;
	float lod;
} grass_settings_t;

typedef struct grass_lod_params_s
{
	float dist;
	float dist2;
	float nearclip2;
	float invfade;
	float lod;
	qboolean use_dist;
} grass_lod_params_t;

typedef struct grass_dlight_s
{
	vec3_t origin;
	vec3_t color;
	float radius;
	float minlight;
	float cull_radius2;
} grass_dlight_t;

typedef struct grass_dlight_list_s
{
	int count;
	grass_dlight_t lights[MAX_DLIGHTS];
} grass_dlight_list_t;

typedef struct grass_light_cache_entry_s
{
	unsigned int generation;
	qboolean cheap;
	int cell[3];
	vec3_t light;
} grass_light_cache_entry_t;

typedef struct grass_vertex_s
{
	float vertex[3];
	float color[4];
	float texcoord[4];
} grass_vertex_t;

typedef struct grass_shader_vertex_s
{
	float base[3];
	float color[4];
	float bladecoord[4];
	float geom[4];
} grass_shader_vertex_t;

typedef struct grass_cached_blade_s
{
	vec3_t pos;
	unsigned int seed;
	unsigned int bladebits;
	unsigned int colorbits;
	unsigned int amountbits;
	unsigned int lodbits;
	float heightscale;
	int cell_x;
	int cell_y;
} grass_cached_blade_t;

typedef struct grass_surface_cache_s
{
	const msurface_t *surface;
	grass_cached_blade_t *blades;
	int count;
	int capacity;
	qboolean built;
	qboolean failed;
	GLuint shader_vbo[GRASS_SHADER_LOD_LEVELS];
	int shader_vertex_count[GRASS_SHADER_LOD_LEVELS];
	float shader_baseheight[GRASS_SHADER_LOD_LEVELS];
	vec3_t shader_basecolor[GRASS_SHADER_LOD_LEVELS];
	vec3_t shader_tipcolor[GRASS_SHADER_LOD_LEVELS];
	unsigned int shader_lightstyle_hash[GRASS_SHADER_LOD_LEVELS];
	qboolean shader_built[GRASS_SHADER_LOD_LEVELS];
	qboolean shader_failed[GRASS_SHADER_LOD_LEVELS];
} grass_surface_cache_t;

typedef struct grass_model_cache_s
{
	qmodel_t *model;
	int firstsurface;
	int numsurfaces;
	float density;
	float cellsize;
	grass_surface_cache_t *surfaces;
	struct grass_model_cache_s *next;
} grass_model_cache_t;

struct grass_presence_cache_s
{
	qmodel_t *model;
	int firstsurface;
	int numsurfaces;
	unsigned int texhash;
	qboolean has_blade_surfaces;
	byte *surface_blade_flags;
	int *surface_visframes;
	struct grass_presence_cache_s *next;
};

typedef struct grass_brush_blocker_s
{
	vec3_t mins;
	vec3_t maxs;
} grass_brush_blocker_t;

static grass_settings_t grass_settings;
static char grass_settings_string[256];
static qboolean grass_settings_valid;
static grass_vertex_t *r_grass_vertex_batch;
static int r_grass_vertex_count;
static GLuint r_grass_vertex_vbo;
static grass_light_cache_entry_t r_grass_light_cache[GRASS_LIGHT_CACHE_SIZE];
static unsigned int r_grass_light_cache_generation;
static grass_model_cache_t *r_grass_model_caches;
static grass_presence_cache_t *r_grass_presence_caches;
static qmodel_t *r_grass_cache_worldmodel;
static qmodel_t *r_grass_brush_blocker_worldmodel;
static byte *r_grass_brush_blocker_submodels;
static int r_grass_brush_blocker_submodel_count;
static grass_brush_blocker_t *r_grass_brush_blockers;
static int r_grass_brush_blocker_count;

static float R_GrassAnimTime (void)
{
	double t;

	t = fmod(cl.time, GRASS_TIME_WRAP);
	if (t < 0.0)
		t += GRASS_TIME_WRAP;
	return (float)t;
}

static qboolean R_GrassUseVertexVBO (void)
{
	return r_grass_vertex_vbo != 0 && gl_vbo_able && GL_BufferDataFunc;
}

static qboolean R_GrassUseStaticShaderVBO (void)
{
	return gl_vbo_able && GL_GenBuffersFunc && GL_BufferDataFunc &&
		GL_ClientActiveTextureFunc && GL_Uniform3fFunc && gl_max_texture_units >= 2;
}

static qboolean R_GrassValueSeparator (char c)
{
	return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int R_GrassParseValues (const char *s, float *values, int maxvalues)
{
	int count;

	count = 0;
	while (s && *s && count < maxvalues)
	{
		char *end;
		double parsed;
		float value;

		while (*s && R_GrassValueSeparator(*s))
			s++;
		if (!*s)
			break;

		parsed = strtod(s, &end);
		value = (float)parsed;
		if (end == s || !isfinite(parsed) || !isfinite(value))
			break;

		values[count] = value;
		count++;
		s = end;
		/* Stop on malformed tokens like "0.5x" instead of skipping ahead. */
		if (*s && !R_GrassValueSeparator(*s))
			return count;
	}

	return count;
}

static void R_GrassSettingsDefaults (grass_settings_t *settings)
{
	settings->amount = GRASS_DEFAULT_AMOUNT;
	settings->blades = GRASS_DEFAULT_BLADES;
	settings->density = GRASS_DEFAULT_DENSITY;
	settings->height = GRASS_DEFAULT_HEIGHT;
	settings->dist = GRASS_DEFAULT_DIST;
	settings->movement = GRASS_DEFAULT_MOVEMENT;
	settings->lod = GRASS_DEFAULT_LOD;
}

static const grass_settings_t *R_GrassSettings (void)
{
	const char *s;
	float values[GRASS_CUSTOM_VALUE_MAX];
	int count;

	s = r_grass.string ? r_grass.string : "";
	if (grass_settings_valid && !strcmp(grass_settings_string, s))
		return &grass_settings;

	R_GrassSettingsDefaults(&grass_settings);
	count = R_GrassParseValues(s, values, countof(values));

	if (count <= 0)
		grass_settings.amount = 0.0f;
	else if (count == 1)
		grass_settings.amount = CLAMP(0.0f, values[0], 1.0f);
	else if (count >= 7)
	{
		grass_settings.amount = CLAMP(0.0f, values[0], 1.0f);
		grass_settings.blades = values[1];
		grass_settings.density = values[2];
		grass_settings.height = values[3];
		grass_settings.dist = values[4];
		grass_settings.movement = values[5];
		grass_settings.lod = values[6];
	}
	else
	{
		grass_settings.amount = GRASS_DEFAULT_AMOUNT;
		grass_settings.blades = values[0];
		if (count > 1)
			grass_settings.density = values[1];
		if (count > 2)
			grass_settings.height = values[2];
		if (count > 3)
			grass_settings.dist = values[3];
		if (count > 4)
			grass_settings.movement = values[4];
		if (count > 5)
			grass_settings.lod = values[5];
	}

	q_strlcpy(grass_settings_string, s, sizeof(grass_settings_string));
	grass_settings_valid = true;
	return &grass_settings;
}

static qboolean R_GrassEnabled (void)
{
	return R_GrassSettings()->amount > 0.0f;
}

static float R_GrassAmount (void)
{
	return CLAMP(0.0f, R_GrassSettings()->amount, 1.0f);
}

static float R_GrassMovement (void)
{
	return CLAMP(0.0f, R_GrassSettings()->movement, 2.0f);
}

static int R_GrassBladeMode (void)
{
	static qboolean warned_shader_fallback = false;
	float blades;

	blades = R_GrassSettings()->blades;
	if (blades <= 0.0f)
	{
		warned_shader_fallback = false;
		return 0;
	}
	if (blades >= 2.0f)
	{
		if (r_grass_program != 0)
		{
			warned_shader_fallback = false;
			return GRASS_BLADE_MODE_SHADER;
		}
		if (!warned_shader_fallback)
		{
			Con_Printf("r_grass shader blades requested, but the grass GLSL shader is unavailable; falling back to CPU blades\n");
			warned_shader_fallback = true;
		}
	}
	else
		warned_shader_fallback = false;

	return GRASS_BLADE_MODE_CPU;
}

static qboolean R_GrassTexSeparator (char c)
{
	return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const char *R_TextureGrassBaseName (const char *name)
{
	if (name[0] == '+' && name[1] && name[2])
		return name + 2;
	return name;
}

static qboolean R_TextureNameMatchesGrassTex (const char *name, const char *token, size_t token_len)
{
	const char *basename;

	basename = R_TextureGrassBaseName(name);
	if (strlen(basename) != token_len)
		return false;

	return !q_strncasecmp(basename, token, token_len);
}

static qboolean R_TextureMatchesGrassTex (const texture_t *t, qboolean *has_tokens)
{
	const char *s, *start;
	size_t token_len;

	*has_tokens = false;
	if (!r_grass_tex.string)
		return false;

	s = r_grass_tex.string;
	while (*s)
	{
		while (*s && R_GrassTexSeparator(*s))
			s++;
		if (!*s)
			break;

		start = s;
		while (*s && !R_GrassTexSeparator(*s))
			s++;

		token_len = (size_t)(s - start);
		if (token_len > 0)
		{
			*has_tokens = true;
			if (R_TextureNameMatchesGrassTex(t->name, start, token_len))
				return true;
		}
	}

	return false;
}

static qboolean R_TextureHasGrass (const texture_t *t)
{
	const char *name;
	qboolean has_tex_tokens;

	if (!R_GrassEnabled() || !t || !t->name[0])
		return false;

	name = R_TextureGrassBaseName(t->name);
	if (name[0] == '*' || name[0] == '!' || name[0] == '{')
		return false;
	if (!q_strncasecmp(name, "sky", 3))
		return false;

	if (R_TextureMatchesGrassTex(t, &has_tex_tokens))
		return true;
	if (has_tex_tokens)
		return false;

	return t->grass_detected;
}

static qboolean R_TextureUsesSurfaceGrass (const texture_t *t)
{
	return R_GrassBladeMode() == 0 && R_TextureHasGrass(t);
}

static qboolean R_GrassBladesActive (void)
{
	const grass_settings_t *settings;

	settings = R_GrassSettings();
	return settings->amount > 0.0f && R_GrassBladeMode() != 0 &&
		settings->density > 0.0f && settings->height > 0.0f;
}

static unsigned int R_GrassHashUInt (unsigned int x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

static float R_GrassHashFloat (unsigned int x)
{
	return (R_GrassHashUInt(x) & 0x00ffffffU) * (1.0f / 16777215.0f);
}

static float R_GrassBitsToFloat (unsigned int x)
{
	return (x & 0x00ffffffU) * (1.0f / 16777215.0f);
}

static void R_GrassApplyBladeColorVariation (unsigned int colorbits, const vec3_t basein, const vec3_t tipin, vec3_t baseout, vec3_t tipout)
{
	float warm, dry, bright, tipboost;

	warm = (float)(colorbits & 0xffU) * (1.0f / 255.0f) - 0.5f;
	dry = (float)((colorbits >> 8) & 0xffU) * (1.0f / 255.0f) - 0.5f;
	bright = 0.88f + (float)((colorbits >> 16) & 0xffU) * (0.24f / 255.0f);
	tipboost = 0.92f + (float)((colorbits >> 24) & 0xffU) * (0.16f / 255.0f);

	baseout[0] = basein[0] * CLAMP(0.78f, bright * (1.0f + warm * 0.10f + dry * 0.06f), 1.22f);
	baseout[1] = basein[1] * CLAMP(0.78f, bright * (1.0f - dry * 0.05f), 1.22f);
	baseout[2] = basein[2] * CLAMP(0.78f, bright * (1.0f - warm * 0.08f - dry * 0.06f), 1.22f);
	tipout[0] = tipin[0] * CLAMP(0.80f, bright * tipboost * (1.0f + warm * 0.12f + dry * 0.08f), 1.25f);
	tipout[1] = tipin[1] * CLAMP(0.80f, bright * tipboost * (1.0f - dry * 0.04f), 1.25f);
	tipout[2] = tipin[2] * CLAMP(0.80f, bright * tipboost * (1.0f - warm * 0.10f - dry * 0.08f), 1.25f);
}

static void R_TextureGrassBladeColors (const texture_t *t, vec3_t base, vec3_t tip)
{
	vec3_t color;

	if (t && t->grass_color_valid)
		VectorCopy(t->grass_color, color);
	else
	{
		color[0] = 0.11f;
		color[1] = 0.34f;
		color[2] = 0.045f;
	}

	VectorScale(color, 0.35f, base);
	VectorCopy(color, tip);
}

static void R_SetGrassColorUniforms (const texture_t *t)
{
	vec3_t base, tip;

	R_TextureGrassBladeColors(t, base, tip);
	if (grassBaseColorLoc != -1)
		GL_Uniform3fFunc(grassBaseColorLoc, base[0], base[1], base[2]);
	if (grassTipColorLoc != -1)
		GL_Uniform3fFunc(grassTipColorLoc, tip[0], tip[1], tip[2]);
}

static void R_GrassAddAmbientLight (vec3_t blocklight)
{
	float ambient;

	if (cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cls.demoplayback)
		return;

	ambient = CLAMP(0.0f, r_ambient.value, 255.0f) * 256.0f;
	blocklight[0] += ambient;
	blocklight[1] += ambient;
	blocklight[2] += ambient;
}

static void R_GrassSampleHDRLight (const uint32_t *lightmap, int index, float scale, vec3_t sample)
{
	static const float rgb9e5tab[32] = {
		1.0f/(1<<24),	1.0f/(1<<23),	1.0f/(1<<22),	1.0f/(1<<21),	1.0f/(1<<20),	1.0f/(1<<19),	1.0f/(1<<18),	1.0f/(1<<17),
		1.0f/(1<<16),	1.0f/(1<<15),	1.0f/(1<<14),	1.0f/(1<<13),	1.0f/(1<<12),	1.0f/(1<<11),	1.0f/(1<<10),	1.0f/(1<<9),
		1.0f/(1<<8),	1.0f/(1<<7),	1.0f/(1<<6),	1.0f/(1<<5),	1.0f/(1<<4),	1.0f/(1<<3),	1.0f/(1<<2),	1.0f/(1<<1),
		1.0f,			1.0f*(1<<1),	1.0f*(1<<2),	1.0f*(1<<3),	1.0f*(1<<4),	1.0f*(1<<5),	1.0f*(1<<6),	1.0f*(1<<7),
	};
	uint32_t e5bgr9;
	float e;

	e5bgr9 = lightmap[index];
	e = rgb9e5tab[e5bgr9 >> 27] * (1 << 7) * scale;
	sample[0] = ((e5bgr9 >> 0) & 0x1ff) * e;
	sample[1] = ((e5bgr9 >> 9) & 0x1ff) * e;
	sample[2] = ((e5bgr9 >> 18) & 0x1ff) * e;
}

static void R_GrassSampleRGBLight (const byte *lightmap, int index, float scale, vec3_t sample)
{
	lightmap += index * 3;
	sample[0] = lightmap[0] * scale;
	sample[1] = lightmap[1] * scale;
	sample[2] = lightmap[2] * scale;
}

static void R_GrassAddWeightedLight (vec3_t blocklight, const vec3_t sample, float weight)
{
	blocklight[0] += sample[0] * weight;
	blocklight[1] += sample[1] * weight;
	blocklight[2] += sample[2] * weight;
}

static void R_GrassAddSurfaceLightmap (const qmodel_t *model, const msurface_t *s, const vec3_t point, qboolean interpolate, vec3_t blocklight)
{
	float lm_s, lm_t, frac_s, frac_t;
	int smax, tmax, size, s0, s1, t0, t1, idx00, idx10, idx01, idx11, maps;

	if (r_fullbright_cheatsafe || !model->lightdata)
	{
		blocklight[0] += 32768.0f;
		blocklight[1] += 32768.0f;
		blocklight[2] += 32768.0f;
		return;
	}

	R_GrassAddAmbientLight(blocklight);
	if (!s->samples)
		return;

	smax = s->extents[0] + 1;
	tmax = s->extents[1] + 1;
	if (smax <= 0 || tmax <= 0)
		return;
	size = smax * tmax;

	lm_s = DotProduct(point, s->lmvecs[0]) + s->lmvecs[0][3];
	lm_t = DotProduct(point, s->lmvecs[1]) + s->lmvecs[1][3];
	lm_s = CLAMP(0.0f, lm_s, (float)(smax - 1));
	lm_t = CLAMP(0.0f, lm_t, (float)(tmax - 1));

	if (interpolate)
	{
		s0 = (int)floorf(lm_s);
		t0 = (int)floorf(lm_t);
		s1 = q_min(s0 + 1, smax - 1);
		t1 = q_min(t0 + 1, tmax - 1);
		frac_s = lm_s - s0;
		frac_t = lm_t - t0;
	}
	else
	{
		s0 = (int)floorf(lm_s + 0.5f);
		t0 = (int)floorf(lm_t + 0.5f);
		s1 = s0;
		t1 = t0;
		frac_s = frac_t = 0.0f;
	}

	idx00 = t0 * smax + s0;
	idx10 = t0 * smax + s1;
	idx01 = t1 * smax + s0;
	idx11 = t1 * smax + s1;

	if (model->flags & MOD_HDRLIGHTING)
	{
		const uint32_t *lightmap = (const uint32_t *)s->samples;

		for (maps = 0; maps < MAXLIGHTMAPS && s->styles[maps] != INVALID_LIGHTSTYLE; maps++)
		{
			vec3_t sample;
			float scale = (float)d_lightstylevalue[s->styles[maps]];

			if (interpolate)
			{
				R_GrassSampleHDRLight(lightmap, idx00, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, (1.0f - frac_s) * (1.0f - frac_t));
				R_GrassSampleHDRLight(lightmap, idx10, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, frac_s * (1.0f - frac_t));
				R_GrassSampleHDRLight(lightmap, idx01, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, (1.0f - frac_s) * frac_t);
				R_GrassSampleHDRLight(lightmap, idx11, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, frac_s * frac_t);
			}
			else
			{
				R_GrassSampleHDRLight(lightmap, idx00, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, 1.0f);
			}
			lightmap += size;
		}
	}
	else
	{
		const byte *lightmap = (const byte *)s->samples;

		for (maps = 0; maps < MAXLIGHTMAPS && s->styles[maps] != INVALID_LIGHTSTYLE; maps++)
		{
			vec3_t sample;
			float scale = (float)d_lightstylevalue[s->styles[maps]];

			if (interpolate)
			{
				R_GrassSampleRGBLight(lightmap, idx00, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, (1.0f - frac_s) * (1.0f - frac_t));
				R_GrassSampleRGBLight(lightmap, idx10, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, frac_s * (1.0f - frac_t));
				R_GrassSampleRGBLight(lightmap, idx01, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, (1.0f - frac_s) * frac_t);
				R_GrassSampleRGBLight(lightmap, idx11, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, frac_s * frac_t);
			}
			else
			{
				R_GrassSampleRGBLight(lightmap, idx00, scale, sample);
				R_GrassAddWeightedLight(blocklight, sample, 1.0f);
			}
			lightmap += size * 3;
		}
	}
}

static void R_GrassPointToEntitySpace (const entity_t *ent, const vec3_t in, vec3_t out)
{
	vec3_t local;

	if (!ent)
	{
		VectorCopy(in, out);
		return;
	}

	VectorSubtract(in, ent->origin, local);
	if (ent->angles[0] || ent->angles[1] || ent->angles[2])
	{
		vec3_t angles, forward, right, up;

		VectorCopy(ent->angles, angles);
		AngleVectors(angles, forward, right, up);
		out[0] = DotProduct(local, forward);
		out[1] = -DotProduct(local, right);
		out[2] = DotProduct(local, up);
	}
	else
		VectorCopy(local, out);
}

static void R_GrassPointFromEntitySpace (const entity_t *ent, const vec3_t in, vec3_t out)
{
	if (!ent)
	{
		VectorCopy(in, out);
		return;
	}

	if (ent->angles[0] || ent->angles[1] || ent->angles[2])
	{
		vec3_t angles, forward, right, up;

		VectorCopy(ent->angles, angles);
		AngleVectors(angles, forward, right, up);
		VectorScale(forward, in[0], out);
		VectorMA(out, -in[1], right, out);
		VectorMA(out, in[2], up, out);
		VectorAdd(out, ent->origin, out);
	}
	else
		VectorAdd(in, ent->origin, out);
}

static qboolean R_GrassLightIntersectsSurfaceBounds (const msurface_t *s, const vec3_t origin, float radius)
{
	int i;
	vec3_t closest, delta;

	for (i = 0; i < 3; i++)
		closest[i] = CLAMP(s->mins[i], origin[i], s->maxs[i]);

	VectorSubtract(closest, origin, delta);
	return DotProduct(delta, delta) < radius * radius;
}

static void R_GrassBuildDlightList (const msurface_t *s, const entity_t *ent, grass_dlight_list_t *list, qboolean force_scan)
{
	int lnum;
	qboolean use_dlightbits;

	list->count = 0;
	use_dlightbits = !force_scan && (s->dlightframe == r_framecount);
	if (!use_dlightbits && !force_scan)
		return;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		dlight_t *light;
		grass_dlight_t *dst;
		vec3_t lightorg;
		float cull_radius;

		if (use_dlightbits && !(s->dlightbits[lnum >> 5] & (1U << (lnum & 31))))
			continue;

		light = &cl_dlights[lnum];
		if (light->die < cl.time || (light->spawn > cl.mtime[0] && cls.demoplayback) || !light->radius)
			continue;
		cull_radius = light->radius - light->minlight;
		if (cull_radius <= 0.0f)
			continue;
		R_GrassPointToEntitySpace(ent, light->origin, lightorg);
		if (force_scan && !R_GrassLightIntersectsSurfaceBounds(s, lightorg, cull_radius))
			continue;

		if (list->count >= MAX_DLIGHTS)
			break;

		dst = &list->lights[list->count++];
		VectorCopy(lightorg, dst->origin);
		VectorCopy(light->color, dst->color);
		dst->radius = light->radius;
		dst->minlight = light->minlight;
		dst->cull_radius2 = cull_radius * cull_radius;
	}
}

static qboolean R_GrassLightstyleIsAnimated (unsigned int style)
{
	const lightstyle_t *lightstyle;
	int i;
	char first;

	if (style >= MAX_LIGHTSTYLES)
		return false;

	lightstyle = &cl_lightstyle[style];
	if (lightstyle->length <= 1)
		return false;

	first = lightstyle->map[0];
	for (i = 1; i < lightstyle->length; i++)
		if (lightstyle->map[i] != first)
			return true;

	return false;
}

static qboolean R_GrassSurfaceHasAnimatedLightstyles (const msurface_t *s)
{
	int maps;

	if (r_flatlightstyles.value || !r_dynamic.value)
		return false;

	for (maps = 0; maps < MAXLIGHTMAPS && s->styles[maps] != INVALID_LIGHTSTYLE; maps++)
	{
		if (R_GrassLightstyleIsAnimated((unsigned int)s->styles[maps]))
			return true;
	}

	return false;
}

static void R_GrassAddDynamicLights (const grass_dlight_list_t *list, const vec3_t point, vec3_t blocklight)
{
	int i;

	for (i = 0; i < list->count; i++)
	{
		const grass_dlight_t *light;
		vec3_t delta;
		float add, d2;

		light = &list->lights[i];
		VectorSubtract(point, light->origin, delta);
		d2 = DotProduct(delta, delta);
		if (d2 >= light->cull_radius2)
			continue;

		add = light->radius - sqrtf(d2);
		if (add <= light->minlight)
			continue;

		blocklight[0] += add * light->color[0] * 256.0f;
		blocklight[1] += add * light->color[1] * 256.0f;
		blocklight[2] += add * light->color[2] * 256.0f;
	}
}

static void R_GrassLightForPoint (const qmodel_t *model, const msurface_t *s, const grass_dlight_list_t *dlights, const vec3_t point, qboolean cheap, vec3_t light)
{
	int i;
	vec3_t blocklight;

	blocklight[0] = blocklight[1] = blocklight[2] = 0.0f;
	R_GrassAddSurfaceLightmap(model, s, point, !cheap, blocklight);
	if (!cheap && dlights->count)
		R_GrassAddDynamicLights(dlights, point, blocklight);

	for (i = 0; i < 3; i++)
		light[i] = CLAMP(0.05f, blocklight[i] * (1.0f / 32768.0f), 2.0f);
}

static grass_light_cache_entry_t *R_GrassBeginLightCache (void)
{
	r_grass_light_cache_generation++;
	if (!r_grass_light_cache_generation)
	{
		memset(r_grass_light_cache, 0, sizeof(r_grass_light_cache));
		r_grass_light_cache_generation = 1;
	}

	return r_grass_light_cache;
}

static void R_GrassLightForPointCached (const qmodel_t *model, const msurface_t *s, const grass_dlight_list_t *dlights, const vec3_t point, qboolean cheap, grass_light_cache_entry_t *cache, vec3_t light)
{
	int i, qx, qy, qz;
	unsigned int hash;

	if (!cache)
	{
		R_GrassLightForPoint(model, s, dlights, point, cheap, light);
		return;
	}

	qx = (int)floorf(point[0] * (1.0f / GRASS_LIGHT_CACHE_CELL));
	qy = (int)floorf(point[1] * (1.0f / GRASS_LIGHT_CACHE_CELL));
	qz = (int)floorf(point[2] * (1.0f / GRASS_LIGHT_CACHE_CELL));
	hash = R_GrassHashUInt((unsigned int)qx * 73856093U ^ (unsigned int)qy * 19349663U ^ (unsigned int)qz * 83492791U ^ (cheap ? 0x9e3779b9U : 0U));

	for (i = 0; i < GRASS_LIGHT_CACHE_PROBES; i++)
	{
		grass_light_cache_entry_t *entry;

		entry = &cache[(hash + (unsigned int)i) & (GRASS_LIGHT_CACHE_SIZE - 1)];
		if (entry->generation == r_grass_light_cache_generation)
		{
			if (entry->cheap == cheap && entry->cell[0] == qx && entry->cell[1] == qy && entry->cell[2] == qz)
			{
				VectorCopy(entry->light, light);
				return;
			}
			continue;
		}

		entry->generation = r_grass_light_cache_generation;
		entry->cheap = cheap;
		entry->cell[0] = qx;
		entry->cell[1] = qy;
		entry->cell[2] = qz;
		R_GrassLightForPoint(model, s, dlights, point, cheap, entry->light);
		VectorCopy(entry->light, light);
		return;
	}

	R_GrassLightForPoint(model, s, dlights, point, cheap, light);
}

static void R_GrassSurfaceNormal (const msurface_t *s, vec3_t normal)
{
	VectorCopy(s->plane->normal, normal);
	if (s->flags & SURF_PLANEBACK)
		VectorScale(normal, -1.0f, normal);
}

static unsigned int R_GrassTexStringHash (void)
{
	const unsigned char *s;
	unsigned int hash;

	s = (const unsigned char *)(r_grass_tex.string ? r_grass_tex.string : "");
	hash = 2166136261U;
	while (*s)
	{
		hash ^= *s++;
		hash *= 16777619U;
	}

	return hash;
}

static qboolean R_GrassSurfaceCanHaveBlades (const msurface_t *s)
{
	vec3_t normal;

	if (!s || !s->texinfo || !s->texinfo->texture || !s->polys)
		return false;
	if (s->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE | SURF_DRAWFENCE))
		return false;
	if (!R_TextureHasGrass(s->texinfo->texture))
		return false;

	R_GrassSurfaceNormal(s, normal);
	return normal[2] >= 0.35f;
}

static qboolean R_GrassScanModelForBladeSurfaces (qmodel_t *model)
{
	int i, firstsurface, numsurfaces;
	msurface_t *s;

	if (!model || model->type != mod_brush || !model->surfaces)
		return false;

	firstsurface = model->firstmodelsurface;
	numsurfaces = model->nummodelsurfaces;
	if (firstsurface < 0 || firstsurface > model->numsurfaces ||
		numsurfaces <= 0 || numsurfaces > model->numsurfaces - firstsurface)
		return false;

	for (i = 0, s = model->surfaces + firstsurface; i < numsurfaces; i++, s++)
		if (R_GrassSurfaceCanHaveBlades(s))
			return true;

	return false;
}

static qboolean R_GrassRefreshPresenceCache (qmodel_t *model, grass_presence_cache_t *cache)
{
	int i, firstsurface, numsurfaces;
	msurface_t *s;

	if (!cache)
		return R_GrassScanModelForBladeSurfaces(model);

	cache->has_blade_surfaces = false;
	if (cache->surface_blade_flags)
		memset(cache->surface_blade_flags, 0, (size_t)cache->numsurfaces * sizeof(*cache->surface_blade_flags));

	if (!model || model->type != mod_brush || !model->surfaces)
		return false;

	firstsurface = model->firstmodelsurface;
	numsurfaces = model->nummodelsurfaces;
	if (firstsurface < 0 || firstsurface > model->numsurfaces ||
		numsurfaces <= 0 || numsurfaces > model->numsurfaces - firstsurface)
		return false;

	for (i = 0, s = model->surfaces + firstsurface; i < numsurfaces; i++, s++)
	{
		if (!R_GrassSurfaceCanHaveBlades(s))
			continue;
		cache->has_blade_surfaces = true;
		if (cache->surface_blade_flags)
			cache->surface_blade_flags[i] = 1;
	}

	return cache->has_blade_surfaces;
}

static void R_GrassFreePresenceCache (grass_presence_cache_t *cache)
{
	if (!cache)
		return;

	free(cache->surface_blade_flags);
	free(cache->surface_visframes);
	free(cache);
}

static grass_presence_cache_t *R_GrassGetPresenceCache (qmodel_t *model, qboolean create)
{
	grass_presence_cache_t **link, *cache;
	unsigned int texhash;

	if (!model)
		return NULL;

	texhash = R_GrassTexStringHash();
	for (link = &r_grass_presence_caches; (cache = *link); )
	{
		if (cache->model != model)
		{
			link = &cache->next;
			continue;
		}

		if (cache->firstsurface != model->firstmodelsurface ||
			cache->numsurfaces != model->nummodelsurfaces)
		{
			*link = cache->next;
			R_GrassFreePresenceCache(cache);
			continue;
		}

		if (cache->texhash != texhash)
		{
			cache->texhash = texhash;
			R_GrassRefreshPresenceCache(model, cache);
		}
		return cache;
	}

	if (!create)
		return NULL;

	cache = (grass_presence_cache_t *)calloc(1, sizeof(*cache));
	if (!cache)
		return NULL;

	cache->model = model;
	cache->firstsurface = model->firstmodelsurface;
	cache->numsurfaces = model->nummodelsurfaces;
	cache->texhash = texhash;
	if (cache->numsurfaces > 0)
	{
		cache->surface_blade_flags = (byte *)calloc((size_t)cache->numsurfaces, sizeof(*cache->surface_blade_flags));
		cache->surface_visframes = (int *)calloc((size_t)cache->numsurfaces, sizeof(*cache->surface_visframes));
	}
	R_GrassRefreshPresenceCache(model, cache);
	cache->next = r_grass_presence_caches;
	r_grass_presence_caches = cache;
	return cache;
}

static qboolean R_GrassPresenceCacheHasBladeSurfaces (const grass_presence_cache_t *cache)
{
	return cache && cache->has_blade_surfaces;
}

static int R_GrassSurfacePresenceIndex (const grass_presence_cache_t *cache, const qmodel_t *model, const msurface_t *s)
{
	int index;

	if (!cache || !model || !s)
		return -1;

	index = (int)(s - (model->surfaces + cache->firstsurface));
	if (index < 0 || index >= cache->numsurfaces)
		return -1;

	return index;
}

static qboolean R_GrassSurfaceCanHaveBladesCached (const grass_presence_cache_t *cache, qmodel_t *model, const msurface_t *s)
{
	int index;

	index = R_GrassSurfacePresenceIndex(cache, model, s);
	if (index < 0)
		return false;
	if (!cache->surface_blade_flags)
		return R_GrassSurfaceCanHaveBlades(s);

	return cache->surface_blade_flags[index] != 0;
}

#ifndef SDL_THREADS_DISABLED
static void R_GrassMarkSurfaceVisibleCached (grass_presence_cache_t *cache, qmodel_t *model, const msurface_t *s)
{
	int index;

	index = R_GrassSurfacePresenceIndex(cache, model, s);
	if (index < 0 || !cache->surface_visframes)
		return;

	cache->surface_visframes[index] = r_visframecount;
}
#endif

static qboolean R_GrassSurfaceVisibleCached (const grass_presence_cache_t *cache, qmodel_t *model, const msurface_t *s, texchain_t chain, qboolean use_presence_vis)
{
	int index;

	if (chain != chain_world)
		return true;
	if (!use_presence_vis && s->visframe == r_visframecount)
		return true;

	index = R_GrassSurfacePresenceIndex(cache, model, s);
	return index >= 0 && cache->surface_visframes &&
		cache->surface_visframes[index] == r_visframecount;
}

static unsigned int R_GrassSurfaceLightstyleHash (const msurface_t *s)
{
	unsigned int hash;
	int maps;

	hash = 2166136261U;
	for (maps = 0; maps < MAXLIGHTMAPS && s->styles[maps] != INVALID_LIGHTSTYLE; maps++)
	{
		hash ^= (unsigned int)s->styles[maps] + 0x9e3779b9U;
		hash *= 16777619U;
		hash ^= (unsigned int)d_lightstylevalue[s->styles[maps]];
		hash *= 16777619U;
	}

	return hash;
}

static qboolean R_GrassColorsMatch (const vec3_t a, const vec3_t b)
{
	return fabsf(a[0] - b[0]) < 0.0001f &&
		fabsf(a[1] - b[1]) < 0.0001f &&
		fabsf(a[2] - b[2]) < 0.0001f;
}

static void R_GrassLODParams (float dist, float lod, grass_lod_params_t *params)
{
	float nearclip;

	params->dist = dist;
	params->dist2 = dist * dist;
	params->lod = lod;
	params->use_dist = dist > 0.0f;
	if (dist <= 0.0f)
	{
		params->nearclip2 = 0.0f;
		params->invfade = 0.0f;
		return;
	}

	nearclip = dist * 0.25f;
	params->nearclip2 = nearclip * nearclip;
	params->invfade = 1.0f / (dist - nearclip);
}

static float R_GrassDensityScaleForDelta (const vec3_t delta, const grass_lod_params_t *params)
{
	float d2, d, fade;

	if (!params->use_dist)
		return 1.0f;

	d2 = DotProduct(delta, delta);
	if (d2 >= params->dist2)
		return 0.0f;

	if (params->lod <= 0.0f)
		return 1.0f;

	if (d2 <= params->nearclip2)
		return 1.0f;

	d = sqrtf(d2);
	fade = (params->dist - d) * params->invfade;
	fade = CLAMP(0.0f, fade, 1.0f);
	fade = fade * fade * (3.0f - 2.0f * fade);
	fade *= fade;
	if (params->lod > 1.0f)
		fade = powf(fade, params->lod);

	return fade;
}

static float R_GrassSurfaceDensityScale (const msurface_t *s, const vec3_t vieworg, const grass_lod_params_t *lodparams)
{
	int i;
	vec3_t closest, delta;

	for (i = 0; i < 3; i++)
		closest[i] = CLAMP(s->mins[i], vieworg[i], s->maxs[i]);

	VectorSubtract(closest, vieworg, delta);

	return R_GrassDensityScaleForDelta(delta, lodparams);
}

static float R_GrassPointDensityScale (const vec3_t point, const vec3_t vieworg, const grass_lod_params_t *lodparams)
{
	vec3_t delta;

	VectorSubtract(point, vieworg, delta);

	return R_GrassDensityScaleForDelta(delta, lodparams);
}

static qboolean R_GrassSurfaceVolumeCulled (const msurface_t *s, const entity_t *ent, const vec3_t normal, float maxheight, float movement)
{
	int i, x, y, z;
	float sidepad;
	vec3_t mins, maxs, worldmins, worldmaxs;

	sidepad = 2.0f + maxheight * (0.12f + 0.18f * movement);
	VectorCopy(s->mins, mins);
	VectorCopy(s->maxs, maxs);

	for (i = 0; i < 3; i++)
	{
		mins[i] -= sidepad;
		maxs[i] += sidepad;
		if (normal[i] > 0.0f)
			maxs[i] += maxheight * normal[i];
		else
			mins[i] += maxheight * normal[i];
	}

	if (!ent || (!ent->origin[0] && !ent->origin[1] && !ent->origin[2] &&
		!ent->angles[0] && !ent->angles[1] && !ent->angles[2]))
		return R_CullBox(mins, maxs);

	worldmins[0] = worldmins[1] = worldmins[2] = 999999.0f;
	worldmaxs[0] = worldmaxs[1] = worldmaxs[2] = -999999.0f;
	for (x = 0; x < 2; x++)
	for (y = 0; y < 2; y++)
	for (z = 0; z < 2; z++)
	{
		vec3_t corner, worldcorner;

		corner[0] = x ? maxs[0] : mins[0];
		corner[1] = y ? maxs[1] : mins[1];
		corner[2] = z ? maxs[2] : mins[2];
		R_GrassPointFromEntitySpace(ent, corner, worldcorner);
		for (i = 0; i < 3; i++)
		{
			worldmins[i] = q_min(worldmins[i], worldcorner[i]);
			worldmaxs[i] = q_max(worldmaxs[i], worldcorner[i]);
		}
	}

	return R_CullBox(worldmins, worldmaxs);
}

static void R_GrassBasisForSurface (const msurface_t *s, const vec3_t normal, vec3_t tangent, vec3_t bitangent)
{
	if (fabsf(normal[2]) < 0.98f)
	{
		tangent[0] = -normal[1];
		tangent[1] = normal[0];
		tangent[2] = 0.0f;
	}
	else
	{
		tangent[0] = 1.0f;
		tangent[1] = 0.0f;
		tangent[2] = 0.0f;
	}
	if (VectorNormalize(tangent) == 0.0f)
	{
		tangent[0] = 1.0f;
		tangent[1] = 0.0f;
		tangent[2] = 0.0f;
	}

	CrossProduct(normal, tangent, bitangent);
	VectorNormalize(bitangent);
}

static void R_GrassShaderSideForPoint (const vec3_t vieworg, const vec3_t point, const vec3_t normal, const vec3_t tangent, vec3_t side)
{
	float d;
	vec3_t viewdir;

	VectorSubtract(vieworg, point, viewdir);

	d = DotProduct(viewdir, normal);
	VectorMA(viewdir, -d, normal, viewdir);
	if (VectorNormalize(viewdir) == 0.0f)
	{
		VectorCopy(tangent, side);
		return;
	}

	CrossProduct(normal, viewdir, side);
	if (VectorNormalize(side) == 0.0f)
		VectorCopy(tangent, side);
}

static unsigned int R_GrassHashCell (int x, int y, unsigned int salt)
{
	return R_GrassHashUInt((unsigned int)x * 73856093U ^ (unsigned int)y * 19349663U ^ salt * 83492791U);
}

static int R_GrassFloorDiv (int value, int divisor)
{
	if (value >= 0)
		return value / divisor;
	return -((-value + divisor - 1) / divisor);
}

static int R_GrassHashOffset (unsigned int seed, int size)
{
	int offset;

	offset = (int)(R_GrassHashFloat(seed) * (float)size);
	if (offset >= size)
		offset = size - 1;
	return offset;
}

static int R_GrassCellStepForScale (float scale)
{
	if (scale <= 0.015625f)
		return 8;
	if (scale <= 0.0625f)
		return 4;
	if (scale <= 0.25f)
		return 2;
	return 1;
}

static float R_GrassHashBell (unsigned int seed)
{
	float h;

	h = R_GrassHashFloat(seed + 11U);
	h += R_GrassHashFloat(seed + 47U);
	h += R_GrassHashFloat(seed + 109U);
	return h * (1.0f / 3.0f);
}

static float R_GrassWeatherNoise (float x, float y, unsigned int salt)
{
	int ix, iy;
	float fx, fy, n00, n10, n01, n11;

	ix = (int)floorf(x);
	iy = (int)floorf(y);
	fx = x - ix;
	fy = y - iy;
	fx = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
	fy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

	n00 = R_GrassHashFloat(R_GrassHashCell(ix, iy, salt));
	n10 = R_GrassHashFloat(R_GrassHashCell(ix + 1, iy, salt));
	n01 = R_GrassHashFloat(R_GrassHashCell(ix, iy + 1, salt));
	n11 = R_GrassHashFloat(R_GrassHashCell(ix + 1, iy + 1, salt));

	return (n00 + (n10 - n00) * fx) * (1.0f - fy) + (n01 + (n11 - n01) * fx) * fy;
}

static void R_GrassWindBend (const vec3_t pos, float height, float movement, unsigned int seed, vec3_t bend)
{
	float time, phase, weather, gust, pulse, eddy, angle, amount;
	vec3_t dir, swaydir;

	if (movement <= 0.0f)
	{
		bend[0] = bend[1] = bend[2] = 0.0f;
		return;
	}

	time = R_GrassAnimTime();
	phase = R_GrassHashFloat(seed + 73U) * M_PI * 2.0f;
	weather = R_GrassWeatherNoise(pos[0] * 0.0016f + time * 0.004f, pos[1] * 0.0016f - time * 0.003f, 17U);
	gust = R_GrassWeatherNoise(pos[0] * 0.0065f + time * 0.018f, pos[1] * 0.0065f - time * 0.011f, 53U);
	eddy = R_GrassWeatherNoise(pos[0] * 0.014f - time * 0.010f, pos[1] * 0.014f + time * 0.007f, 97U);
	pulse = 0.5f + 0.5f * sinf(time * (0.18f + weather * 0.16f) + phase + gust * M_PI * 2.0f);

	angle = weather * M_PI * 2.0f + (eddy - 0.5f) * 1.15f;
	dir[0] = cosf(angle);
	dir[1] = sinf(angle);
	dir[2] = 0.0f;

	amount = height * movement * (0.035f + 0.13f * gust * (0.45f + 0.55f * pulse));
	VectorScale(dir, amount, bend);
	swaydir[0] = -dir[1];
	swaydir[1] = dir[0];
	swaydir[2] = 0.0f;
	VectorMA(bend, height * movement * 0.045f * (eddy - 0.5f), swaydir, bend);
	bend[2] = 0.0f;
}

static qboolean R_GrassPointInTriangle2D (float pu, float pv, float au, float av, float bu, float bv, float cu, float cv, float invdenom, float *ba, float *bb, float *bc)
{
	*ba = ((bv - cv) * (pu - cu) + (cu - bu) * (pv - cv)) * invdenom;
	*bb = ((cv - av) * (pu - cu) + (au - cu) * (pv - cv)) * invdenom;
	*bc = 1.0f - *ba - *bb;

	return *ba >= 0.0f && *bb >= 0.0f && *bc >= 0.0f;
}

static void R_GrassClearSurfaceShaderVBO (grass_surface_cache_t *cache, int index)
{
	if (cache->shader_vbo[index] && gl_vbo_able && GL_DeleteBuffersFunc)
	{
		GL_BindBuffer(GL_ARRAY_BUFFER, 0);
		GL_DeleteBuffersFunc(1, &cache->shader_vbo[index]);
	}
	cache->shader_vbo[index] = 0;
	cache->shader_vertex_count[index] = 0;
	cache->shader_baseheight[index] = 0.0f;
	cache->shader_basecolor[index][0] = cache->shader_basecolor[index][1] = cache->shader_basecolor[index][2] = 0.0f;
	cache->shader_tipcolor[index][0] = cache->shader_tipcolor[index][1] = cache->shader_tipcolor[index][2] = 0.0f;
	cache->shader_lightstyle_hash[index] = 0;
	cache->shader_built[index] = false;
	cache->shader_failed[index] = false;
}

static void R_GrassClearSurfaceShaderVBOs (grass_surface_cache_t *cache)
{
	int i;

	for (i = 0; i < GRASS_SHADER_LOD_LEVELS; i++)
		R_GrassClearSurfaceShaderVBO(cache, i);
}

static void R_GrassClearSurfaceCache (grass_surface_cache_t *cache)
{
	R_GrassClearSurfaceShaderVBOs(cache);
	free(cache->blades);
	cache->blades = NULL;
	cache->count = 0;
	cache->capacity = 0;
	cache->built = false;
	cache->failed = false;
	cache->surface = NULL;
}

static void R_GrassClearModelCache (grass_model_cache_t *cache)
{
	int i;

	if (!cache->surfaces)
		return;

	for (i = 0; i < cache->numsurfaces; i++)
		R_GrassClearSurfaceCache(&cache->surfaces[i]);
}

static void R_GrassClearBrushSubmodelBlockers (void)
{
	free(r_grass_brush_blocker_submodels);
	free(r_grass_brush_blockers);
	r_grass_brush_blocker_submodels = NULL;
	r_grass_brush_blocker_submodel_count = 0;
	r_grass_brush_blockers = NULL;
	r_grass_brush_blocker_count = 0;
	r_grass_brush_blocker_worldmodel = NULL;
}

static void R_GrassFreePresenceCaches (void)
{
	grass_presence_cache_t *cache, *next;

	for (cache = r_grass_presence_caches; cache; cache = next)
	{
		next = cache->next;
		R_GrassFreePresenceCache(cache);
	}

	r_grass_presence_caches = NULL;
}

static void R_GrassRemovePresenceCache (qmodel_t *mod)
{
	grass_presence_cache_t **link, *cache;

	for (link = &r_grass_presence_caches; (cache = *link); )
	{
		if (cache->model == mod)
		{
			*link = cache->next;
			R_GrassFreePresenceCache(cache);
		}
		else
			link = &cache->next;
	}
}

static void R_GrassFreeAllModelCaches (void)
{
	grass_model_cache_t *cache, *next;

	for (cache = r_grass_model_caches; cache; cache = next)
	{
		next = cache->next;
		R_GrassClearModelCache(cache);
		free(cache->surfaces);
		free(cache);
	}

	r_grass_model_caches = NULL;
	R_GrassFreePresenceCaches();
	r_grass_cache_worldmodel = cl.worldmodel;
	R_GrassClearBrushSubmodelBlockers();
}

void R_GrassCache_Cleanup (qmodel_t *mod)
{
	grass_model_cache_t **link, *cache;

	if (!mod)
	{
		R_GrassFreeAllModelCaches();
		return;
	}

	R_GrassRemovePresenceCache(mod);

	for (link = &r_grass_model_caches; (cache = *link); )
	{
		if (cache->model == mod)
		{
			*link = cache->next;
			R_GrassClearModelCache(cache);
			free(cache->surfaces);
			free(cache);
		}
		else
			link = &cache->next;
	}

	if (r_grass_cache_worldmodel == mod)
		r_grass_cache_worldmodel = NULL;
	if (r_grass_brush_blocker_worldmodel == mod)
		R_GrassClearBrushSubmodelBlockers();
}

static void R_GrassClearAllShaderVBOs (void)
{
	grass_model_cache_t *cache;
	int i;

	for (cache = r_grass_model_caches; cache; cache = cache->next)
	{
		if (!cache->surfaces)
			continue;
		for (i = 0; i < cache->numsurfaces; i++)
			R_GrassClearSurfaceShaderVBOs(&cache->surfaces[i]);
	}
}

void R_GrassShutdown (void)
{
	R_GrassFreeAllModelCaches();
	r_grass_cache_worldmodel = NULL;
	R_GrassClearBrushSubmodelBlockers();
	R_GrassShutdownGL();
	free(r_grass_vertex_batch);
	r_grass_vertex_batch = NULL;
	r_grass_vertex_count = 0;
}

void R_GrassShutdownGL (void)
{
	R_GrassClearAllShaderVBOs();
	if (r_grass_vertex_vbo && gl_vbo_able && GL_DeleteBuffersFunc)
	{
		GL_BindBuffer(GL_ARRAY_BUFFER, 0);
		GL_DeleteBuffersFunc(1, &r_grass_vertex_vbo);
	}
	r_grass_vertex_vbo = 0;
}

static void R_GrassResetCachesIfWorldChanged (void)
{
	if (r_grass_cache_worldmodel != cl.worldmodel)
		R_GrassFreeAllModelCaches();
}

static qboolean R_GrassAppendCachedBlade (grass_surface_cache_t *cache, const vec3_t pos, int cell_x, int cell_y, unsigned int cellseed)
{
	static qboolean warned;
	grass_cached_blade_t *blade;
	unsigned int seed;

	if (cache->count >= cache->capacity)
	{
		int newcapacity;
		void *newblades;

		newcapacity = cache->capacity ? cache->capacity * 2 : 256;
		if (newcapacity < cache->count + 1)
			newcapacity = cache->count + 1;

		newblades = realloc(cache->blades, sizeof(*cache->blades) * (size_t)newcapacity);
		if (!newblades)
		{
			if (!warned)
			{
				warned = true;
				Con_Printf("R_DrawGrassBlades: failed to allocate grass placement cache\n");
			}
			return false;
		}

		cache->blades = (grass_cached_blade_t *)newblades;
		cache->capacity = newcapacity;
	}

	seed = R_GrassHashUInt(cellseed);
	blade = &cache->blades[cache->count++];
	VectorCopy(pos, blade->pos);
	blade->seed = seed;
	blade->bladebits = R_GrassHashUInt(seed + 61U);
	blade->colorbits = R_GrassHashUInt(seed + 149U);
	blade->amountbits = R_GrassHashUInt(seed + 101U);
	blade->lodbits = R_GrassHashUInt(cellseed + 43U);
	blade->heightscale = 0.58f + R_GrassHashBell(seed) * 0.86f;
	blade->cell_x = cell_x;
	blade->cell_y = cell_y;
	return true;
}

static qboolean R_GrassBrushClassBlocksWorldGrass (const char *classname)
{
	if (!classname || !classname[0])
		return false;
	if (!q_strncasecmp(classname, "func_wall", 9) ||
		!q_strncasecmp(classname, "func_illusionary", 16) ||
		!q_strncasecmp(classname, "func_detail", 11))
		return false;

	return q_strcasestr(classname, "door") != NULL ||
		q_strcasestr(classname, "plat") != NULL ||
		q_strcasestr(classname, "train") != NULL ||
		q_strcasestr(classname, "button") != NULL ||
		q_strcasestr(classname, "lift") != NULL ||
		q_strcasestr(classname, "elev") != NULL;
}

static void R_GrassMarkBrushSubmodelBlocker (const char *modelname)
{
	int submodel;

	if (!modelname || modelname[0] != '*' || !r_grass_brush_blocker_submodels)
		return;

	submodel = Q_atoi(modelname + 1);
	if (submodel <= 0 || submodel >= r_grass_brush_blocker_submodel_count)
		return;

	r_grass_brush_blocker_submodels[submodel >> 3] |= (byte)(1u << (submodel & 7));
}

static qboolean R_GrassBrushSubmodelMarked (const qmodel_t *m)
{
	unsigned int submodel;

	if (!m || m->submodelof != cl.worldmodel || !r_grass_brush_blocker_submodels)
		return false;

	submodel = m->submodelidx;
	if (submodel >= (unsigned int)r_grass_brush_blocker_submodel_count)
		return false;
	return (r_grass_brush_blocker_submodels[submodel >> 3] & (1u << (submodel & 7))) != 0;
}

static void R_GrassBuildBrushSubmodelBlockers (void)
{
	static qboolean warned_alloc;
	const char *data;
	int bytes, i, count;
	qboolean parse_failed;

	R_GrassClearBrushSubmodelBlockers();
	r_grass_brush_blocker_worldmodel = cl.worldmodel;
	if (!cl.worldmodel || cl.worldmodel->numsubmodels <= 1 || !cl.worldmodel->entities)
		return;

	r_grass_brush_blocker_submodel_count = cl.worldmodel->numsubmodels;
	bytes = (r_grass_brush_blocker_submodel_count + 7) >> 3;
	r_grass_brush_blocker_submodels = (byte *)calloc(1, (size_t)bytes);
	if (!r_grass_brush_blocker_submodels)
	{
		if (!warned_alloc)
		{
			warned_alloc = true;
			Con_Printf("R_DrawGrassBlades: failed to allocate grass brush blocker map\n");
		}
		return;
	}

	data = cl.worldmodel->entities;
	parse_failed = false;
	while ((data = COM_Parse(data)) != NULL)
	{
		char classname[128], modelname[64];

		if (com_token[0] != '{')
			break;

		classname[0] = 0;
		modelname[0] = 0;
		while (1)
		{
			char key[128];

			data = COM_Parse(data);
			if (!data)
			{
				parse_failed = true;
				break;
			}
			if (com_token[0] == '}')
				break;

			q_strlcpy(key, com_token, sizeof(key));
			data = COM_ParseEx(data, CPE_ALLOWTRUNC);
			if (!data)
			{
				parse_failed = true;
				break;
			}

			if (!q_strcasecmp(key, "classname"))
				q_strlcpy(classname, com_token, sizeof(classname));
			else if (!q_strcasecmp(key, "model"))
				q_strlcpy(modelname, com_token, sizeof(modelname));
		}

		if (parse_failed)
			break;
		if (R_GrassBrushClassBlocksWorldGrass(classname))
			R_GrassMarkBrushSubmodelBlocker(modelname);
	}

	r_grass_brush_blockers = (grass_brush_blocker_t *)calloc((size_t)r_grass_brush_blocker_submodel_count, sizeof(*r_grass_brush_blockers));
	if (!r_grass_brush_blockers)
	{
		if (!warned_alloc)
		{
			warned_alloc = true;
			Con_Printf("R_DrawGrassBlades: failed to allocate grass brush blocker bounds\n");
		}
		return;
	}

	count = cl.model_count < MAX_MODELS ? cl.model_count : MAX_MODELS;
	for (i = 1; i < count; i++)
	{
		qmodel_t *m = cl.model_precache[i];
		grass_brush_blocker_t *blocker;

		if (!m || m->type != mod_brush || m->name[0] != '*' || !R_GrassBrushSubmodelMarked(m))
			continue;
		if (r_grass_brush_blocker_count >= r_grass_brush_blocker_submodel_count)
			break;

		blocker = &r_grass_brush_blockers[r_grass_brush_blocker_count++];
		VectorCopy(m->mins, blocker->mins);
		VectorCopy(m->maxs, blocker->maxs);
	}
}

static qboolean R_GrassBrushSubmodelBlocksWorldGrass (const qmodel_t *m)
{
	if (r_grass_brush_blocker_worldmodel != cl.worldmodel)
		R_GrassBuildBrushSubmodelBlockers();

	return R_GrassBrushSubmodelMarked(m);
}

static qboolean R_GrassBrushEntityBlocksGrass (const entity_t *ent)
{
	return ent && ent->model && ent->model->type == mod_brush &&
		ent->model->name[0] == '*' && R_GrassBrushSubmodelBlocksWorldGrass(ent->model);
}

static qboolean R_GrassEntityAllowsGrass (const entity_t *ent)
{
	if (!ent)
		return true;
	if (ENTALPHA_DECODE(ent->alpha) < 1.0f)
		return false;
	if (ent->effects & EF_ADDITIVE)
		return false;
	if (ent->netstate.scale != ENTSCALE_DEFAULT)
		return false;
	if (ent->angles[0] || ent->angles[1] || ent->angles[2])
		return false;
	return !R_GrassBrushEntityBlocksGrass(ent);
}

static qboolean R_GrassPointUnderBrushSubmodel (const vec3_t pos)
{
	int i;
	const float pad = 1.0f;

	if (r_grass_brush_blocker_worldmodel != cl.worldmodel)
		R_GrassBuildBrushSubmodelBlockers();

	for (i = 0; i < r_grass_brush_blocker_count; i++)
	{
		const grass_brush_blocker_t *blocker = &r_grass_brush_blockers[i];
		if (pos[0] < blocker->mins[0] - pad || pos[0] > blocker->maxs[0] + pad)
			continue;
		if (pos[1] < blocker->mins[1] - pad || pos[1] > blocker->maxs[1] + pad)
			continue;
		return true;
	}
	return false;
}

static qboolean R_GrassBuildSurfaceCache (grass_surface_cache_t *cache, const qmodel_t *model, const msurface_t *s, float cellsize)
{
	static qboolean warned_cell_scan;
	static qboolean warned_blade_cap;
	int tri;
	glpoly_t *p;
	vec3_t normal;

	R_GrassClearSurfaceCache(cache);
	cache->surface = s;
	cache->built = true;

	if (cellsize <= 0.0f || !s->polys)
		return true;

	R_GrassSurfaceNormal(s, normal);
	if (normal[2] < 0.35f)
		return true;

	p = s->polys;
	for (tri = 2; tri < p->numverts; tri++)
	{
		float *va, *vb, *vc;
		float min_x, max_x, min_y, max_y, denom, invdenom;
		double cells;
		int cell_x, cell_y, first_x, last_x, first_y, last_y;

		va = p->verts[0];
		vb = p->verts[tri - 1];
		vc = p->verts[tri];
		if (!isfinite(va[0]) || !isfinite(va[1]) || !isfinite(va[2]) ||
			!isfinite(vb[0]) || !isfinite(vb[1]) || !isfinite(vb[2]) ||
			!isfinite(vc[0]) || !isfinite(vc[1]) || !isfinite(vc[2]))
			continue;
		denom = (vb[1] - vc[1]) * (va[0] - vc[0]) + (vc[0] - vb[0]) * (va[1] - vc[1]);
		if (!isfinite(denom) || fabsf(denom) < 0.001f)
			continue;
		invdenom = 1.0f / denom;

		min_x = q_min(va[0], q_min(vb[0], vc[0]));
		max_x = q_max(va[0], q_max(vb[0], vc[0]));
		min_y = q_min(va[1], q_min(vb[1], vc[1]));
		max_y = q_max(va[1], q_max(vb[1], vc[1]));
		if (!isfinite(min_x) || !isfinite(max_x) || !isfinite(min_y) || !isfinite(max_y))
			continue;

		first_x = (int)floorf(min_x / cellsize);
		last_x = (int)floorf(max_x / cellsize);
		first_y = (int)floorf(min_y / cellsize);
		last_y = (int)floorf(max_y / cellsize);

		cells = ((double)last_x - (double)first_x + 1.0) * ((double)last_y - (double)first_y + 1.0);
		if (cells > GRASS_SURFACE_CELL_SCAN_MAX)
		{
			if (!warned_cell_scan)
			{
				warned_cell_scan = true;
				Con_Printf("R_DrawGrassBlades: skipping oversized grass surface scan\n");
			}
			continue;
		}

		for (cell_y = first_y; cell_y <= last_y; cell_y++)
		for (cell_x = first_x; cell_x <= last_x; cell_x++)
		{
			unsigned int cellseed;
			float ba, bb, bc, px, py;
			vec3_t pos;

			if (cache->count >= GRASS_SURFACE_BLADE_MAX)
			{
				if (!warned_blade_cap)
				{
					warned_blade_cap = true;
					Con_Printf("R_DrawGrassBlades: capped grass placement cache for one surface\n");
				}
				return true;
			}

			cellseed = R_GrassHashCell(cell_x, cell_y, 0U);
			px = ((float)cell_x + R_GrassHashFloat(cellseed + 17U)) * cellsize;
			py = ((float)cell_y + R_GrassHashFloat(cellseed + 31U)) * cellsize;
			if (!R_GrassPointInTriangle2D(px, py, va[0], va[1], vb[0], vb[1], vc[0], vc[1], invdenom, &ba, &bb, &bc))
				continue;

			pos[0] = va[0] * ba + vb[0] * bb + vc[0] * bc;
			pos[1] = va[1] * ba + vb[1] * bb + vc[1] * bc;
			pos[2] = va[2] * ba + vb[2] * bb + vc[2] * bc;
			if (model == cl.worldmodel && R_GrassPointUnderBrushSubmodel(pos))
				continue;
			if (!R_GrassAppendCachedBlade(cache, pos, cell_x, cell_y, cellseed))
			{
				R_GrassClearSurfaceCache(cache);
				cache->surface = s;
				cache->built = true;
				cache->failed = true;
				return false;
			}
		}
	}

	return true;
}

static grass_model_cache_t *R_GrassGetModelCache (qmodel_t *model, float density, float cellsize)
{
	grass_model_cache_t *cache;

	R_GrassResetCachesIfWorldChanged();

	for (cache = r_grass_model_caches; cache; cache = cache->next)
	{
		if (cache->model == model)
			break;
	}

	if (!cache)
	{
		cache = (grass_model_cache_t *)calloc(1, sizeof(*cache));
		if (!cache)
			return NULL;

		cache->model = model;
		cache->next = r_grass_model_caches;
		r_grass_model_caches = cache;
	}

	if (!cache->surfaces || cache->firstsurface != model->firstmodelsurface || cache->numsurfaces != model->nummodelsurfaces || fabsf(cache->density - density) > 0.001f)
	{
		R_GrassClearModelCache(cache);
		free(cache->surfaces);

		cache->firstsurface = model->firstmodelsurface;
		cache->numsurfaces = model->nummodelsurfaces;
		cache->density = density;
		cache->cellsize = cellsize;
		cache->surfaces = (grass_surface_cache_t *)calloc((size_t)cache->numsurfaces, sizeof(*cache->surfaces));
		if (!cache->surfaces)
		{
			cache->numsurfaces = 0;
			return NULL;
		}
	}

	return cache;
}

static grass_surface_cache_t *R_GrassGetSurfaceCache (grass_model_cache_t *modelcache, const msurface_t *s)
{
	int index;
	grass_surface_cache_t *cache;

	index = (int)(s - (modelcache->model->surfaces + modelcache->firstsurface));
	if (index < 0 || index >= modelcache->numsurfaces)
		return NULL;

	cache = &modelcache->surfaces[index];
	if (!cache->built || cache->surface != s)
		R_GrassBuildSurfaceCache(cache, modelcache->model, s, modelcache->cellsize);

	if (cache->failed)
		return NULL;
	return cache;
}

static qboolean R_GrassCachedBladeSelectedForStep (const grass_cached_blade_t *blade, int cellstep)
{
	unsigned int blockseed;
	int block_x, block_y, cell_x, cell_y;

	if (cellstep <= 1)
		return true;

	block_x = R_GrassFloorDiv(blade->cell_x, cellstep);
	block_y = R_GrassFloorDiv(blade->cell_y, cellstep);
	blockseed = R_GrassHashCell(block_x, block_y, (unsigned int)cellstep + 211U);
	cell_x = block_x * cellstep + R_GrassHashOffset(blockseed + 3U, cellstep);
	cell_y = block_y * cellstep + R_GrassHashOffset(blockseed + 7U, cellstep);

	return blade->cell_x == cell_x && blade->cell_y == cell_y;
}

static qboolean R_GrassEnsureVertexBatch (void)
{
	static qboolean warned;

	if (!r_grass_vertex_batch)
	{
		r_grass_vertex_batch = (grass_vertex_t *)malloc(sizeof(*r_grass_vertex_batch) * GRASS_VERTEX_BATCH_MAX);
		if (!r_grass_vertex_batch)
		{
			if (!warned)
			{
				warned = true;
				Con_Printf("R_DrawGrassBlades: failed to allocate grass vertex batch\n");
			}
			return false;
		}
	}

	if (!r_grass_vertex_vbo && gl_vbo_able && GL_GenBuffersFunc)
		GL_GenBuffersFunc(1, &r_grass_vertex_vbo);

	return true;
}

static void R_GrassFlushVertexBatch (void)
{
	static qboolean warned_incomplete;
	int drawcount;
	qboolean usevbo;

	if (r_grass_vertex_count <= 0)
		return;

	usevbo = R_GrassUseVertexVBO();
	drawcount = r_grass_vertex_count - (r_grass_vertex_count % 3);
	if (drawcount > 0)
	{
		if (usevbo)
		{
			GL_BindBuffer(GL_ARRAY_BUFFER, r_grass_vertex_vbo);
			GL_BufferDataFunc(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(*r_grass_vertex_batch) * (size_t)drawcount), r_grass_vertex_batch, GL_STREAM_DRAW);
		}
		glDrawArrays(GL_TRIANGLES, 0, drawcount);
	}
	if (drawcount != r_grass_vertex_count && !warned_incomplete)
	{
		warned_incomplete = true;
		Con_DPrintf("R_DrawGrassBlades: dropped incomplete grass vertex batch\n");
	}
	r_grass_vertex_count = 0;
}

static void R_GrassBeginVertexBatch (void)
{
	qboolean usevbo;

	r_grass_vertex_count = 0;

	usevbo = R_GrassUseVertexVBO();
	GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	if (GL_ClientActiveTextureFunc)
		GL_ClientActiveTextureFunc(GL_TEXTURE0);

	if (usevbo)
		GL_BindBuffer(GL_ARRAY_BUFFER, r_grass_vertex_vbo);
	else
		GL_BindBuffer(GL_ARRAY_BUFFER, 0);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	if (usevbo)
	{
		glVertexPointer(3, GL_FLOAT, sizeof(*r_grass_vertex_batch), (const GLvoid *)offsetof(grass_vertex_t, vertex));
		glColorPointer(4, GL_FLOAT, sizeof(*r_grass_vertex_batch), (const GLvoid *)offsetof(grass_vertex_t, color));
		glTexCoordPointer(4, GL_FLOAT, sizeof(*r_grass_vertex_batch), (const GLvoid *)offsetof(grass_vertex_t, texcoord));
	}
	else
	{
		glVertexPointer(3, GL_FLOAT, sizeof(*r_grass_vertex_batch), r_grass_vertex_batch[0].vertex);
		glColorPointer(4, GL_FLOAT, sizeof(*r_grass_vertex_batch), r_grass_vertex_batch[0].color);
		glTexCoordPointer(4, GL_FLOAT, sizeof(*r_grass_vertex_batch), r_grass_vertex_batch[0].texcoord);
	}
}

static void R_GrassEndVertexBatch (void)
{
	R_GrassFlushVertexBatch();

	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	GL_BindBuffer(GL_ARRAY_BUFFER, 0);
}

static void R_GrassAddVertex (const vec3_t vertex, const vec3_t color, float s, float t, float seed, float curl)
{
	grass_vertex_t *out;

	if (r_grass_vertex_count >= GRASS_VERTEX_BATCH_MAX)
		R_GrassFlushVertexBatch();
	if (r_grass_vertex_count >= GRASS_VERTEX_BATCH_MAX)
		return;

	out = &r_grass_vertex_batch[r_grass_vertex_count++];
	out->vertex[0] = vertex[0];
	out->vertex[1] = vertex[1];
	out->vertex[2] = vertex[2];
	out->color[0] = color[0];
	out->color[1] = color[1];
	out->color[2] = color[2];
	out->color[3] = 1.0f;
	out->texcoord[0] = s;
	out->texcoord[1] = t;
	out->texcoord[2] = seed;
	out->texcoord[3] = curl;
}

static void R_DrawGrassBladeTri (const vec3_t base, const vec3_t normal, const vec3_t side, const vec3_t bend, float height, float width, float shade, float seed, float curl, const vec3_t basecolor, const vec3_t tipcolor)
{
	vec3_t left, right, tip;
	vec3_t shadedbase, shadedtip;

	if ((r_grass_vertex_count % 3) != 0 || r_grass_vertex_count + 3 > GRASS_VERTEX_BATCH_MAX)
		R_GrassFlushVertexBatch();

	VectorMA(base, width, side, left);
	VectorMA(base, -width, side, right);
	VectorMA(base, height, normal, tip);
	if (curl > 0.0f)
	{
		VectorMA(tip, height * curl * 0.16f, side, tip);
		VectorMA(tip, -height * curl * 0.08f, normal, tip);
	}
	VectorAdd(tip, bend, tip);

	VectorScale(basecolor, shade, shadedbase);
	VectorScale(tipcolor, shade, shadedtip);
	R_GrassAddVertex(left, shadedbase, -0.055f, -1.0f, seed, curl);
	R_GrassAddVertex(right, shadedbase, 0.055f, -1.0f, seed, curl);
	R_GrassAddVertex(tip, shadedtip, 0.0f, 1.0f, seed, curl);
}

static int R_GrassShaderLODIndexForStep (int cellstep)
{
	if (cellstep >= 8)
		return 3;
	if (cellstep >= 4)
		return 2;
	if (cellstep >= 2)
		return 1;
	return 0;
}

static void R_GrassStoreShaderVBOKey (grass_surface_cache_t *cache, int index, float baseheight, const vec3_t basecolor, const vec3_t tipcolor, unsigned int lightstyle_hash)
{
	cache->shader_baseheight[index] = baseheight;
	VectorCopy(basecolor, cache->shader_basecolor[index]);
	VectorCopy(tipcolor, cache->shader_tipcolor[index]);
	cache->shader_lightstyle_hash[index] = lightstyle_hash;
}

static qboolean R_GrassShaderVBOKeyMatches (const grass_surface_cache_t *cache, int index, float baseheight, const vec3_t basecolor, const vec3_t tipcolor, unsigned int lightstyle_hash)
{
	return fabsf(cache->shader_baseheight[index] - baseheight) < 0.001f &&
		cache->shader_lightstyle_hash[index] == lightstyle_hash &&
		R_GrassColorsMatch(cache->shader_basecolor[index], basecolor) &&
		R_GrassColorsMatch(cache->shader_tipcolor[index], tipcolor);
}

static void R_GrassEmitShaderVertex (grass_shader_vertex_t *out, const vec3_t base, const vec3_t color, float s, float t, float seed, float curl, float height, float width, float lodrand)
{
	VectorCopy(base, out->base);
	out->color[0] = color[0];
	out->color[1] = color[1];
	out->color[2] = color[2];
	out->color[3] = 1.0f;
	out->bladecoord[0] = s;
	out->bladecoord[1] = t;
	out->bladecoord[2] = seed;
	out->bladecoord[3] = curl;
	out->geom[0] = height;
	out->geom[1] = width;
	out->geom[2] = lodrand;
	out->geom[3] = 0.0f;
}

static qboolean R_GrassEnsureSurfaceShaderVBO (qmodel_t *model, const msurface_t *s, grass_surface_cache_t *cache, int cellstep, float baseheight, const vec3_t basecolor, const vec3_t tipcolor)
{
	int i, colorindex, lodindex, vertex_count;
	size_t maxverts;
	unsigned int lightstyle_hash;
	vec3_t normal;
	grass_shader_vertex_t *vertices;
	grass_dlight_list_t nodlights;
	grass_light_cache_entry_t *lightcacheptr;

	if (!R_GrassUseStaticShaderVBO() || !cache || cache->count <= 0)
		return false;

	lodindex = R_GrassShaderLODIndexForStep(cellstep);
	lightstyle_hash = R_GrassSurfaceLightstyleHash(s);
	if (cache->shader_built[lodindex] &&
		R_GrassShaderVBOKeyMatches(cache, lodindex, baseheight, basecolor, tipcolor, lightstyle_hash))
		return true;
	if (cache->shader_failed[lodindex] &&
		R_GrassShaderVBOKeyMatches(cache, lodindex, baseheight, basecolor, tipcolor, lightstyle_hash))
		return false;

	R_GrassClearSurfaceShaderVBO(cache, lodindex);
	R_GrassStoreShaderVBOKey(cache, lodindex, baseheight, basecolor, tipcolor, lightstyle_hash);

	R_GrassSurfaceNormal(s, normal);
	if (normal[2] < 0.35f)
		return false;

	maxverts = (size_t)cache->count * 3U;
	vertices = (grass_shader_vertex_t *)malloc(sizeof(*vertices) * maxverts);
	if (!vertices)
	{
		cache->shader_failed[lodindex] = true;
		return false;
	}

	nodlights.count = 0;
	lightcacheptr = R_GrassBeginLightCache();
	vertex_count = 0;

	for (i = 0; i < cache->count; i++)
	{
		const grass_cached_blade_t *blade;
		unsigned int seed, bladebits;
		float height, width, shade, widthrand, shaderand, curlrand, curl, seedcoord, lodrand;
		vec3_t base, light, variedbasecolor, variedtipcolor, litbasecolor, littipcolor;

		blade = &cache->blades[i];
		if (!R_GrassCachedBladeSelectedForStep(blade, cellstep))
			continue;

		seed = blade->seed;
		VectorMA(blade->pos, 0.8f, normal, base);

		bladebits = blade->bladebits;
		widthrand = (float)(bladebits & 0xffU) * (1.0f / 255.0f);
		shaderand = (float)((bladebits >> 8) & 0xffU) * (1.0f / 255.0f);
		height = baseheight * blade->heightscale;
		width = CLAMP(0.25f, height * (0.018f + widthrand * 0.020f), 0.80f) * 1.05f;
		shade = 0.75f + shaderand * 0.45f;
		curlrand = (float)((blade->colorbits >> 24) & 0xffU) * (1.0f / 255.0f);
		curl = curlrand > 0.90f ? 0.45f + (curlrand - 0.90f) * (0.55f / 0.10f) : 0.0f;

		R_GrassLightForPointCached(model, s, &nodlights, blade->pos, false, lightcacheptr, light);
		R_GrassApplyBladeColorVariation(blade->colorbits, basecolor, tipcolor, variedbasecolor, variedtipcolor);
		for (colorindex = 0; colorindex < 3; colorindex++)
		{
			litbasecolor[colorindex] = variedbasecolor[colorindex] * light[colorindex] * shade;
			littipcolor[colorindex] = variedtipcolor[colorindex] * light[colorindex] * shade;
		}

		seedcoord = (float)(seed & 0xffffU) * (1.0f / 256.0f);
		lodrand = R_GrassBitsToFloat(blade->lodbits);
		R_GrassEmitShaderVertex(&vertices[vertex_count++], base, litbasecolor, -0.055f, -1.0f, seedcoord, curl, height, width, lodrand);
		R_GrassEmitShaderVertex(&vertices[vertex_count++], base, litbasecolor, 0.055f, -1.0f, seedcoord, curl, height, width, lodrand);
		R_GrassEmitShaderVertex(&vertices[vertex_count++], base, littipcolor, 0.0f, 1.0f, seedcoord, curl, height, width, lodrand);
	}

	if (vertex_count > 0)
	{
		GL_GenBuffersFunc(1, &cache->shader_vbo[lodindex]);
		if (!cache->shader_vbo[lodindex])
		{
			free(vertices);
			cache->shader_failed[lodindex] = true;
			return false;
		}
		GL_BindBuffer(GL_ARRAY_BUFFER, cache->shader_vbo[lodindex]);
		GL_BufferDataFunc(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(*vertices) * (size_t)vertex_count), vertices, GL_STATIC_DRAW);
		GL_BindBuffer(GL_ARRAY_BUFFER, 0);
	}

	free(vertices);
	cache->shader_vertex_count[lodindex] = vertex_count;
	cache->shader_built[lodindex] = true;
	cache->shader_failed[lodindex] = false;
	return true;
}

static void R_GrassUploadShaderDlights (const msurface_t *s, const entity_t *ent, qboolean force_scan_dlights)
{
	grass_dlight_list_t list;
	float posradius[GRASS_SHADER_DLIGHTS * 4];
	float colormin[GRASS_SHADER_DLIGHTS * 4];
	int i, count;

	if (grassGeomDLightCountLoc < 0)
		return;

	R_GrassBuildDlightList(s, ent, &list, force_scan_dlights);
	count = list.count;
	if (count > GRASS_SHADER_DLIGHTS)
		count = GRASS_SHADER_DLIGHTS;

	for (i = 0; i < count; i++)
	{
		const grass_dlight_t *l = &list.lights[i];
		posradius[i * 4 + 0] = l->origin[0];
		posradius[i * 4 + 1] = l->origin[1];
		posradius[i * 4 + 2] = l->origin[2];
		posradius[i * 4 + 3] = l->radius;
		colormin[i * 4 + 0] = l->color[0];
		colormin[i * 4 + 1] = l->color[1];
		colormin[i * 4 + 2] = l->color[2];
		colormin[i * 4 + 3] = l->minlight;
	}

	GL_Uniform1iFunc(grassGeomDLightCountLoc, count);
	if (count > 0)
	{
		if (grassGeomDLightPosRadiusLoc >= 0)
			GL_Uniform4fvFunc(grassGeomDLightPosRadiusLoc, count, posradius);
		if (grassGeomDLightColorMinLoc >= 0)
			GL_Uniform4fvFunc(grassGeomDLightColorMinLoc, count, colormin);
	}
}

static qboolean R_DrawGrassSurfaceShaderVBO (qmodel_t *model, const entity_t *ent, const msurface_t *s, grass_surface_cache_t *cache, int cellstep, float baseheight, const vec3_t basecolor, const vec3_t tipcolor, qboolean force_scan_dlights)
{
	int lodindex, drawcount;
	vec3_t normal, tangent, bitangent;

	if (!R_GrassEnsureSurfaceShaderVBO(model, s, cache, cellstep, baseheight, basecolor, tipcolor))
		return false;

	lodindex = R_GrassShaderLODIndexForStep(cellstep);
	drawcount = cache->shader_vertex_count[lodindex];
	if (drawcount <= 0)
		return true;

	R_GrassSurfaceNormal(s, normal);
	R_GrassBasisForSurface(s, normal, tangent, bitangent);
	if (grassGeomStaticNormalLoc >= 0)
		GL_Uniform3fFunc(grassGeomStaticNormalLoc, normal[0], normal[1], normal[2]);
	if (grassGeomStaticTangentLoc >= 0)
		GL_Uniform3fFunc(grassGeomStaticTangentLoc, tangent[0], tangent[1], tangent[2]);
	if (grassGeomStaticCellWeightLoc >= 0)
		GL_Uniform1fFunc(grassGeomStaticCellWeightLoc, (float)(cellstep * cellstep));
	R_GrassUploadShaderDlights(s, ent, force_scan_dlights);

	GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	GL_BindBuffer(GL_ARRAY_BUFFER, cache->shader_vbo[lodindex]);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(grass_shader_vertex_t), (const GLvoid *)offsetof(grass_shader_vertex_t, base));
	glColorPointer(4, GL_FLOAT, sizeof(grass_shader_vertex_t), (const GLvoid *)offsetof(grass_shader_vertex_t, color));

	GL_ClientActiveTextureFunc(GL_TEXTURE0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(4, GL_FLOAT, sizeof(grass_shader_vertex_t), (const GLvoid *)offsetof(grass_shader_vertex_t, bladecoord));
	GL_ClientActiveTextureFunc(GL_TEXTURE1);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(4, GL_FLOAT, sizeof(grass_shader_vertex_t), (const GLvoid *)offsetof(grass_shader_vertex_t, geom));
	GL_ClientActiveTextureFunc(GL_TEXTURE0);

	glDrawArrays(GL_TRIANGLES, 0, drawcount);

	GL_ClientActiveTextureFunc(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	GL_ClientActiveTextureFunc(GL_TEXTURE0);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	GL_BindBuffer(GL_ARRAY_BUFFER, 0);
	return true;
}

static void R_DrawGrassSurfaceBlades (qmodel_t *model, const entity_t *ent, const msurface_t *s, const grass_surface_cache_t *cache, float grassamount, float baseheight, float movement, const vec3_t vieworg, const grass_lod_params_t *lodparams, float surface_density_scale, const vec3_t basecolor, const vec3_t tipcolor, int mode, qboolean force_scan_dlights)
{
	int i, cellstep, colorindex;
	float cellweight;
	vec3_t normal, tangent, bitangent, shader_side, shader_bend;
	grass_dlight_list_t dlights;
	grass_light_cache_entry_t *lightcacheptr;

	if (!cache || cache->count <= 0 || grassamount <= 0.0f)
		return;
	R_GrassSurfaceNormal(s, normal);
	if (normal[2] < 0.35f)
		return;
	R_GrassBasisForSurface(s, normal, tangent, bitangent);
	R_GrassBuildDlightList(s, ent, &dlights, force_scan_dlights);
	lightcacheptr = R_GrassBeginLightCache();
	if (mode == GRASS_BLADE_MODE_SHADER)
	{
		shader_side[0] = shader_side[1] = shader_side[2] = 0.0f;
		shader_bend[0] = shader_bend[1] = shader_bend[2] = 0.0f;
	}

	cellstep = R_GrassCellStepForScale(surface_density_scale);
	cellweight = (float)(cellstep * cellstep);

	for (i = 0; i < cache->count; i++)
	{
		const grass_cached_blade_t *blade;
		unsigned int seed, bladebits;
		float lodscale, lodchance, height, width, shade, widthrand, shaderand, anglerand, curlrand, curl;
		vec3_t base, light, variedbasecolor, variedtipcolor, litbasecolor, littipcolor;
		qboolean cheaplight;

		blade = &cache->blades[i];
		seed = blade->seed;

		if (grassamount < 1.0f && R_GrassBitsToFloat(blade->amountbits) > grassamount)
			continue;
		if (!R_GrassCachedBladeSelectedForStep(blade, cellstep))
			continue;

		lodscale = R_GrassPointDensityScale(blade->pos, vieworg, lodparams);
		lodchance = lodscale * cellweight;
		if (lodchance <= 0.0f || (lodchance < 1.0f && R_GrassBitsToFloat(blade->lodbits) > lodchance))
			continue;
		cheaplight = (lodscale < 0.5f);

		VectorMA(blade->pos, 0.8f, normal, base);

		bladebits = blade->bladebits;
		widthrand = (float)(bladebits & 0xffU) * (1.0f / 255.0f);
		shaderand = (float)((bladebits >> 8) & 0xffU) * (1.0f / 255.0f);
		anglerand = (float)((bladebits >> 16) & 0xffffU) * (1.0f / 65535.0f);
		height = baseheight * blade->heightscale;
		width = CLAMP(0.25f, height * (0.018f + widthrand * 0.020f), 0.80f);
		shade = 0.75f + shaderand * 0.45f;
		curlrand = (float)((blade->colorbits >> 24) & 0xffU) * (1.0f / 255.0f);
		curl = curlrand > 0.90f ? 0.45f + (curlrand - 0.90f) * (0.55f / 0.10f) : 0.0f;

		R_GrassLightForPointCached(model, s, &dlights, blade->pos, cheaplight, lightcacheptr, light);
		R_GrassApplyBladeColorVariation(blade->colorbits, basecolor, tipcolor, variedbasecolor, variedtipcolor);
		for (colorindex = 0; colorindex < 3; colorindex++)
		{
			litbasecolor[colorindex] = variedbasecolor[colorindex] * light[colorindex];
			littipcolor[colorindex] = variedtipcolor[colorindex] * light[colorindex];
		}

		if (mode == GRASS_BLADE_MODE_SHADER)
		{
			R_GrassShaderSideForPoint(vieworg, base, normal, tangent, shader_side);
			R_DrawGrassBladeTri(base, normal, shader_side, shader_bend, height, width * 1.05f, shade, (float)(seed & 0xffffU) * (1.0f / 256.0f), curl, litbasecolor, littipcolor);
		}
		else
		{
			float angle, ca, sa;
			vec3_t side, bend;

			angle = anglerand * M_PI * 2.0f;
			ca = cosf(angle);
			sa = sinf(angle);
			side[0] = tangent[0] * ca + bitangent[0] * sa;
			side[1] = tangent[1] * ca + bitangent[1] * sa;
			side[2] = tangent[2] * ca + bitangent[2] * sa;
			R_GrassWindBend(blade->pos, height, movement, seed, bend);

			R_DrawGrassBladeTri(base, normal, side, bend, height, width, shade, (float)(seed & 0xffffU) * (1.0f / 256.0f), curl, litbasecolor, littipcolor);
			if (!cheaplight)
			{
				vec3_t side2;

				side2[0] = -tangent[0] * sa + bitangent[0] * ca;
				side2[1] = -tangent[1] * sa + bitangent[1] * ca;
				side2[2] = -tangent[2] * sa + bitangent[2] * ca;
				R_DrawGrassBladeTri(base, normal, side2, bend, height * 0.92f, width * 0.72f, shade * 0.9f, (float)((seed + 113U) & 0xffffU) * (1.0f / 256.0f), curl * 0.75f, litbasecolor, littipcolor);
			}
		}
	}
}

static void R_DrawGrassBlades (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int i, mode, firstsurface, numsurfaces;
	float density, grassamount, baseheight, grassdist, grasslod, movement, cellsize;
	const grass_settings_t *settings;
	grass_lod_params_t lodparams;
	grass_presence_cache_t *presencecache;
	grass_model_cache_t *modelcache;
	msurface_t *s;
	vec3_t grass_vieworg;
	qboolean use_static_shader, use_scenecache_visibility, force_scan_dlights;

	if (!R_GrassBladesActive() || r_drawflat_cheatsafe || r_lightmap_cheatsafe)
		return;
	if (!model || !R_GrassEntityAllowsGrass(ent))
		return;
	presencecache = R_GrassGetPresenceCache(model, true);
	if (!R_GrassPresenceCacheHasBladeSurfaces(presencecache))
		return;

	settings = R_GrassSettings();
	mode = R_GrassBladeMode();
	density = CLAMP(0.0f, settings->density, GRASS_DENSITY_MAX);
	grassamount = CLAMP(0.0f, settings->amount, 1.0f);
	baseheight = CLAMP(1.0f, settings->height, 96.0f);
	grassdist = CLAMP(0.0f, settings->dist, GRASS_DIST_MAX);
	grasslod = CLAMP(0.0f, settings->lod, 2.0f);
	movement = CLAMP(0.0f, settings->movement, 2.0f);
	cellsize = sqrtf(512.0f / q_max(0.01f, density));
	use_scenecache_visibility = (chain == chain_world && r_grass_scenecache_visframe == r_visframecount);
	force_scan_dlights = use_scenecache_visibility && !gl_flashblend.value;
	R_GrassPointToEntitySpace(ent, r_refdef.vieworg, grass_vieworg);
	R_GrassLODParams(grassdist, grasslod, &lodparams);
	use_static_shader = (mode == GRASS_BLADE_MODE_SHADER && grassamount >= 0.999f &&
		grassGeomStaticModeLoc >= 0 && grassGeomEyePosLoc >= 0 &&
		grassGeomStaticNormalLoc >= 0 && grassGeomStaticTangentLoc >= 0 &&
		grassGeomStaticLodLoc >= 0 && grassGeomStaticCellWeightLoc >= 0 &&
		R_GrassUseStaticShaderVBO());
	if (!use_static_shader && !R_GrassEnsureVertexBatch())
		return;
	modelcache = R_GrassGetModelCache(model, density, cellsize);
	if (!modelcache)
		return;

	GL_DisableMultitexture();
	if (GL_SelectTextureFunc)
		GL_SelectTexture(GL_TEXTURE0);
	if (mode == GRASS_BLADE_MODE_SHADER)
	{
		GL_UseProgramFunc(r_grass_program);
		GL_Uniform1fFunc(grassGeomAmountLoc, 1.0f);
		GL_Uniform1fFunc(grassGeomTimeLoc, R_GrassAnimTime());
		GL_Uniform1fFunc(grassGeomMovementLoc, movement);
		GL_Uniform1fFunc(grassGeomFadeDistLoc, grassdist);
		GL_Uniform1iFunc(grassGeomFogModeLoc, Fog_GetMode());
		if (grassGeomEyePosLoc >= 0)
			GL_Uniform3fFunc(grassGeomEyePosLoc, grass_vieworg[0], grass_vieworg[1], grass_vieworg[2]);
		if (grassGeomStaticModeLoc >= 0)
			GL_Uniform1iFunc(grassGeomStaticModeLoc, use_static_shader ? 1 : 0);
		if (grassGeomStaticLodLoc >= 0)
			GL_Uniform1fFunc(grassGeomStaticLodLoc, grasslod);
	}
	else if (GL_UseProgramFunc)
		GL_UseProgramFunc(0);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.12f);
	glDisable(GL_CULL_FACE);
	glShadeModel(GL_SMOOTH);

	if (!use_static_shader)
		R_GrassBeginVertexBatch();
	firstsurface = model->firstmodelsurface;
	numsurfaces = model->nummodelsurfaces;
	for (i = 0, s = model->surfaces + firstsurface; i < numsurfaces; i++, s++)
	{
		texture_t *t, *animt;
		float surface_density_scale;
		int surface_cellstep;
		vec3_t basecolor, tipcolor;
		grass_surface_cache_t *surfacecache;
		vec3_t surfacenormal;

		if (!R_GrassSurfaceVisibleCached(presencecache, model, s, chain, use_scenecache_visibility))
			continue;
		if (!R_GrassSurfaceCanHaveBladesCached(presencecache, model, s))
			continue;
		if (!s->texinfo || !s->polys || (s->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE | SURF_DRAWFENCE)))
			continue;

		t = s->texinfo->texture;
		if (!R_TextureHasGrass(t))
			continue;

		surface_density_scale = R_GrassSurfaceDensityScale(s, grass_vieworg, &lodparams);
		if (surface_density_scale <= 0.001f)
			continue;
		R_GrassSurfaceNormal(s, surfacenormal);
		if (surfacenormal[2] < 0.35f)
			continue;
		if (R_GrassSurfaceVolumeCulled(s, ent, surfacenormal, baseheight * 1.35f + 2.0f, movement))
			continue;

		animt = R_TextureAnimation(t, ent ? ent->frame : 0);
		R_TextureGrassBladeColors(animt, basecolor, tipcolor);
		surfacecache = R_GrassGetSurfaceCache(modelcache, s);
		if (use_static_shader)
		{
			surface_cellstep = R_GrassCellStepForScale(surface_density_scale);
			if (!R_GrassSurfaceHasAnimatedLightstyles(s) &&
				R_DrawGrassSurfaceShaderVBO(model, ent, s, surfacecache, surface_cellstep, baseheight, basecolor, tipcolor, force_scan_dlights))
				continue;

			if (grassGeomStaticModeLoc >= 0)
				GL_Uniform1iFunc(grassGeomStaticModeLoc, 0);
			if (R_GrassEnsureVertexBatch())
			{
				R_GrassBeginVertexBatch();
				R_DrawGrassSurfaceBlades(model, ent, s, surfacecache, grassamount, baseheight, movement, grass_vieworg, &lodparams, surface_density_scale, basecolor, tipcolor, mode, force_scan_dlights);
				R_GrassEndVertexBatch();
			}
			if (grassGeomStaticModeLoc >= 0)
				GL_Uniform1iFunc(grassGeomStaticModeLoc, 1);
		}
		else
			R_DrawGrassSurfaceBlades(model, ent, s, surfacecache, grassamount, baseheight, movement, grass_vieworg, &lodparams, surface_density_scale, basecolor, tipcolor, mode, force_scan_dlights);
	}
	if (!use_static_shader)
		R_GrassEndVertexBatch();

	glShadeModel(GL_FLAT);
	if (gl_cull.value)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666f);
	glEnable(GL_TEXTURE_2D);
	glColor4f(1, 1, 1, 1);
	if (mode == GRASS_BLADE_MODE_SHADER)
		GL_UseProgramFunc(0);
}

/*
=============
GLGrass_CreateShaders
=============
*/
static void GLGrass_CreateShaders (void)
{
	const GLchar *vertSource =
		"#version 110\n"
		"\n"
		"uniform float GrassTime;\n"
		"uniform float GrassMovement;\n"
		"uniform float GrassFadeDist;\n"
		"uniform vec3 GrassEyePos;\n"
		"uniform int GrassStaticMode;\n"
		"uniform vec3 GrassStaticNormal;\n"
		"uniform vec3 GrassStaticTangent;\n"
		"uniform float GrassStaticLod;\n"
		"uniform float GrassStaticCellWeight;\n"
		"#define GRASS_SHADER_DLIGHTS 4\n"
		"uniform int GrassDLightCount;\n"
		"uniform vec4 GrassDLightPosRadius[GRASS_SHADER_DLIGHTS];\n"
		"uniform vec4 GrassDLightColorMin[GRASS_SHADER_DLIGHTS];\n"
		"\n"
		"varying vec4 BladeCoord;\n"
		"varying vec4 BladeColor;\n"
		"varying float BladeCull;\n"
		"varying vec3 BladeDynLight;\n"
		"varying float FogFragCoord;\n"
		"\n"
		"vec3 GrassSafeNormalize(vec3 v, vec3 fallback)\n"
		"{\n"
		"	float len2 = dot(v, v);\n"
		"	if (len2 > 0.000001)\n"
		"		return v * inversesqrt(len2);\n"
		"	return fallback;\n"
		"}\n"
		"\n"
		"float GrassWindHash(vec2 p)\n"
		"{\n"
		"	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);\n"
		"}\n"
		"\n"
		"float GrassWindNoise(vec2 p)\n"
		"{\n"
		"	vec2 i = floor(p);\n"
		"	vec2 f = fract(p);\n"
		"	f = f * f * (3.0 - 2.0 * f);\n"
		"	float a = GrassWindHash(i);\n"
		"	float b = GrassWindHash(i + vec2(1.0, 0.0));\n"
		"	float c = GrassWindHash(i + vec2(0.0, 1.0));\n"
		"	float d = GrassWindHash(i + vec2(1.0, 1.0));\n"
		"	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
		"}\n"
		"\n"
		"float GrassStaticDensityScale(vec3 base)\n"
		"{\n"
		"	if (GrassFadeDist <= 0.0)\n"
		"		return 1.0;\n"
		"	vec3 delta = base - GrassEyePos;\n"
		"	float d2 = dot(delta, delta);\n"
		"	float dist2 = GrassFadeDist * GrassFadeDist;\n"
		"	if (d2 >= dist2)\n"
		"		return 0.0;\n"
		"	if (GrassStaticLod <= 0.0)\n"
		"		return 1.0;\n"
		"	float nearclip = GrassFadeDist * 0.25;\n"
		"	float nearclip2 = nearclip * nearclip;\n"
		"	if (d2 <= nearclip2)\n"
		"		return 1.0;\n"
		"	float fade = (GrassFadeDist - sqrt(d2)) / (GrassFadeDist - nearclip);\n"
		"	fade = clamp(fade, 0.0, 1.0);\n"
		"	fade = fade * fade * (3.0 - 2.0 * fade);\n"
		"	fade *= fade;\n"
		"	if (GrassStaticLod > 1.0)\n"
		"		fade = pow(fade, GrassStaticLod);\n"
		"	return fade;\n"
		"}\n"
		"\n"
		"void main()\n"
		"{\n"
		"	vec4 bladeCoord = gl_MultiTexCoord0;\n"
		"	float tip = clamp((bladeCoord.y + 1.0) * 0.5, 0.0, 1.0);\n"
		"	float bend = tip * (0.2 + 0.8 * tip);\n"
		"	float seed = bladeCoord.z;\n"
		"	float curl = clamp(bladeCoord.w, 0.0, 1.0);\n"
		"	float bladeCull = 1.0;\n"
		"	vec3 dynLight = vec3(0.0);\n"
		"	vec4 vertex = gl_Vertex;\n"
		"	if (GrassStaticMode != 0)\n"
		"	{\n"
		"		vec4 geom = gl_MultiTexCoord1;\n"
		"		float lodchance = GrassStaticDensityScale(vertex.xyz) * GrassStaticCellWeight;\n"
		"		if (lodchance <= 0.0 || (lodchance < 1.0 && geom.z > lodchance))\n"
		"			bladeCull = 0.0;\n"
		"		vec3 normal = GrassSafeNormalize(GrassStaticNormal, vec3(0.0, 0.0, 1.0));\n"
		"		vec3 tangent = GrassSafeNormalize(GrassStaticTangent, vec3(1.0, 0.0, 0.0));\n"
		"		vec3 viewdir = GrassEyePos - vertex.xyz;\n"
		"		viewdir -= normal * dot(viewdir, normal);\n"
		"		viewdir = GrassSafeNormalize(viewdir, tangent);\n"
		"		vec3 side = GrassSafeNormalize(cross(normal, viewdir), tangent);\n"
		"		float sideSign = 0.0;\n"
		"		if (bladeCoord.x < -0.0001)\n"
		"			sideSign = 1.0;\n"
		"		else if (bladeCoord.x > 0.0001)\n"
		"			sideSign = -1.0;\n"
		"		vertex.xyz += (side * (geom.y * sideSign) + normal * (geom.x * tip)) * bladeCull;\n"
		"		vertex.xyz += (side * (geom.x * curl * 0.16 * tip) - normal * (geom.x * curl * 0.08 * tip)) * bladeCull;\n"
		"		bend *= bladeCull;\n"
		"		for (int li = 0; li < GRASS_SHADER_DLIGHTS; li++)\n"
		"		{\n"
		"			if (li >= GrassDLightCount)\n"
		"				break;\n"
		"			float add = GrassDLightPosRadius[li].w - distance(gl_Vertex.xyz, GrassDLightPosRadius[li].xyz);\n"
		"			if (add > GrassDLightColorMin[li].w)\n"
		"				dynLight += (add * (1.0 / 128.0)) * GrassDLightColorMin[li].xyz;\n"
		"		}\n"
		"	}\n"
		"	vec2 worldXY = vertex.xy * 0.0035;\n"
		"	float windAngle = GrassWindNoise(worldXY + vec2(GrassTime * 0.025, GrassTime * -0.018)) * 6.28318;\n"
		"	vec2 windDir = vec2(cos(windAngle), sin(windAngle));\n"
		"	float windStr = GrassWindNoise(worldXY * 4.0 - vec2(GrassTime * 0.06, GrassTime * 0.04));\n"
		"	float jitter = sin(GrassTime * (1.25 + fract(seed * 0.013) * 0.5) + seed * 0.071) * 0.20;\n"
		"	vertex.xy += (windDir * (0.55 + 0.45 * windStr) + vec2(jitter, jitter * 0.7)) * GrassMovement * bend * 1.6;\n"
		"	BladeCoord = bladeCoord;\n"
		"	BladeColor = gl_Color;\n"
		"	BladeCull = bladeCull;\n"
		"	BladeDynLight = dynLight;\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * vertex;\n"
		"	FogFragCoord = gl_Position.w;\n"
		"}\n";
	const GLchar *fragSource =
		"#version 110\n"
		"\n"
		"uniform float GrassAmount;\n"
		"uniform float GrassTime;\n"
		"uniform float GrassMovement;\n"
		"uniform float GrassFadeDist;\n"
		"uniform int FogMode;\n"
		"\n"
		"varying vec4 BladeCoord;\n"
		"varying vec4 BladeColor;\n"
		"varying float BladeCull;\n"
		"varying vec3 BladeDynLight;\n"
		"varying float FogFragCoord;\n"
		"\n"
		"float FogFactor(float dist)\n"
		"{\n"
		"	if (FogMode == 1)\n"
		"		return (gl_Fog.end - dist) / (gl_Fog.end - gl_Fog.start);\n"
		"	if (FogMode == 2)\n"
		"		return exp(-gl_Fog.density * dist);\n"
		"	return exp(-gl_Fog.density * gl_Fog.density * dist * dist);\n"
		"}\n"
		"\n"
		"float GrassHash1(float n)\n"
		"{\n"
		"	return fract(sin(n) * 43758.5453);\n"
		"}\n"
		"\n"
		"float FogDitherHash(vec2 p)\n"
		"{\n"
		"	return fract(52.9829189 * fract(p.x * 0.06711056 + p.y * 0.00583715));\n"
		"}\n"
		"\n"
		"float FogDither()\n"
		"{\n"
		"	vec2 p = floor(gl_FragCoord.xy);\n"
		"	return (FogDitherHash(p) + FogDitherHash(p + vec2(17.0, 29.0)) - 1.0) * (1.0 / 255.0);\n"
		"}\n"
		"\n"
		"vec4 GrassBlade(vec2 p, float x, float curl)\n"
		"{\n"
		"	float hdist = GrassHash1(x * 1.71);\n"
		"	float s = mix(0.85, 1.75, hdist);\n"
		"	float tip = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);\n"
		"	float curltip = curl * smoothstep(0.58, 1.0, tip);\n"
		"	float sway = (GrassHash1(x * 0.097 + 19.0) - 0.5) * 0.024;\n"
		"	p.x += tip * (0.2 + 0.8 * tip) * sway;\n"
		"	p.x -= curltip * (0.022 + 0.052 * tip);\n"
		"	p.y -= curltip * curltip * 0.055;\n"
		"	p.x *= s;\n"
		"	p.y = (1.0 + p.y) * s - 1.0;\n"
		"	return vec4(mix(vec3(0.05, 0.1, 0.0) * 0.8, vec3(0.0, 0.3, 0.0), (p.y + 1.0) * 0.5 + abs(p.x)), 1.0);\n"
		"}\n"
		"\n"
		"float GrassDither()\n"
		"{\n"
		"	return (fract(gl_FragCoord.x * 0.482635532 + gl_FragCoord.y * 0.1353412) - 0.5) * 0.006;\n"
		"}\n"
		"\n"
		"vec3 GrassBladeTexture(vec2 p, float x, float curl)\n"
		"{\n"
		"	float v = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);\n"
		"	float u = clamp(p.x * 9.0 + 0.5, 0.0, 1.0);\n"
		"	float fiber = GrassHash1(x * 0.041 + floor(u * 32.0) * 3.11 + floor(v * 10.0) * 11.7);\n"
		"	float center = 1.0 - smoothstep(0.0, 0.035, abs(p.x));\n"
		"	float edge = smoothstep(0.035, 0.060, abs(p.x));\n"
		"	float top = smoothstep(0.38, 1.0, v);\n"
		"	float curltip = curl * smoothstep(0.68, 1.0, v);\n"
		"	float fibers = (fiber - 0.5) * 0.16;\n"
		"	float value = mix(0.72, 1.04, v) + fibers + center * (0.055 + top * 0.035) - edge * 0.12;\n"
		"	vec3 detail = vec3(value);\n"
		"	detail += vec3(0.060, 0.085, -0.025) * top * (0.45 + 0.55 * fiber);\n"
		"	detail += vec3(0.025, 0.035, -0.015) * center * (0.35 + 0.65 * v);\n"
		"	detail *= 1.0 - curltip * 0.10;\n"
		"	detail += vec3(0.045, 0.060, -0.025) * curltip * (1.0 - edge);\n"
		"	return clamp(detail, vec3(0.56, 0.56, 0.50), vec3(1.28, 1.34, 1.14));\n"
		"}\n"
		"\n"
		"void main()\n"
		"{\n"
		"	float curl = clamp(BladeCoord.w, 0.0, 1.0);\n"
		"	vec4 blade = GrassBlade(BladeCoord.xy, BladeCoord.z, curl);\n"
		"	float alpha = GrassAmount * BladeCull;\n"
		"	if (GrassFadeDist > 0.0)\n"
		"		alpha *= 1.0 - smoothstep(GrassFadeDist * 0.55, GrassFadeDist * 0.85, FogFragCoord);\n"
		"	if (alpha < 0.12)\n"
		"		discard;\n"
		"	vec3 bladeTexture = GrassBladeTexture(BladeCoord.xy, BladeCoord.z, curl);\n"
		"	vec3 colour = BladeColor.rgb * bladeTexture * (0.84 + blade.g * 0.45);\n"
		"\n"
		"	float side = clamp(abs(BladeCoord.x) * 18.0, 0.0, 1.0);\n"
		"	float tipBlend = clamp((BladeCoord.y + 1.0) * 0.5, 0.0, 1.0);\n"
		"	float root = 1.0 - tipBlend;\n"
		"	float ao = 1.0 - root * root * 0.45;\n"
		"	float sideShade = 0.94 - side * 0.10 + tipBlend * 0.05;\n"
		"	colour *= sideShade * ao;\n"
		"	colour *= 1.0 + BladeDynLight;\n"
		"\n"
		"	colour += vec3(GrassDither());\n"
		"	float fog = FogFactor(FogFragCoord);\n"
		"	fog = clamp(fog, 0.0, 1.0);\n"
		"	colour = mix(gl_Fog.color.rgb, colour, fog);\n"
		"	colour = clamp(colour + vec3(FogDither()), 0.0, 1.0);\n"
		"	gl_FragColor = vec4(colour, 1.0);\n"
		"}\n";

	if (!gl_glsl_able)
		return;

	r_grass_program = GL_CreateProgram (vertSource, fragSource, 0, NULL);
	if (r_grass_program != 0)
	{
		grassGeomAmountLoc = GL_GetUniformLocation (&r_grass_program, "GrassAmount");
		grassGeomTimeLoc = GL_GetUniformLocation (&r_grass_program, "GrassTime");
		grassGeomMovementLoc = GL_GetUniformLocation (&r_grass_program, "GrassMovement");
		grassGeomFadeDistLoc = GL_GetUniformLocation (&r_grass_program, "GrassFadeDist");
		grassGeomFogModeLoc = GL_GetUniformLocation (&r_grass_program, "FogMode");
		grassGeomEyePosLoc = GL_GetUniformLocation (&r_grass_program, "GrassEyePos");
		grassGeomStaticModeLoc = GL_GetUniformLocation (&r_grass_program, "GrassStaticMode");
		grassGeomStaticNormalLoc = GL_GetUniformLocation (&r_grass_program, "GrassStaticNormal");
		grassGeomStaticTangentLoc = GL_GetUniformLocation (&r_grass_program, "GrassStaticTangent");
		grassGeomStaticLodLoc = GL_GetUniformLocation (&r_grass_program, "GrassStaticLod");
		grassGeomStaticCellWeightLoc = GL_GetUniformLocation (&r_grass_program, "GrassStaticCellWeight");
		grassGeomDLightCountLoc = GL_GetUniformLocation (&r_grass_program, "GrassDLightCount");
		grassGeomDLightPosRadiusLoc = GL_GetUniformLocation (&r_grass_program, "GrassDLightPosRadius");
		grassGeomDLightColorMinLoc = GL_GetUniformLocation (&r_grass_program, "GrassDLightColorMin");
	}
}

/*
=============
GLWorld_CreateShaders
=============
*/
static void GLWater_CreateShaders (void)
{
	const char *modedefines[countof(r_water)] = {
		"",
		"#define LIT\n"
	};
	const glsl_attrib_binding_t bindings[] = {
		{ "Vert", vertAttrIndex },
		{ "TexCoords", texCoordsAttrIndex },
		{ "LMCoords", LMCoordsAttrIndex }
	};

	// Driver bug workarounds:
	// - "Intel(R) UHD Graphics 600" version "4.6.0 - Build 26.20.100.7263"
	//    crashing on glUseProgram with `vec3 Vert` and
	//    `gl_ModelViewProjectionMatrix * vec4(Vert, 1.0);`. Work around with
	//    making Vert a vec4. (https://sourceforge.net/p/quakespasm/bugs/39/)
	const GLchar *vertSource = \
		"#version 110\n"
		"%s"
		"\n"
		"attribute vec4 Vert;\n"
		"attribute vec2 TexCoords;\n"
"#ifdef LIT\n"
		"attribute vec2 LMCoords;\n"
		"varying vec2 tc_lm;\n"
"#endif\n"
		"\n"
		"varying float FogFragCoord;\n"
		"varying vec2 tc_tex;\n"
		"\n"
		"void main()\n"
		"{\n"
		"	tc_tex = TexCoords;\n"
"#ifdef LIT\n"
		"	tc_lm = LMCoords;\n"
"#endif\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * Vert;\n"
		"	FogFragCoord = gl_Position.w;\n"
		"}\n";

	const GLchar *fragSource = \
		"#version 110\n"
		"%s"
		"\n"
		"uniform sampler2D Tex;\n"
"#ifdef LIT\n"
		"uniform sampler2D LMTex;\n"
		"uniform float LightScale;\n"
		"varying vec2 tc_lm;\n"
"#endif\n"
		"uniform float Alpha;\n"
		"uniform float WarpTime;\n"
		"uniform int FogMode;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"varying vec2 tc_tex;\n"
		"\n"
		"float FogFactor(float dist)\n"
		"{\n"
		"	if (FogMode == 1)\n"
		"		return (gl_Fog.end - dist) / (gl_Fog.end - gl_Fog.start);\n"
		"	if (FogMode == 2)\n"
		"		return exp(-gl_Fog.density * dist);\n"
		"	return exp(-gl_Fog.density * gl_Fog.density * dist * dist);\n"
		"}\n"
		"\n"
		"float FogDitherHash(vec2 p)\n"
		"{\n"
		"	return fract(52.9829189 * fract(p.x * 0.06711056 + p.y * 0.00583715));\n"
		"}\n"
		"\n"
		"float FogDither()\n"
		"{\n"
		"	vec2 p = floor(gl_FragCoord.xy);\n"
		"	return (FogDitherHash(p) + FogDitherHash(p + vec2(17.0, 29.0)) - 1.0) * (1.0 / 255.0);\n"
		"}\n"
		"\n"
		"void main()\n"
		"{\n"
		"	vec2 ntc = tc_tex;\n"
		//CYCLE 128
		//AMP 8*0x10000
		//SPEED 20
		//	sintable[i] = AMP + sin(i*3.14159*2/CYCLE)*AMP;
		//
		//  r_turb_turb = sintable + ((int)(cl.time*SPEED)&(CYCLE-1));
		//
		//	sturb = ((r_turb_s + r_turb_turb[(r_turb_t>>16)&(CYCLE-1)])>>16)&63;
        //	tturb = ((r_turb_t + r_turb_turb[(r_turb_s>>16)&(CYCLE-1)])>>16)&63;
        //The following 4 lines SHOULD match the software renderer, except normalised coords rather than snapped texels
        "#define M_PI 3.14159\n"
		"#define TIMEBIAS (((WarpTime*20.0)*M_PI*2.0)/128.0)\n"
		"	ntc += 0.125 + sin(tc_tex.ts*M_PI + TIMEBIAS)*0.125;\n"
		"	vec4 result = texture2D(Tex, ntc.st);\n"
"#ifdef LIT\n"
		"	result *= texture2D(LMTex, tc_lm.xy);\n"
		"	result.rgb *= LightScale;\n"
"#endif\n"
		"	result.a *= Alpha;\n"
		"	result = clamp(result, 0.0, 1.0);\n"
		"	float fog = FogFactor(FogFragCoord);\n"
		"	fog = clamp(fog, 0.0, 1.0);\n"
		"	result.rgb = mix(gl_Fog.color.rgb, result.rgb, fog);\n"
		"	result.rgb = clamp(result.rgb + vec3(FogDither()), 0.0, 1.0);\n"
		"	gl_FragColor = result;\n"
		"}\n";

	const GLchar *vertSource_sky =
		"#version 110\n"
		"\n"
		"uniform vec3 EyePos;\n"
		"\n"
		"attribute vec4 Vert;\n"
		"attribute vec2 TexCoords;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"varying vec3 SkyDir;\n"
		"\n"
		"void main()\n"
		"{\n"
			"SkyDir = Vert.xyz - EyePos;\n"
			"gl_Position = gl_ModelViewProjectionMatrix * Vert;\n"
			"FogFragCoord = gl_Position.w;\n"
		"}\n";
	const GLchar *fragSource_sky =
		"#version 110\n"
		"\n"
		"uniform sampler2D Tex;\n"
		"uniform sampler2D CloudTex;\n"
		"uniform float WarpTime;\n"
		"uniform float Alpha, FogAlpha;\n"
		"uniform vec3 FogColour;\n"
		"varying float FogFragCoord;\n"
		"varying vec3 SkyDir;\n"
		"void main ()\n"
		"{\n"
			"vec2 tccoord;\n"
			"vec3 dir = SkyDir;\n"
			"dir.z *= 3.0;\n"
			"dir.xy *= 2.953125/length(dir);\n"
			"tccoord = (dir.xy + WarpTime*0.0625);\n"
			"vec3 sky = vec3(texture2D(Tex, tccoord));\n"
			"tccoord = (dir.xy + WarpTime*0.125);\n"
			"vec4 clouds = texture2D(CloudTex, tccoord);\n"
			"clouds.a *= Alpha;\n"
			"sky = (sky.rgb*(1.0-clouds.a)) + (clouds.a*clouds.rgb);\n"

#if 1	//sky is logically an infinite distance away, so fog is just an alpha blend with the colour, no distance calcs needed.
			"if (FogAlpha > 0.0)\n"
				"sky.rgb = mix(sky.rgb, FogColour.rgb, FogAlpha);\n"
#else	//do fog as normal. we actually have distance values.
			"float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
			"fog = clamp(fog, 0.0, 1.0) * FogAlpha + (1.0-FogAlpha);\n"
			"sky.rgb = mix(gl_Fog.color.rgb, sky.rgb, fog);\n"
#endif

			"gl_FragColor = vec4(sky, 1.0);\n"
		"}\n";

	const GLchar *vertSource_fastsky =
		"#version 110\n"
		"attribute vec4 Vert;\n"
		"varying float FogFragCoord;\n"
		"void main()\n"
		"{\n"
			"gl_Position = gl_ModelViewProjectionMatrix * Vert;\n"
			"FogFragCoord = gl_Position.w;\n"
		"}\n";
	const GLchar *fragSource_fastsky =
		"#version 110\n"
		"\n"
		"uniform float Alpha, FogAlpha;\n"
		"uniform vec3 SkyColour;\n"
		"uniform vec3 FogColour;\n"
		"varying float FogFragCoord;\n"
		"void main ()\n"
		"{\n"
			"vec3 sky = SkyColour.rgb;\n"

#if 1	//sky is logically an infinite distance away, so fog is just an alpha blend with the colour, no distance calcs needed.
			"if (FogAlpha > 0.0)\n"
				"sky.rgb = mix(sky.rgb, FogColour.rgb, FogAlpha);\n"
#else	//do fog as normal. we actually have distance values.
			"float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
			"fog = clamp(fog, 0.0, 1.0) * FogAlpha + (1.0-FogAlpha);\n"
			"sky.rgb = mix(gl_Fog.color.rgb, sky.rgb, fog);\n"
#endif
			"gl_FragColor = vec4(sky, 1.0);\n"
		"}\n";

	size_t i;
	char vtext[1024];
	char ftext[4096];
	gl_glsl_water_able = false;

	if (!gl_glsl_able)
		return;

	for (i = 0; i < countof(r_water); i++)
	{
		if (i == 3)
			r_water[i].program = GL_CreateProgram (vertSource_fastsky, fragSource_fastsky, sizeof(bindings)/sizeof(bindings[0]), bindings);
		else if (i == 2)
			r_water[i].program = GL_CreateProgram (vertSource_sky, fragSource_sky, sizeof(bindings)/sizeof(bindings[0]), bindings);
		else
		{
			snprintf(vtext, sizeof(vtext), vertSource, modedefines[i]);
			snprintf(ftext, sizeof(ftext), fragSource, modedefines[i]);
			r_water[i].program = GL_CreateProgram (vtext, ftext, sizeof(bindings)/sizeof(bindings[0]), bindings);
		}

		if (r_water[i].program != 0)
		{
			// get uniform locations
			GLuint texLoc				= ((i!=3)?GL_GetUniformLocation (&r_water[i].program, "Tex"):-1);
			GLuint LMTexLoc				= ((i==1)?GL_GetUniformLocation (&r_water[i].program, "LMTex"):-1);
			GLuint CloudTexLoc			= ((i==2)?GL_GetUniformLocation (&r_water[i].program, "CloudTex"):-1);
			r_water[i].light_scale		= ((i==1)?GL_GetUniformLocation (&r_water[i].program, "LightScale"):-1);
			r_water[i].alpha_scale		= ((i!=3)?GL_GetUniformLocation (&r_water[i].program, "Alpha"):-1);
			r_water[i].time				= ((i!=3)?GL_GetUniformLocation (&r_water[i].program, "WarpTime"):-1);
			r_water[i].eyepos			= ((i==2)?GL_GetUniformLocation (&r_water[i].program, "EyePos"):-1);
			r_water[i].fogalpha			= ((i>=2)?GL_GetUniformLocation (&r_water[i].program, "FogAlpha"):-1);
			r_water[i].colour			= ((i==3)?GL_GetUniformLocation (&r_water[i].program, "SkyColour"):-1);
			r_water[i].fogmode			= ((i<2)?GL_GetUniformLocation (&r_water[i].program, "FogMode"):-1);
			r_water[i].skyfogcolor		= ((i>=2)?GL_GetUniformLocation (&r_water[i].program, "FogColour"):-1);

			if (!r_water[i].program)
				return;

			//bake constants here.
			GL_UseProgramFunc (r_water[i].program);
			GL_Uniform1iFunc (texLoc, 0);
			if (LMTexLoc != -1)
				GL_Uniform1iFunc (LMTexLoc, 1);
			if (CloudTexLoc != -1)
				GL_Uniform1iFunc (CloudTexLoc, 2);
			GL_UseProgramFunc (0);
		}
		else
			return;	//erk?
	}
	gl_glsl_water_able = true;
}

/*
================
R_DrawTextureChains_Water -- johnfitz
================
*/
void R_DrawTextureChains_Water (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	glpoly_t	*p;
	qboolean	bound;
	float entalpha;

	if (r_drawflat_cheatsafe || r_lightmap_cheatsafe) // ericw -- !r_drawworld_cheatsafe check moved to R_DrawWorld_Water ()
		return;

	if (gl_glsl_water_able)
	{
		int lastlightmap = -2;
		int mode = -1;
		const int overbright = !!gl_overbright.value;
		const int wide10bits = (gl_lightmap_format == GL_RGB10_A2);
		float lightmapscale = (overbright?2:1) * (wide10bits?4:1);
		for (i=0 ; i<model->numtextures ; i++)
		{
			t = model->textures[i];
			if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
				continue;
			s = t->texturechains[chain];

			entalpha = GL_WaterAlphaForEntitySurface (ent, s);
			if (entalpha < 1.0f)
			{
				glDepthMask (GL_FALSE);
				glEnable (GL_BLEND);
			}

// Bind the buffers
			GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
			GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0); // indices come from client memory!
			GL_VertexAttribPointerFunc (vertAttrIndex,      3, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0));
			GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 3);
			GL_VertexAttribPointerFunc (LMCoordsAttrIndex,  2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 5);

			//actually use the buffers...
			GL_EnableVertexAttribArrayFunc (vertAttrIndex);
			GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);

			GL_SelectTexture (GL_TEXTURE0);
			GL_Bind (t->gltexture);
			GL_SelectTexture (GL_TEXTURE1);
			for (; s; s = s->texturechain)
			{
				if (s->lightmaptexturenum != lastlightmap)
				{
					R_FlushBatch(IS_WATER); // woods #caustics

					mode = s->lightmaptexturenum>=0 && !r_fullbright_cheatsafe;
					if (mode)
					{	//lit
						GL_EnableVertexAttribArrayFunc (LMCoordsAttrIndex);
						GL_Bind (lightmaps[s->lightmaptexturenum].texture);
					}
					else	//unlit
						GL_DisableVertexAttribArrayFunc (LMCoordsAttrIndex);

					GL_UseProgramFunc (r_water[mode].program);
					GL_Uniform1fFunc (r_water[mode].time, cl.time);
					GL_Uniform1iFunc (r_water[mode].fogmode, Fog_GetMode());
					if (r_water[mode].light_scale != -1)
						GL_Uniform1fFunc (r_water[mode].light_scale, lightmapscale);
					GL_Uniform1fFunc (r_water[mode].alpha_scale, entalpha);
					lastlightmap = s->lightmaptexturenum;
				}
				R_BatchSurface (s, IS_WATER); // woods #caustics

				rs_brushpasses++;
			}

			R_FlushBatch (IS_WATER); // woods #caustics
			GL_UseProgramFunc (0);
			GL_DisableVertexAttribArrayFunc (vertAttrIndex);
			GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
			GL_DisableVertexAttribArrayFunc (LMCoordsAttrIndex);
			GL_SelectTexture (GL_TEXTURE0);
			lastlightmap = -2;

			if (entalpha < 1.0f)
			{
				glDepthMask (GL_TRUE);
				glDisable (GL_BLEND);
			}
		}
	}
	else
	{
		// legacy water for people with such old gpus that they can't even use glsl.
		for (i=0 ; i<model->numtextures ; i++)
		{
			t = model->textures[i];
			if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
				continue;
			bound = false;
			entalpha = 1.0f;
			for (s = t->texturechains[chain]; s; s = s->texturechain)
			{
				if (!bound) //only bind once we are sure we need this texture
				{
					entalpha = GL_WaterAlphaForEntitySurface (ent, s);
					R_BeginTransparentDrawing (entalpha);
					GL_Bind (t->gltexture);
					bound = true;
				}
				for (p = s->polys->next; p; p = p->next)
				{
					DrawWaterPoly (p);
					rs_brushpasses++;
				}
			}
			R_EndTransparentDrawing (entalpha);
		}
	}
}

/*
================
R_DrawTextureChains_White -- johnfitz -- draw sky and water as white polys when r_lightmap is 1
================
*/
void R_DrawTextureChains_White (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;

	glDisable (GL_TEXTURE_2D);
	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTILED))
			continue;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
	}
	glEnable (GL_TEXTURE_2D);
}

/*
================
R_DrawLightmapChains -- johnfitz -- R_BlendLightmaps stripped down to almost nothing
================
*/
void R_DrawLightmapChains (void)
{
	int			i, j;
	glpoly_t	*p;
	float		*v;

	for (i=0 ; i<lightmap_count ; i++)
	{
		if (!lightmaps[i].polys)
			continue;

		GL_Bind (lightmaps[i].texture);
		for (p = lightmaps[i].polys; p; p=p->chain)
		{
			glBegin (GL_POLYGON);
			v = p->verts[0];
			for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[5], v[6]);
				glVertex3fv (v);
			}
			glEnd ();
			rs_brushpasses++;
		}
	}
}

/*
=============
GLWorld_CreateShaders
=============
*/
void GLWorld_CreateShaders (void)
{
	const glsl_attrib_binding_t bindings[] = {
		{ "Vert", vertAttrIndex },
		{ "TexCoords", texCoordsAttrIndex },
		{ "LMCoords", LMCoordsAttrIndex }
	};

	// Driver bug workarounds:
	// - "Intel(R) UHD Graphics 600" version "4.6.0 - Build 26.20.100.7263"
	//    crashing on glUseProgram with `vec3 Vert` and
	//    `gl_ModelViewProjectionMatrix * vec4(Vert, 1.0);`. Work around with
	//    making Vert a vec4. (https://sourceforge.net/p/quakespasm/bugs/39/)
	const GLchar *vertSource = \
		"#version 110\n"
		"\n"
		"attribute vec4 Vert;\n"
		"attribute vec2 TexCoords;\n"
		"attribute vec2 LMCoords;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"varying vec2 tc_tex;\n"
		"varying vec2 tc_lm;\n"
		"\n"
		"void main()\n"
		"{\n"
		"	tc_tex = TexCoords;\n"
		"	tc_lm = LMCoords;\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * Vert;\n"
		"	FogFragCoord = gl_Position.w;\n"
		"}\n";
	
	const GLchar *fragSource = \
		"#version 110\n"
		"\n"
		"#define M_PI 3.14159\n"
		"\n"
		"uniform sampler2D Tex;\n"
		"uniform sampler2D LMTex;\n"
		"uniform sampler2D FullbrightTex;\n"
		"uniform sampler2D CausticsTex;\n"
		"uniform bool UseFullbrightTex;\n"
		"uniform bool UseOverbright;\n"
		"uniform bool UseAlphaTest;\n"
		"uniform bool UseCausticsTex;\n"
		"uniform bool UseGrass;\n"
		"uniform bool UseLightmapWide;\n"
		"uniform bool UseLightmapOnly;\n"
		"uniform float Alpha;\n"
		"uniform float ClTime;\n"
		"uniform float CausticsOpacity;\n"
		"uniform float GrassAmount;\n"
		"uniform float GrassTime;\n"
		"uniform vec3 GrassBaseColor;\n"
		"uniform vec3 GrassTipColor;\n"
		"uniform float GrassMovement;\n"
		"uniform int FogMode;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"varying vec2 tc_tex;\n"
		"varying vec2 tc_lm;\n"
		"\n"
		"float FogFactor(float dist)\n"
		"{\n"
		"	if (FogMode == 1)\n"
		"		return (gl_Fog.end - dist) / (gl_Fog.end - gl_Fog.start);\n"
		"	if (FogMode == 2)\n"
		"		return exp(-gl_Fog.density * dist);\n"
		"	return exp(-gl_Fog.density * gl_Fog.density * dist * dist);\n"
		"}\n"
		"\n"
		"float FogDitherHash(vec2 p)\n"
		"{\n"
		"	return fract(52.9829189 * fract(p.x * 0.06711056 + p.y * 0.00583715));\n"
		"}\n"
		"\n"
		"float FogDither()\n"
		"{\n"
		"	vec2 p = floor(gl_FragCoord.xy);\n"
		"	return (FogDitherHash(p) + FogDitherHash(p + vec2(17.0, 29.0)) - 1.0) * (1.0 / 255.0);\n"
		"}\n"
		"\n"
		"float GrassHash(vec2 p)\n"
		"{\n"
		"	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);\n"
		"}\n"
		"\n"
		"float GrassNoise(vec2 p)\n"
		"{\n"
		"	vec2 i = floor(p);\n"
		"	vec2 f = fract(p);\n"
		"	f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n"
		"	return mix(mix(GrassHash(i), GrassHash(i + vec2(1.0, 0.0)), f.x),\n"
		"	           mix(GrassHash(i + vec2(0.0, 1.0)), GrassHash(i + vec2(1.0, 1.0)), f.x), f.y);\n"
		"}\n"
		"\n"
		"vec2 GrassFlow(vec2 p)\n"
		"{\n"
		"	float weather = GrassNoise(p * 0.018 + vec2(GrassTime * 0.018, -GrassTime * 0.011));\n"
		"	float eddy = GrassNoise(p * 0.057 + vec2(-GrassTime * 0.021, GrassTime * 0.014));\n"
		"	float gust = GrassNoise(p * 0.13 + vec2(GrassTime * 0.045, -GrassTime * 0.028));\n"
		"	float pulse = 0.5 + 0.5 * sin(GrassTime * (0.18 + weather * 0.16) + gust * 6.28318);\n"
		"	float angle = weather * M_PI * 2.0 + (eddy - 0.5) * 1.15;\n"
		"	vec2 dir = vec2(cos(angle), sin(angle));\n"
		"	vec2 side = vec2(-dir.y, dir.x);\n"
		"	return dir * (0.035 + 0.115 * gust * pulse) + side * (0.045 * (eddy - 0.5));\n"
		"}\n"
		"\n"
		"float GrassBlade(vec2 uv, vec2 scale, float seed)\n"
		"{\n"
		"	vec2 q = uv * scale + seed;\n"
		"	vec2 cell = floor(q);\n"
		"	vec2 f = fract(q);\n"
		"	float rnd = GrassHash(cell + seed);\n"
		"	float hdist = (GrassHash(cell + vec2(4.7, 8.3) + seed) + GrassHash(cell + vec2(9.2, 1.6) + seed) + GrassHash(cell + vec2(2.5, 12.1) + seed) + GrassHash(cell + vec2(15.3, 5.4) + seed)) * 0.25;\n"
		"	float height = 0.48 + 0.48 * hdist;\n"
		"	float width = 0.018 + 0.022 * GrassHash(cell + vec2(9.1, 2.4));\n"
		"	float root = 0.20 + 0.60 * GrassHash(cell + vec2(12.5, 6.6) + seed);\n"
		"	float tip = f.y * f.y;\n"
		"	float wind = dot(GrassFlow(cell + seed), vec2(1.0, 0.35)) * GrassMovement;\n"
		"	float sway = 0.035 * cos(GrassTime * 0.95 + rnd * 6.28318 + cell.y * 0.17) * GrassMovement;\n"
		"	float center = root + tip * (wind + sway + (rnd - 0.5) * 0.18);\n"
		"	float shape = 1.0 - smoothstep(0.0, width, abs(f.x - center));\n"
		"	shape *= smoothstep(0.02, 0.18, f.y);\n"
		"	shape *= 1.0 - smoothstep(height, height + 0.08, f.y);\n"
		"	return shape * (0.65 + 0.35 * rnd);\n"
		"}\n"
		"\n"
		"void main()\n"
		"{\n"
		"	vec4 result = texture2D(Tex, tc_tex.xy);\n"
		"	vec4 lightmapColor = texture2D(LMTex, tc_lm.xy); // Sample lightmap early\n"
		"	if (UseLightmapWide)\n"
		"	    lightmapColor.rgb *= 4.0;\n"
		"	float lightBrightness = (lightmapColor.r + lightmapColor.g + lightmapColor.b) / 3.0;\n"
		"	float surfaceLightFactor = clamp(lightBrightness * 1.5, 0.0, 1.0);\n"
		"	vec3 grassLightColor = lightmapColor.rgb;\n"
		"	if (UseOverbright)\n"
		"		grassLightColor *= 2.0;\n"
		"	float grassLightBrightness = (grassLightColor.r + grassLightColor.g + grassLightColor.b) / 3.0;\n"
		"	float grassLightFactor = clamp(grassLightBrightness * 1.5, 0.0, 1.0);\n"
		"	if (UseLightmapOnly)\n"
		"		result = vec4(0.5, 0.5, 0.5, 1.0);\n"
		"	if (UseAlphaTest && (result.a < 0.666))\n"
		"		discard;\n"
		"	result *= lightmapColor;\n"
		"	if (UseOverbright)\n"
		"		result.rgb *= 2.0;\n"
		"	if (UseGrass && GrassAmount > 0.0)\n"
		"	{\n"
		"		float grass = GrassBlade(tc_tex.xy, vec2(48.0, 18.0), 0.0);\n"
		"		grass += 0.65 * GrassBlade(tc_tex.xy + vec2(0.17, 0.41), vec2(72.0, 27.0), 19.7);\n"
		"		grass = clamp(grass, 0.0, 1.0);\n"
		"		float grassVar = GrassNoise(tc_tex.xy * 18.0 + GrassTime * 0.03);\n"
		"		vec3 grassColor = mix(GrassBaseColor, GrassTipColor, grassVar);\n"
		"		grassColor *= max(grassLightColor, vec3(0.05));\n"
		"		float grassAlpha = grass * GrassAmount * grassLightFactor;\n"
		"		vec3 grassMix = mix(result.rgb * 0.65, grassColor, 0.75);\n"
		"		result.rgb = mix(result.rgb, grassMix, grassAlpha);\n"
		"	}\n"
		"	if (UseFullbrightTex)\n"
		"		result += texture2D(FullbrightTex, tc_tex.xy);\n"
		"\n"
		"	if (UseCausticsTex)\n"
		"	{\n"
		"       // --- Chromatic Aberration Start --- \n"
		"       const float aberrationAmount = 0.0015; // Hardcoded faint aberration strength \n"
		"       float causticsSpeed = 0.5; \n"
		"		vec2 causticsCoord = vec2(\n"
		"			(tc_tex.x + sin(0.465 * (causticsSpeed * ClTime + tc_tex.y))) * -0.1234375,\n"
		"			(tc_tex.y + sin(0.465 * (causticsSpeed * ClTime + tc_tex.x))) * -0.1234375\n"
		"		);\n"
		"       vec2 offsetR = vec2(aberrationAmount, 0.0);\n"
		"       vec2 offsetB = vec2(-aberrationAmount, 0.0);\n"
		"       float causticsR = texture2D(CausticsTex, causticsCoord + offsetR).r;\n"
		"       float causticsG = texture2D(CausticsTex, causticsCoord).g;\n"
		"       float causticsB = texture2D(CausticsTex, causticsCoord + offsetB).b;\n"
		"       float causticsA = texture2D(CausticsTex, causticsCoord).a;\n"
		"		vec4 caustics = vec4(causticsR, causticsG, causticsB, causticsA);\n"
		"\n"
		"       // --- Second Layer ----------------------------------------------------- \n"
		"       vec2 causticsCoord2 = vec2(\n"
		"           (tc_tex.x + sin(0.395 * (causticsSpeed * ClTime - tc_tex.y))) * -0.093,\n"
		"           (tc_tex.y + sin(0.475 * (causticsSpeed * ClTime - tc_tex.x))) * -0.093\n"
		"       );\n"
		"       vec3 c2 = texture2D(CausticsTex, causticsCoord2).rgb;\n"
		"       vec3 causticsRGB = max(caustics.rgb, c2);\n"
		"\n"
		"       // --- Blend using Light Factor --- \n"
		"       // Modulate the base CausticsOpacity by the calculated lightFactor\n"
		"       float finalCausticsOpacity = CausticsOpacity * surfaceLightFactor;\n"
		"       result.rgb = mix(result.rgb, causticsRGB * result.rgb * 2.0, finalCausticsOpacity);\n"
		"	}\n"
		"\n"
		"	result = clamp(result, 0.0, 1.0);\n"
		"	float fog = FogFactor(FogFragCoord);\n"
		"	fog = clamp(fog, 0.0, 1.0);\n"
		"	result = mix(gl_Fog.color, result, fog);\n"
		"	result.rgb = clamp(result.rgb + vec3(FogDither()), 0.0, 1.0);\n"
		"	result.a = Alpha;\n" // FIXME: This will make almost transparent things cut holes though heavy fog
		"	gl_FragColor = result;\n"
		"}\n";

	GLWorld_DeleteShaderPrograms();

	if (!gl_glsl_alias_able)
		return;

	r_world_program = GL_CreateProgram (vertSource, fragSource, sizeof(bindings)/sizeof(bindings[0]), bindings);
	
	if (r_world_program != 0)
	{
		// get uniform locations
		texLoc = GL_GetUniformLocation (&r_world_program, "Tex");
		LMTexLoc = GL_GetUniformLocation (&r_world_program, "LMTex");
		fullbrightTexLoc = GL_GetUniformLocation (&r_world_program, "FullbrightTex");
		causticsTexLoc = GL_GetUniformLocation(&r_world_program, "CausticsTex"); // woods #caustics
		useFullbrightTexLoc = GL_GetUniformLocation (&r_world_program, "UseFullbrightTex");
		useOverbrightLoc = GL_GetUniformLocation (&r_world_program, "UseOverbright");
		useAlphaTestLoc = GL_GetUniformLocation (&r_world_program, "UseAlphaTest");
		useCausticsTexLoc = GL_GetUniformLocation(&r_world_program, "UseCausticsTex"); // woods #caustics
		useGrassLoc = GL_GetUniformLocation (&r_world_program, "UseGrass"); // woods #grass
		useLightmapWideLoc = GL_GetUniformLocation (&r_world_program, "UseLightmapWide");
		useLightmapOnlyLoc = GL_GetUniformLocation (&r_world_program, "UseLightmapOnly");
		alphaLoc = GL_GetUniformLocation (&r_world_program, "Alpha");
		clTimeLoc = GL_GetUniformLocation(&r_world_program, "ClTime"); // woods #caustics
		causticsOpacityLoc = GL_GetUniformLocation(&r_world_program, "CausticsOpacity"); // woods #caustics
		grassAmountLoc = GL_GetUniformLocation (&r_world_program, "GrassAmount"); // woods #grass
		grassTimeLoc = GL_GetUniformLocation (&r_world_program, "GrassTime"); // woods #grass
		grassBaseColorLoc = GL_GetUniformLocation (&r_world_program, "GrassBaseColor"); // woods #grass
		grassTipColorLoc = GL_GetUniformLocation (&r_world_program, "GrassTipColor"); // woods #grass
		grassMovementLoc = GL_GetUniformLocation (&r_world_program, "GrassMovement"); // woods #grass
		fogModeLoc = GL_GetUniformLocation (&r_world_program, "FogMode");

		GL_UseProgramFunc (r_world_program);
		GL_Uniform1iFunc (texLoc, 0);
		GL_Uniform1iFunc (LMTexLoc, 1);
		GL_Uniform1iFunc (fullbrightTexLoc, 2);
		GL_Uniform1iFunc (useGrassLoc, 0);
		R_SetGrassColorUniforms(NULL);
		GL_UseProgramFunc (0);
	}

	GLGrass_CreateShaders();
	GLWater_CreateShaders();
}

/*
================
R_DrawTextureChains_GLSL -- ericw

Draw lightmapped surfaces with fulbrights in one pass, using VBO.
Requires 3 TMUs, OpenGL 2.0
================
*/
void R_DrawTextureChains_GLSL (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	const float	entalpha = (ent != NULL) ?
			 ENTALPHA_DECODE(ent->alpha) : 1.0f;
	const int	overbright = !!gl_overbright.value;
	const int wide10bits = (gl_lightmap_format == GL_RGB10_A2);

	int			i;
	msurface_t	*s;
	texture_t	*t;
	texture_t	*animt;
	//qboolean	bound; //removed this cos it was pointless anyway
	int		lastlightmap;
	gltexture_t	*fullbright = NULL;
	const unsigned int enteffects = (ent != NULL) ? ent->effects : 0;

// enable blending / disable depth writes
	if (enteffects & EF_ADDITIVE)
	{
		glDepthMask (GL_FALSE);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE);
		glEnable (GL_BLEND);
	}
	else if (entalpha < 1)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
	}

	GL_UseProgramFunc (r_world_program);

// Bind the buffers
	GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0); // indices come from client memory!

	GL_EnableVertexAttribArrayFunc (vertAttrIndex);
	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (LMCoordsAttrIndex);

	GL_VertexAttribPointerFunc (vertAttrIndex,      3, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0));
	GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 3);
	GL_VertexAttribPointerFunc (LMCoordsAttrIndex,  2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 5);

// set uniforms
	GL_Uniform1iFunc (texLoc, 0);
	GL_Uniform1iFunc (LMTexLoc, 1);
	GL_Uniform1iFunc (fullbrightTexLoc, 2);
	GL_Uniform1iFunc(causticsTexLoc, 3); // woods #caustics
	GL_Uniform1iFunc (useFullbrightTexLoc, 0);
	GL_Uniform1iFunc (useOverbrightLoc, overbright);
	GL_Uniform1iFunc(useCausticsTexLoc, 0); // woods #caustics
	GL_Uniform1iFunc (useGrassLoc, 0); // woods #grass
	GL_Uniform1iFunc (useAlphaTestLoc, 0);
	GL_Uniform1iFunc (useLightmapWideLoc, wide10bits);
	GL_Uniform1iFunc (useLightmapOnlyLoc, 0);
	GL_Uniform1fFunc (alphaLoc, entalpha);
	GL_Uniform1fFunc(clTimeLoc, cl.time); // woods #caustics
	GL_Uniform1fFunc(causticsOpacityLoc, gl_caustics.value); // woods #caustics
	GL_Uniform1fFunc (grassAmountLoc, R_GrassAmount()); // woods #grass
	GL_Uniform1fFunc (grassTimeLoc, R_GrassAnimTime()); // woods #grass
	GL_Uniform1fFunc (grassMovementLoc, R_GrassMovement()); // woods #grass
	GL_Uniform1iFunc (fogModeLoc, Fog_GetMode());

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

		animt = R_TextureAnimation(t, ent != NULL ? ent->frame : 0);

	// Enable/disable TMU 2 (fullbrights)
	// FIXME: Move below to where we bind GL_TEXTURE0
		if (gl_fullbrights.value && (fullbright = animt->fullbright))
		{
			GL_SelectTexture (GL_TEXTURE2);
			GL_Bind (fullbright);
			GL_Uniform1iFunc (useFullbrightTexLoc, 1);
		}
		else
			GL_Uniform1iFunc (useFullbrightTexLoc, 0);

		R_ClearBatch ();

		//bind the appropriate diffuse
		GL_SelectTexture (GL_TEXTURE0);
		GL_Bind (animt->gltexture);
		if (R_TextureUsesSurfaceGrass(t) && R_GrassEntityAllowsGrass(ent))
		{
			GL_Uniform1iFunc (useGrassLoc, 1); // woods #grass
			R_SetGrassColorUniforms(animt); // woods #grass
		}
		else
			GL_Uniform1iFunc (useGrassLoc, 0); // woods #grass
		if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
			GL_Uniform1iFunc (useAlphaTestLoc, 1); // Flip alpha test back on

		GL_SelectTexture (GL_TEXTURE1);
		lastlightmap = -1;	//we're checking anyway, so w/e

		int underwater = 0;

		for (underwater = 0; underwater < 2; underwater++)
		{
		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
				if ((!underwater && !(s->flags & SURF_UNDERWATER)) || (underwater && (s->flags & SURF_UNDERWATER)))
				{
						GL_SelectTexture(GL_TEXTURE0);
						GL_Bind(animt->gltexture);
						if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
							GL_Uniform1iFunc(useAlphaTestLoc, 1); // Flip alpha test back on

					if (s->lightmaptexturenum != lastlightmap)
						R_FlushBatch(underwater ? UNDER_WATER : ABOVE_WATER);

					GL_SelectTexture(GL_TEXTURE1);
					GL_Bind (lightmaps[s->lightmaptexturenum].texture);
					lastlightmap = s->lightmaptexturenum;
					R_BatchSurface(s, underwater ? UNDER_WATER : ABOVE_WATER);

					rs_brushpasses++;
				}
			}

			R_FlushBatch(underwater ? UNDER_WATER : ABOVE_WATER);
		}


		if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
			GL_Uniform1iFunc (useAlphaTestLoc, 0); // Flip alpha test back off
	}

	// clean up
	GL_DisableVertexAttribArrayFunc (vertAttrIndex);
	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (LMCoordsAttrIndex);

	GL_UseProgramFunc (0);
	GL_SelectTexture (GL_TEXTURE0);

	if (enteffects & EF_ADDITIVE)
	{
		glDepthMask (GL_TRUE);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);	//our normal alpha setting.
		glDisable (GL_BLEND);
	}
	else if (entalpha < 1)
	{
		glDepthMask (GL_TRUE);
		glDisable (GL_BLEND);
	}
}

/*
================
R_DrawLightmapChains_GLSL -- ericw
================
*/
void R_DrawLightmapChains_GLSL(qmodel_t* model, entity_t* ent, texchain_t chain)
{
	const int	overbright = !!gl_overbright.value;
	const int wide10bits = (gl_lightmap_format == GL_RGB10_A2);

	int			i;
	msurface_t* s;
	texture_t* t;
	int		lastlightmap;

	GL_UseProgramFunc(r_world_program);

	// Bind the buffers
	GL_BindBuffer(GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // indices come from client memory!

	GL_EnableVertexAttribArrayFunc(vertAttrIndex);
	GL_EnableVertexAttribArrayFunc(texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc(LMCoordsAttrIndex);

	GL_VertexAttribPointerFunc(vertAttrIndex, 3, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float*)0));
	GL_VertexAttribPointerFunc(texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float*)0) + 3);
	GL_VertexAttribPointerFunc(LMCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float*)0) + 5);

	// set uniforms
	GL_Uniform1iFunc(texLoc, 0);
	GL_Uniform1iFunc(LMTexLoc, 1);
	GL_Uniform1iFunc(fullbrightTexLoc, 2);
	GL_Uniform1iFunc(useFullbrightTexLoc, 0);
	GL_Uniform1iFunc(useOverbrightLoc, overbright);
	GL_Uniform1iFunc(useAlphaTestLoc, 0);
	GL_Uniform1iFunc(useCausticsTexLoc, 0);
	GL_Uniform1iFunc(useGrassLoc, 0); // woods #grass
	GL_Uniform1iFunc(useLightmapWideLoc, wide10bits);
	GL_Uniform1fFunc(alphaLoc, 1.0f);
	GL_Uniform1iFunc(useFullbrightTexLoc, 0);
	GL_Uniform1iFunc(useLightmapOnlyLoc, 1);
	GL_Uniform1iFunc(fogModeLoc, Fog_GetMode());

	R_ClearBatch();
	lastlightmap = -1;

	for (i = 0; i < model->numtextures; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

		if (t->texturechains[chain]->texinfo->flags & TEX_SPECIAL)
			continue; // unlit water

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (s->lightmaptexturenum < 0)
				continue;

			if (s->lightmaptexturenum != lastlightmap)
			{
				R_FlushBatch(ABOVE_WATER);

				GL_SelectTexture(GL_TEXTURE1);
				GL_Bind(lightmaps[s->lightmaptexturenum].texture);
				lastlightmap = s->lightmaptexturenum;
			}
			R_BatchSurface(s, ABOVE_WATER);

			rs_brushpasses++;
		}
	}

	R_FlushBatch(ABOVE_WATER);

	// clean up
	GL_DisableVertexAttribArrayFunc(vertAttrIndex);
	GL_DisableVertexAttribArrayFunc(texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc(LMCoordsAttrIndex);

	GL_UseProgramFunc(0);
	GL_SelectTexture(GL_TEXTURE0);
}

/*
=============
R_DrawWorld -- johnfitz -- rewritten
=============
*/
void R_DrawTextureChains (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	float entalpha;
	
	if (ent != NULL)
		entalpha = ENTALPHA_DECODE(ent->alpha);
	else
		entalpha = 1;

	R_UploadLightmaps ();

	if (r_drawflat_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		R_DrawTextureChains_Drawflat (model, chain);
		glEnable (GL_TEXTURE_2D);
		return;
	}

	if (r_fullbright_cheatsafe)
	{
		R_BeginTransparentDrawing (entalpha);
		R_DrawTextureChains_TextureOnly (model, ent, chain);
		R_EndTransparentDrawing (entalpha);
		goto fullbrights;
	}

	if (r_lightmap_cheatsafe)
	{
		if (r_world_program != 0)
		{
			R_DrawLightmapChains_GLSL(model, ent, chain);
			return;
		}

		if (!gl_overbright.value)
		{
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor3f(0.5, 0.5, 0.5);
		}
		R_DrawLightmapChains ();
		if (!gl_overbright.value)
		{
			glColor3f(1,1,1);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		R_DrawTextureChains_White (model, chain);
		return;
	}

	R_BeginTransparentDrawing (entalpha);

	R_DrawTextureChains_NoTexture (model, chain);

	// OpenGL 2 fast path
	if (r_world_program != 0)
	{
		R_EndTransparentDrawing (entalpha);
		
		R_DrawTextureChains_GLSL (model, ent, chain);
		if (chain != chain_world)
			R_DrawGrassBlades(model, ent, chain);
		return;
	}

	if (gl_overbright.value)
	{
		if (gl_texture_env_combine && gl_mtexable) //case 1: texture and lightmap in one pass, overbright using texture combiners
		{
			GL_EnableMultitexture ();
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_PREVIOUS_EXT);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_TEXTURE);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			GL_DisableMultitexture ();
			R_DrawTextureChains_Multitexture (model, ent, chain);
			GL_EnableMultitexture ();
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DisableMultitexture ();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else if (entalpha < 1) //case 2: can't do multipass if entity has alpha, so just draw the texture
		{
			R_DrawTextureChains_TextureOnly (model, ent, chain);
		}
		else //case 3: texture in one pass, lightmap in second pass using 2x modulation blend func, fog in third pass
		{
			//to make fog work with multipass lightmapping, need to do one pass
			//with no fog, one modulate pass with black fog, and one additive
			//pass with black geometry and normal fog
			Fog_DisableGFog ();
			R_DrawTextureChains_TextureOnly (model, ent, chain);
			Fog_EnableGFog ();
			glDepthMask (GL_FALSE);
			glEnable (GL_BLEND);
			glBlendFunc (GL_DST_COLOR, GL_SRC_COLOR); //2x modulate
			Fog_StartAdditive ();
			R_DrawLightmapChains ();
			Fog_StopAdditive ();
			if (Fog_GetDensity() > 0)
			{
				glBlendFunc(GL_ONE, GL_ONE); //add
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glColor3f(0,0,0);
				R_DrawTextureChains_TextureOnly (model, ent, chain);
				glColor3f(1,1,1);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
			glDepthMask (GL_TRUE);
		}
	}
	else
	{
		if (gl_mtexable) //case 4: texture and lightmap in one pass, regular modulation
		{
			GL_EnableMultitexture ();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DisableMultitexture ();
			R_DrawTextureChains_Multitexture (model, ent, chain);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else if (entalpha < 1) //case 5: can't do multipass if entity has alpha, so just draw the texture
		{
			R_DrawTextureChains_TextureOnly (model, ent, chain);
		}
		else //case 6: texture in one pass, lightmap in a second pass, fog in third pass
		{
			//to make fog work with multipass lightmapping, need to do one pass
			//with no fog, one modulate pass with black fog, and one additive
			//pass with black geometry and normal fog
			Fog_DisableGFog ();
			R_DrawTextureChains_TextureOnly (model, ent, chain);
			Fog_EnableGFog ();
			glDepthMask (GL_FALSE);
			glEnable (GL_BLEND);
			glBlendFunc(GL_ZERO, GL_SRC_COLOR); //modulate
			Fog_StartAdditive ();
			R_DrawLightmapChains ();
			Fog_StopAdditive ();
			if (Fog_GetDensity() > 0)
			{
				glBlendFunc(GL_ONE, GL_ONE); //add
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glColor3f(0,0,0);
				R_DrawTextureChains_TextureOnly (model, ent, chain);
				glColor3f(1,1,1);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
			glDepthMask (GL_TRUE);
		}
	}

	R_EndTransparentDrawing (entalpha);

fullbrights:
	if (gl_fullbrights.value)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
		glBlendFunc (GL_ONE, GL_ONE);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor3f (entalpha, entalpha, entalpha);
		Fog_StartAdditive ();
		R_DrawTextureChains_Glow (model, ent, chain);
		Fog_StopAdditive ();
		glColor3f (1, 1, 1);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable (GL_BLEND);
		glDepthMask (GL_TRUE);
	}

	if (chain != chain_world)
		R_DrawGrassBlades(model, ent, chain);
}

/*
=============
R_DrawWorld -- ericw -- moved from R_DrawTextureChains, which is no longer specific to the world.
=============
*/
void R_DrawWorld (void)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_DrawTextureChains (cl.worldmodel, NULL, chain_world);
#ifndef SDL_THREADS_DISABLED
	RSceneCache_Draw(false);
#endif
	R_DrawGrassBlades(cl.worldmodel, NULL, chain_world);
}

/*
=============
R_DrawWorld_Water -- ericw -- moved from R_DrawTextureChains_Water, which is no longer specific to the world.
=============
*/
void R_DrawWorld_Water (void)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_DrawTextureChains_Water (cl.worldmodel, NULL, chain_world);
#ifndef SDL_THREADS_DISABLED
	RSceneCache_Draw(true);
#endif
}

/*
=============
R_DrawWorld_ShowTris -- ericw -- moved from R_DrawTextureChains_ShowTris, which is no longer specific to the world.
=============
*/
void R_DrawWorld_ShowTris (void)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_DrawTextureChains_ShowTris (cl.worldmodel, chain_world);
}



#ifndef SDL_THREADS_DISABLED
/*
================
Scenecache stuff -- spike
Uses a worker thread to build an index buffer that can be thrown at the gpu.
Ignores frustum checks - the gpu can generally cull this faster than the main thread anyway.
Forces fatpvs on, to invisible walls popin/stutter.
Doesn't walk any leafs (per-frame), so can't use efrags. We instead just do a pvs check on each individually (should at least avoid poisoning the cache).

woods -- added #caustics support

================
*/
static struct
{	//I'm tagging things as commented-volatile to mark the things that we depend upon before the sdl lock/unlock/wait calls.
	SDL_Thread *thread;
	SDL_mutex *mutex;
	SDL_cond *wt_cond;
	SDL_cond *rt_cond;

	/*volatile*/ qboolean die;
	/*volatile*/ struct rscenecache_s *processing;
	/*volatile*/ qboolean processed;	//lightmaps need updating

	struct rscenecache_s *drawing;
	qboolean doingskybox;

	struct rscenecache_s
	{
		struct rscenecache_s *next;

		vec3_t pos;
		int hostframe;	//forget them if they get too old.
		qmodel_t *worldmodel;
		byte *pvs;

		byte *cachedsubmodels;	//one bit for each.
		unsigned int numcachedsubmodels;

		unsigned int brushpolys;
		unsigned int lightmaps;
		unsigned int numtextures;

		/*volatile*/ enum
		{
			SCS_BUILDING,
			SCS_COMPUTED,
			SCS_FINISHED,	//has an ebo.
			SCS_DISCARDED,
		} status;
		GLuint ebo;
		dlight_t dlights[countof(cl_dlights)];	//added this here so the cache at least gets consistent lighting without having to fight the main thread.
		double time;	//for killing old lights...
		struct rscenecachebath_s
		{
			unsigned int *idx;
			unsigned int *eboidx;
			size_t numidx;
			size_t maxidx;
		} batches[1];	//one per texturelm...
	} *cache;	//remember a few, for skyrooms or multiple-csqc-renderscenes etc. we need at least two - previous and pending
} rscenecache;
byte *skipsubmodels;


static void RSceneCache_RenderDynamicLightmaps (struct rscenecache_s *cache, msurface_t *fa, int dlightframecount)
{
	static entity_t r_worldentity;	//so the dlight stuff doesn't bug out.
	byte		*base;
	int			maps;
	glRect_t    *theRect;
	int smax, tmax;

	if (fa->flags & SURF_DRAWTILED) //johnfitz -- not a lightmapped surface
		return;

	// check for lightmap modification
	for (maps=0; maps < MAXLIGHTMAPS && fa->styles[maps] != INVALID_LIGHTSTYLE; maps++)
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	if (fa->dlightframe == dlightframecount	// dynamic this frame
		|| fa->cached_dlight)			// dynamic previously
	{
dynamic:
		if (r_dynamic.value)
		{
			struct lightmap_s *lm = &lightmaps[fa->lightmaptexturenum];
			lm->modified = true;
			theRect = &lm->rectchange;
			if (fa->light_t < theRect->t) {
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;
				theRect->t = fa->light_t;
			}
			if (fa->light_s < theRect->l) {
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;
				theRect->l = fa->light_s;
			}
			smax = fa->extents[0]+1;
			tmax = fa->extents[1]+1;
			if ((theRect->w + theRect->l) < (fa->light_s + smax))
				theRect->w = (fa->light_s-theRect->l)+smax;
			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t-theRect->t)+tmax;
			base = lm->pbodata;
			base += fa->light_t * LMBLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
			R_BuildLightMap (cache->worldmodel, fa, base, LMBLOCK_WIDTH*lightmap_bytes, &r_worldentity, dlightframecount, cache->dlights);
		}
	}
}

static unsigned short rscenecache_used_lightstyles[MAX_LIGHTSTYLES];
static int rscenecache_num_used_lightstyles;
static qmodel_t *rscenecache_lightstyle_model;

static void RSceneCache_ResetLightstyleTracking(qmodel_t *mod)
{
	if (!mod || rscenecache_lightstyle_model == mod)
	{
		rscenecache_lightstyle_model = NULL;
		rscenecache_num_used_lightstyles = 0;
	}
}

static qboolean RSceneCache_UsedLightstylesChanged(const int *old_vals, const int *new_vals)
{
	msurface_t *surf;
	int i, j;

	// Compare only styles referenced by world surfaces, but preserve the
	// force-rebuild sentinel used by RSceneCache_Queue.
	if (old_vals[0] == INT_MIN)
		return true;

	if (rscenecache_lightstyle_model != cl.worldmodel)
	{
		byte seen[(MAX_LIGHTSTYLES + 7) / 8];

		memset(seen, 0, sizeof(seen));
		rscenecache_num_used_lightstyles = 0;
		rscenecache_lightstyle_model = cl.worldmodel;

		for (i = 0, surf = cl.worldmodel->surfaces; i < cl.worldmodel->numsurfaces; i++, surf++)
		{
			for (j = 0; j < MAXLIGHTMAPS && surf->styles[j] != INVALID_LIGHTSTYLE; j++)
			{
				unsigned short style = surf->styles[j];

				if (style >= MAX_LIGHTSTYLES)
					continue;
				if (seen[style >> 3] & (1u << (style & 7)))
					continue;

				seen[style >> 3] |= (1u << (style & 7));
				rscenecache_used_lightstyles[rscenecache_num_used_lightstyles++] = style;
			}
		}
	}

	for (i = 0; i < rscenecache_num_used_lightstyles; i++)
		if (old_vals[rscenecache_used_lightstyles[i]] != new_vals[rscenecache_used_lightstyles[i]])
			return true;

	return false;
}

static qboolean RSceneCache_HasActiveDlights(const dlight_t *lights, size_t count, double time)
{
	size_t i;

	for (i = 0; i < count; ++i)
	{
		if (lights[i].die < time || !lights[i].radius)
			continue;
		return true;
	}

	return false;
}

static int RSceneCache_Thread(void *ctx)
{
	unsigned int i, j, e;
	mleaf_t *leaf;
	msurface_t **mark, *surf;
	struct rscenecache_s *cache;
	byte *vis;
	unsigned int bpolys;
	unsigned int clusters;
	unsigned int *idx;
	size_t numidx;
	struct rscenecachebath_s *batch;
	mmodel_t *sub;

	SDL_LockMutex(rscenecache.mutex);
	SDL_CondSignal(rscenecache.rt_cond);	//wake the parent thread. its waiting for us.
	while (!rscenecache.die)
	{
		if (!rscenecache.processing)	//might have been posted+signaled to us while we were busy on the last one.
			SDL_CondWait(rscenecache.wt_cond, rscenecache.mutex);
		cache = rscenecache.processing;
		rscenecache.processing = NULL;	//accepted!
		SDL_UnlockMutex(rscenecache.mutex);
		if (cache)
		{
			int visframecount = r_visframecount;
			int dlightframecount = r_framecount;

			if (!gl_flashblend.value)
				for (j = 0; j < countof(cache->dlights); j++)
				{
					if ((cache->dlights[j].die < cache->time) ||
						(!cache->dlights[j].radius))
						continue;
					//FIXME: no model context passed
					R_MarkLights (&cache->dlights[j], cache->dlights[j].origin, dlightframecount, j, cache->worldmodel->nodes);
				}

			bpolys = 0;
			vis = cache->pvs;
			leaf = &cache->worldmodel->leafs[1];
			clusters = cache->worldmodel->numleafs;
			for (i=0 ; i<clusters ; i++, leaf++)
			{
				if (vis[i>>3] & (1<<(i&7)))
				{
					if (leaf->contents != CONTENTS_SKY || r_oldskyleaf.value)
						for (j=0, mark = leaf->firstmarksurface; j<(unsigned int)leaf->nummarksurfaces; j++, mark++)
						{
							surf = *mark;
							if (surf->visframe != visframecount)
							{
								surf->visframe = visframecount;

								bpolys++;
								if (surf->numedges < 3)
									continue;	//ignore any buggy degenerate ones.
								if ((unsigned)(surf->lightmaptexturenum+1) >= cache->lightmaps)
									continue;	//wtf
								if (!surf->texinfo) { // material sanity – guard against NULL or out-of-range
									Con_DPrintf("RSceneCache: surface %ld has NULL texinfo – skipping\n",
										(long)(surf - cache->worldmodel->surfaces));
									continue;
								}
								if ((unsigned int)surf->texinfo->materialidx >= cache->numtextures)
									continue;	//should have been sanitised at load.
								numidx = (surf->numedges-2)*3;
								int uw = (surf->flags & SURF_UNDERWATER) ? 1 : 0;
								batch = &cache->batches[
								        surf->texinfo->materialidx * cache->lightmaps * 2   /* texture bank  */
								      + uw                    * cache->lightmaps            /* above / under */
								      + (1 + surf->lightmaptexturenum)];                    /* lightmap slot */
								if (batch->numidx+numidx > batch->maxidx)
								{
									void *new_idx;
									batch->maxidx = batch->numidx+numidx + 4096;	//overestimate, because why not
									new_idx = realloc(batch->idx, sizeof(*batch->idx)*batch->maxidx);
									if (!new_idx) {
										Con_Printf("RSceneCache_Thread: Failed to realloc index buffer\n");
										continue; // Skip this surface if allocation fails
								}
									batch->idx = new_idx;
								}
								idx = &batch->idx[batch->numidx];
								batch->numidx += numidx;
								for (e = 2; e < (unsigned int)surf->numedges; e++)
								{
									*idx++ = surf->vbo_firstvert;
									*idx++ = surf->vbo_firstvert + e-1;
									*idx++ = surf->vbo_firstvert + e;
								}

								RSceneCache_RenderDynamicLightmaps(cache, surf, dlightframecount);
							}
						}
				}
			}

			for (i = 0; i < cache->numcachedsubmodels; i++)
			{
				if (!(cache->cachedsubmodels[i>>3]&(1u<<(i&7))))
					continue;	//not needed.
				sub = &cache->worldmodel->submodels[i];

				if (!gl_flashblend.value)
					for (j = 0; j < countof(cache->dlights); j++)
					{
						if ((cache->dlights[j].die < cache->time) ||
							(!cache->dlights[j].radius))
							continue;
						//FIXME: no model context passed
						R_MarkLights (&cache->dlights[j], cache->dlights[j].origin, dlightframecount, j, cache->worldmodel->nodes + sub->headnode[0]);
					}

				//FIXME: these should really use MultiDrawIndirect, so we can add/remove them more cheaply.
				for (j=0, surf = cache->worldmodel->surfaces+sub->firstface; j<(unsigned int)sub->numfaces; j++, surf++)
				{	//don't bother with visframe checks here. a) we shouldn't be getting dupes anyway. b) we don't want to trip up the regular rendering if its rendering a moving copy while we're generating a new cache.
					bpolys++;
					if (surf->numedges < 3)
						continue;	//ignore any buggy degenerate ones.
					if ((unsigned)(surf->lightmaptexturenum+1) >= cache->lightmaps)
						continue;	//wtf
					if ((unsigned int)surf->texinfo->materialidx >= cache->numtextures)
						continue;	//should have been sanitised at load.
					numidx = (surf->numedges-2)*3;
					int uw = (surf->flags & SURF_UNDERWATER) ? 1 : 0;
					batch = &cache->batches[
					        surf->texinfo->materialidx * cache->lightmaps * 2   /* texture bank  */
					      + uw                    * cache->lightmaps            /* above / under */
					      + (1 + surf->lightmaptexturenum)];                    /* lightmap slot */
					if (batch->numidx+numidx > batch->maxidx)
					{
						void *new_idx;
						batch->maxidx = batch->numidx+numidx + 4096;	//overestimate, because why not
						new_idx = realloc(batch->idx, sizeof(*batch->idx)*batch->maxidx);
						if (!new_idx) {
							Con_Printf("RSceneCache_Thread: Failed to realloc submodel index buffer\n");
							continue; // Skip this surface if allocation fails
						}
						batch->idx = new_idx;
					}
					idx = &batch->idx[batch->numidx];
					batch->numidx += numidx;
					for (e = 2; e < (unsigned int)surf->numedges; e++)
					{
						*idx++ = surf->vbo_firstvert;
						*idx++ = surf->vbo_firstvert + e-1;
						*idx++ = surf->vbo_firstvert + e;
					}

					RSceneCache_RenderDynamicLightmaps(cache, surf, dlightframecount);
				}
			}

			cache->brushpolys = bpolys;

			SDL_LockMutex(rscenecache.mutex);
			rscenecache.processed = true;
			cache->status = SCS_COMPUTED;
			SDL_CondSignal(rscenecache.rt_cond);
		}
		else
			SDL_LockMutex(rscenecache.mutex);
	}
	SDL_UnlockMutex(rscenecache.mutex);
	return 0;
}
static qboolean RSceneCache_Queue(byte *vis)
{
	extern GLuint gl_bmodel_vbo;
//	int type = 0;
	struct rscenecache_s *cache, *best = NULL, *building;
	float bdist=FLT_MAX, d;	//bdist should match fatpvs size, so we don't have invisible walls.
	vec3_t offset;
	unsigned int rowbytes = (cl.worldmodel->numleafs+7)>>3;
	int e;
	qboolean grass_blades_active;
	static int settingconflict;

	static int old_lightstylevalue[countof(d_lightstylevalue)];
	byte *bakesubmodels;

	skipsubmodels = NULL;
	rscenecache.drawing = NULL;	//still need to figure out which cache to use.
	if (!*r_scenecache.string)
		r_scenecache.value = 1;	//consistency with FTE's 'auto' seting.
	if (!r_scenecache.value)
	{
		settingconflict = -1;

		if (rscenecache.thread)
			RSceneCache_Shutdown();
		return false;
	}
	else if (r_fullbright_cheatsafe || r_lightmap_cheatsafe || r_drawflat_cheatsafe)
	{	//r_drawflat cannot possibly work with this. we do not track how many tris there were per surface so you'd be colouring tris rather than surfs, but maybe that's whats actually important... anyway, debug features don't need to be fast. NOTE: QuakeWorld engines have a different interpretation of drawflat - showing block colours based on surface angles, which could be done via glsl, but its not really an nq/qs thing so just use the legacy path.
		//r_fullbright could just use a white texture, or glsl, but its ugly and doesn't deserve to be fast!..
		//r_lightmap would want to force the glsl, could be generic, but its a debug feature that we don't really care about.
		if (settingconflict!=true)
			settingconflict=true, Con_Printf("r_scenecache: Disabling due to conflicting settings\n");

		if (rscenecache.thread)
			RSceneCache_Shutdown();
		return false;
	}
	//Note: r_dynamic is meant to work, but doesn't update as fast as you'd like (eg dlights).
	else if (settingconflict!=false)
		settingconflict=false, Con_DPrintf("r_scenecache: Enabled\n");

	//we're not walking leafs here, so we need to handle static ents specially. and before the following loop...
	for (e = 0; e < cl.num_statics; e++)
	{
		struct cl_static_entities_s *test = &cl.static_entities[e];
		entity_t *pent = test->ent;

		if (pent->model && cl_numvisedicts < cl_maxvisedicts)
		{
			if (CL_CTFPugSwapEntityModel(pent))
				CL_LinkStaticEnt(test);

			if (test->num_clusters<=MAX_ENT_LEAFS)
			{
				unsigned int i;
				for (i=0 ; i < test->num_clusters ; i++)
					if (vis[test->clusternums[i] >> 3] & (1 << (test->clusternums[i]&7) ))
						break;
				if (i == test->num_clusters)
					continue;	//not visible.
			}//else too many clusters, we were not tracking this ent properly. assume its visible and hope frustum checks later will stop it... they ARE frustum checked, right?

			if (R_CullBox(test->absmin, test->absmax))
				continue;

#ifdef PSET_SCRIPT
			if (pent->netstate.emiteffectnum > 0)
			{
				float t = cl.time-cl.oldtime;
				vec3_t axis[3];
				if (t < 0) t = 0; else if (t > 0.1) t= 0.1;
				AngleVectors(pent->angles, axis[0], axis[1], axis[2]);
				if (pent->model->type == mod_alias)
					axis[0][2] *= -1;	//stupid vanilla bug
				PScript_RunParticleEffectState(pent->origin, axis[0], t, cl.particle_precache[pent->netstate.emiteffectnum].index, &pent->emitstate);
			}
			else if (pent->model->emiteffect >= 0)
			{
				float t = cl.time-cl.oldtime;
				vec3_t axis[3];
				if (t < 0) t = 0; else if (t > 0.1) t= 0.1;
				AngleVectors(pent->angles, axis[0], axis[1], axis[2]);
				if (pent->model->flags & MOD_EMITFORWARDS)
				{
					if (pent->model->type == mod_alias)
						axis[0][2] *= -1;	//stupid vanilla bug
				}
				else
					VectorScale(axis[2], -1, axis[0]);
				PScript_RunParticleEffectState(pent->origin, axis[0], t, pent->model->emiteffect, &pent->emitstate);
				if (pent->model->flags & MOD_EMITREPLACE)
					continue;
			}
#endif
			cl_visedicts[cl_numvisedicts++] = pent;
		}
	}

	//okay, now figure out which bmodels we can bake into the cache
	bakesubmodels = alloca((cl.worldmodel->numsubmodels+7)>>3);
	memset(bakesubmodels, 0, (cl.worldmodel->numsubmodels+7)>>3);
	grass_blades_active = R_GrassBladesActive();
	if (r_scenecache.value != 2 && r_drawentities.value)
	for (e = 0; e < cl_numvisedicts; e++)
	{
		entity_t *ent = cl_visedicts[e];
		size_t m;
		if (!ent->model || ent->model->submodelof != cl.worldmodel ||	//we only want submodels of the world here.
			ent->origin[0]||ent->origin[1]||ent->origin[2] ||	//can only bake them if they're in the identity position. :(
			ent->angles[0]||ent->angles[1]||ent->angles[2] ||	//and not rotated
			(ent->eflags&EFLAGS_VIEWMODEL) ||	//viewmodel etc screws with origins.
			ent->frame ||	//don't bother tracking toggled textures here.
			ent->alpha!=0 ||	//transparent stuff would need extra batches, which gets awkward and misordered.
			ent->effects)	//weird stuff like EF_ADDITIVE/EF_FULLBRIGHT. probably not used on submodels anyway.
			continue;	//nope, can't bake it.
		if (grass_blades_active && R_GrassEntityAllowsGrass(ent) &&
			R_GrassPresenceCacheHasBladeSurfaces(R_GrassGetPresenceCache(ent->model, true)))
			continue;	//keep grass-bearing bmodels in the normal entity path.
		//okay, we want to bake this one.
		m = ent->model->submodelidx;
		bakesubmodels[m>>3] |= (1u<<(m&7));
	}

	for (building = NULL, cache = rscenecache.cache; cache; cache = cache->next)
	{
		if (cache->worldmodel != cl.worldmodel)
		{	//this cache is completely unsuitable.
			if (cache->status == SCS_BUILDING)
				building = cache;
			continue;
		}

		if (!memcmp(cache->pvs, vis, rowbytes))
		{	//pvs matches. yay. we *could* check leaf, but that wouldn't handle detail brushes properly.
			VectorCopy(r_origin, cache->pos);	//might as well keep its origin updated, so we don't block needlessly, but only when its actually valid.
			if (cache->status == SCS_BUILDING)
			{	//its perfect so there's no point building it, but we still can't use it yet, so keep looking for one we CAN use.
				building = cache;
				if (!best)
					best = cache;
				continue;
			}
			else
			{	//we're in the right leaf, so yay?
				if (!memcmp(cache->cachedsubmodels, bakesubmodels, (cl.worldmodel->numsubmodels+7)>>3))
				{	//this one's perfect.
					best = cache;
					bdist = 0;
					break;
				}
				else
				{
					if (bdist > 100)
					{
						best = cache;
						bdist = 100;
					}
					continue;	//might have one with the correct submodels...
				}
			}
		}

		if (cache->status == SCS_BUILDING)
		{
			building = cache;
			continue;	//can't be better if we're not able to use it yet... we'll block building a new one though.
		}

		VectorSubtract(r_origin, cache->pos, offset);
		d = DotProduct(offset,offset);
		if (memcmp(cache->cachedsubmodels, bakesubmodels, (cl.worldmodel->numsubmodels+7)>>3))
			d += 100;
		if (d < bdist)
			bdist = d, best = cache;
	}

	//check if there's one building already (don't want to queue too many)
	if (!building && best)
		for (building = best; building && building->status != SCS_BUILDING; building = building->next)
			;

	if (!r_dynamic.value)
		old_lightstylevalue[0] = INT_MIN;	//something that'll force a regen pretty soon...
	else
	{
		if (!building)
		{	//if the lighting is changing then keep rebuilding,
			if (RSceneCache_UsedLightstylesChanged(old_lightstylevalue, d_lightstylevalue))
			{
				old_lightstylevalue[0] = INT_MIN;	//something that'll force a regen pretty soon...
				cache = NULL;	//make sure its rebuilt (can still use the best while it computes).
			}
			else if (r_dynamic.value)
			{
				qboolean have_active_dlights = false;
				dlight_t *l = cl_dlights;
				size_t i;

				for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
				{
					if (l->spawn > cl.mtime[0] && cls.demoplayback) // woods (iw) #democontrols
					{
						l->die = 0.f;
						continue;
					}
					
					if (l->die < cl.time || !l->radius)
						continue;

					have_active_dlights = true;
					cache = NULL;
					break;
				}

				if (!have_active_dlights && best &&
					RSceneCache_HasActiveDlights(best->dlights, countof(best->dlights), best->time))
					cache = NULL;
			}
		}
	}

	if (!best || (!cache && !building))
	{	//no perfect matches. build a new one.
		struct rscenecache_s *oldest = NULL;
		unsigned int oldestage = 3, a;

		memcpy(old_lightstylevalue, d_lightstylevalue, sizeof(old_lightstylevalue));

		SDL_LockMutex(rscenecache.mutex);
		if(rscenecache.processing)
		{	//we already had one queued? don't wait for TWO frames!
			rscenecache.processing->status = SCS_DISCARDED;
			rscenecache.processing = NULL;
		}
		SDL_UnlockMutex(rscenecache.mutex);

		for (cache = rscenecache.cache; cache; cache = cache->next)
		{
			if (cache->status == SCS_BUILDING ||	//worker still has it.
				cache == best)						//we're falling back on it...
				continue;

			if (cache->lightmaps != lightmap_count+1 ||
				cache->numtextures != cl.worldmodel->numtextures)
				continue;	//allocation sizes changed...

			if (cache->status == SCS_DISCARDED)
			{	//this one is fine.
				oldest = cache;
				break;
			}

			a = host_framecount-cache->hostframe;	//keep it current
			if (a >= oldestage)
				a = oldestage, oldest = cache;
		}

		if (oldest)
		{	//we found an old one, yay us.
			struct rscenecache_s **link;
			cache = oldest;
			for (link = &rscenecache.cache; *link; )
			{
				if (*link == cache)
				{
					*link = cache->next;	//unlink it...
					cache->next = rscenecache.cache;	//and relink at head so its favoured.
					rscenecache.cache = cache;
					break;
				}
				link = &(*link)->next;
			}

			unsigned int e;
			for (e = 0; e < cache->numtextures*cache->lightmaps*2; e++)
				cache->batches[e].numidx = 0;
		}
		else
		{	//allocate some new memory for it.
			cache = calloc(1,
				sizeof(*cache)-sizeof(cache->batches) +	//base structure
				sizeof(*cache->batches)*cl.worldmodel->numtextures*(lightmap_count+1)*2 +	//trailing batch count...
				rowbytes +	//pvs info thrown onto the end of the allocation because why not.
				((cl.worldmodel->numsubmodels+7)>>3));
					//link it, cos we might as well.
			cache->next = rscenecache.cache;
			rscenecache.cache = cache;

			cache->lightmaps = lightmap_count+1;	//FIXME use texture arrays for the lightmaps, keep this at 2.
			cache->numtextures = cl.worldmodel->numtextures;	//FIXME: merge textures into same-dimensions arrays
			cache->pvs = (byte*)&cache->batches[cache->numtextures*cache->lightmaps*2];
			cache->worldmodel = cl.worldmodel;
			cache->cachedsubmodels = cache->pvs + rowbytes;
			cache->numcachedsubmodels = cl.worldmodel->numsubmodels;
		}

		cache->status = SCS_BUILDING;
		VectorCopy(r_origin, cache->pos);	//might as well overwrite its origin
		cache->hostframe = host_framecount;
		memcpy(cache->pvs, vis, rowbytes);
		memcpy(cache->cachedsubmodels, bakesubmodels, ((cl.worldmodel->numsubmodels+7)>>3));
		memcpy(cache->dlights, cl_dlights, sizeof(cache->dlights));
		cache->time = cl.time;

		//create the worker if it doesn't exist...
		if (!rscenecache.thread)
		{
			rscenecache.die = false;	//just in case...
			rscenecache.mutex = SDL_CreateMutex();
			rscenecache.wt_cond = SDL_CreateCond();
			rscenecache.rt_cond = SDL_CreateCond();
			SDL_LockMutex(rscenecache.mutex);
			rscenecache.thread = SDL_CreateThread(RSceneCache_Thread, "scenecache", NULL);
			if (!rscenecache.thread)
			{
				r_scenecache.value = 0;	//force it off...
				RSceneCache_Shutdown();
				return false;
			}
			SDL_CondWait(rscenecache.rt_cond, rscenecache.mutex);
			SDL_UnlockMutex(rscenecache.mutex);
			//the thread is now at a known position.
		}

		//get the worker to start processing it
		SDL_LockMutex(rscenecache.mutex);
		//oh noes! its processing something else and we have no other queue!
		while(rscenecache.processing)
		{
//			double t = Sys_DoubleTime();
			SDL_CondWait(rscenecache.rt_cond, rscenecache.mutex);
//			t = Sys_DoubleTime()-t;
//			Con_Printf("Scenecache prewait (%f)\n", t*1000);
		}
		rscenecache.processing = cache;
		SDL_CondSignal(rscenecache.wt_cond);
		SDL_UnlockMutex(rscenecache.mutex);
	}
	if (best)
	{
//		if (best->status == SCS_BUILDING)
//			Con_Printf("Scenecache is gonna wait\n");
		cache = best;
	}
	if (!cache)
	{	//this should be unreachable...
		if (rscenecache.thread)
			RSceneCache_Shutdown();
	}
	else
		cache->hostframe = host_framecount;	//keep it current

	rscenecache.drawing = cache;
	rscenecache.doingskybox = false;

	return !!cache;
}
static void RSceneCache_Uncache(struct rscenecache_s *cache)
{
	size_t i;
	if (cache->status == SCS_BUILDING)
	{
		SDL_LockMutex(rscenecache.mutex);
		while(cache->status == SCS_BUILDING)	//thread still has it...
			SDL_CondWait(rscenecache.rt_cond, rscenecache.mutex);
		SDL_UnlockMutex(rscenecache.mutex);
	}
	if (rscenecache.drawing == cache)
		rscenecache.drawing = NULL;
	for (i = 0; i < cache->numtextures*cache->lightmaps*2; i++)
		if (cache->batches[i].idx)
			free(cache->batches[i].idx);
	GL_DeleteBuffersFunc(1, &cache->ebo);
	free(cache);
}
void RSceneCache_Cleanup(qmodel_t *mod)
{
	struct rscenecache_s **link, *cache;

	RSceneCache_ResetLightstyleTracking(mod);

	for (link = &rscenecache.cache; (cache=*link); )
	{
		if (cache->worldmodel == mod)
		{
			*link = cache->next;
			RSceneCache_Uncache(cache);
		}
		else
			link = &cache->next;
	}
}
static void RSceneCache_Finish(struct rscenecache_s *cache)
{
#define USEMAPBUFFER
	unsigned int i;
	size_t numidx;
#ifdef USEMAPBUFFER
	byte *ebomem = NULL;
#endif
	switch(cache->status)
	{
	case SCS_BUILDING:
		//worker is still computing it... block while waiting for it.
		SDL_LockMutex(rscenecache.mutex);
		while(cache->status == SCS_BUILDING)
		{
//			double t = Sys_DoubleTime();
			SDL_CondWait(rscenecache.rt_cond, rscenecache.mutex);
//			t = Sys_DoubleTime()-t;
//			Con_Printf("Scenecache postwait (%f)\n", t*1000);
		}
		SDL_UnlockMutex(rscenecache.mutex);
		//fallthrough
	case SCS_COMPUTED:
		//worker thread finished, but GL threading issues mean it didn't build our EBO (which can be a significant boost)
		if (gl_vbo_able)
		{
			for (i = 0, numidx = 0; i < cache->numtextures*cache->lightmaps*2; i++)
				numidx += cache->batches[i].numidx;

			if (!cache->ebo)
				GL_GenBuffersFunc(1, &cache->ebo);
			GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, cache->ebo); // indices come from client memory!
			GL_BufferDataFunc(GL_ELEMENT_ARRAY_BUFFER, numidx*sizeof(unsigned int), NULL,  GL_STATIC_DRAW);
#ifdef USEMAPBUFFER
			ebomem = GL_MapBufferFunc(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
#endif
		}
		for (i = 0, numidx = 0; i < cache->numtextures*cache->lightmaps*2; i++)
		{
			if (gl_vbo_able)
			{
				cache->batches[i].eboidx = (unsigned int*)(numidx*sizeof(*cache->batches[i].idx));
#ifdef USEMAPBUFFER
				memcpy(ebomem+(uintptr_t)cache->batches[i].eboidx, cache->batches[i].idx, cache->batches[i].numidx*sizeof(*cache->batches[i].idx));
#else
				GL_BufferSubDataFunc(GL_ELEMENT_ARRAY_BUFFER, numidx*sizeof(*cache->batches[i].idx), cache->batches[i].numidx*sizeof(*cache->batches[i].idx), cache->batches[i].idx);
#endif
				//leave the memory allocated to avoid all the reallocs if it gets reused. the cache will still need freeing later anyway.
			}
			else
				cache->batches[i].eboidx = cache->batches[i].idx;	//lame
			numidx += cache->batches[i].numidx;
		}
#ifdef USEMAPBUFFER
		if (gl_vbo_able)
			GL_UnmapBufferFunc(GL_ELEMENT_ARRAY_BUFFER);
#endif
		cache->status = SCS_FINISHED;

		for (i=0, cache = rscenecache.cache; cache; cache = cache->next)
			i++;
		break;
	case SCS_FINISHED:
	case SCS_DISCARDED:	//shouldn't be here...
		break;
	}

	if (rscenecache.processed)
	{	//make sure lightmaps are updated when we can.
		rscenecache.processed = false;
		lightmaps_skipupdates = false;
		R_UploadLightmaps();
		lightmaps_skipupdates = true;
	}
}
static void RSceneCache_Draw(qboolean water)
{
	extern GLuint gl_bmodel_vbo;
	struct rscenecache_s *cache = rscenecache.drawing;
	unsigned int i, j;
	texture_t *tex;
	int b;
	int mode;
	int lastprog = -1;
	float alpha = 0;
	const int overbright = !!gl_overbright.value;
	const int wide10bits = (gl_lightmap_format == GL_RGB10_A2);
	const float lightmapscale = (overbright ? 2.0f : 1.0f) * (wide10bits ? 4.0f : 1.0f);

	if (!cache)
	{
		skipsubmodels = NULL;
		return;
	}
	RSceneCache_Finish(cache);
	skipsubmodels = cache->cachedsubmodels;

	glDepthMask(GL_TRUE);
	glDisable (GL_BLEND);
	if (skyroom_drawn)
	{	//draw skies first, so we don't end up drawing overlapping non-skies behind
		glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
		RSceneCache_DrawSkySurfDepth();
		glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
	}

	GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, cache->ebo); // indices come from client memory!

	GL_EnableVertexAttribArrayFunc (vertAttrIndex);
	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (LMCoordsAttrIndex);

	GL_VertexAttribPointerFunc (vertAttrIndex,      3, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0));
	GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 3);
	GL_VertexAttribPointerFunc (LMCoordsAttrIndex,  2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 5);

	rs_brushpolys += cache->brushpolys; //for r_speeds.;

	if (gl_caustics.value > 0 && underwatertexture) // Bind caustics texture once if enabled
	{
		GL_SelectTexture(GL_TEXTURE3);
		GL_Bind(underwatertexture);
		GL_SelectTexture(GL_TEXTURE0); // Switch back to default texture unit 0
	}

	for (i = 0; i < cache->numtextures; i++)
	{
		if (!cache->worldmodel->textures[i])
			continue;	//stupid buggy shite.
		if ((cache->worldmodel->textures[i]->name[0] == '*') != water)
			continue;
		b = false;
		for (j = 0; j < cache->lightmaps * 2; j++)
		{
			int uw      = (j >= cache->lightmaps);   // 0 = above, 1 = under
			int lm_slot = j % cache->lightmaps;      // 0 = unlit, 1… = lightmap N

			if (!cache->batches[i*cache->lightmaps*2 + j].numidx)
				continue;	//don't waste time on it.

			if (!b)
			{
				b = true;
				tex = R_TextureAnimation (cache->worldmodel->textures[i], 0);
				GL_SelectTexture (GL_TEXTURE0);
				GL_Bind(tex->gltexture);

				//its annoying how we don't know any surface flags here
				if (*tex->name == '*')
				{
					if (lm_slot>0) // changed j to lm_slot
					{	//lit
						GL_EnableVertexAttribArrayFunc (LMCoordsAttrIndex);
						mode = 1;
					}
					else	//unlit
					{
						GL_DisableVertexAttribArrayFunc (LMCoordsAttrIndex);
						mode = 0;
					}

					// detect special liquid types. stoopid lack of surface flag info. :(
					if (!strncmp (cache->worldmodel->textures[i]->name+1, "lava", 4))
						alpha = map_lavaalpha > 0 ? map_lavaalpha : map_fallbackalpha;
					else if (!strncmp (cache->worldmodel->textures[i]->name+1, "slime", 5))
						alpha = map_slimealpha > 0 ? map_slimealpha : map_fallbackalpha;
					else if (!strncmp (cache->worldmodel->textures[i]->name+1, "tele", 4))
						alpha = map_telealpha > 0 ? map_telealpha : map_fallbackalpha;
					else
						alpha = map_wateralpha;// > 0 ? map_wateralpha : map_fallbackalpha;

					if (alpha < 1.0f)
					{
						glDepthMask (GL_FALSE);
						glEnable (GL_BLEND);
					}
					else
					{
						glDepthMask (GL_TRUE);
						glDisable (GL_BLEND);
					}

					if (lastprog != r_water[mode].program)
					{
						lastprog = r_water[mode].program;
						GL_UseProgramFunc (r_water[mode].program);
						GL_Uniform1fFunc (r_water[mode].time, cl.time);
						GL_Uniform1iFunc (r_water[mode].fogmode, Fog_GetMode());
						if (r_water[mode].light_scale != -1)
							GL_Uniform1fFunc (r_water[mode].light_scale, lightmapscale);
					}
					GL_Uniform1fFunc (r_water[mode].alpha_scale, alpha);
				}
				else if (tex->name[0]=='s'&&tex->name[1]=='k'&&tex->name[2]=='y')
				{
					//sky. because why not.
					extern cvar_t r_skyalpha, r_skyfog, r_fastsky;
					extern float skyflatcolor[3];
					float skyfog = 0.0f;
					float *fogcolor;
					if (r_fastsky.value == 1)  // woods -- #fastsky2
						mode = 3;
					else if (r_fastsky.value == 2)
						mode = (skybox_name[0] || externalskyloaded) ? 2 : 3;
					else
						mode = 2;

					if (rscenecache.doingskybox)
						break;	//we're doing skies weirdly. FIXME: replace with cubemap skies, where possible.

					if (Fog_GetGlobalDensity() > 0.0f)
						skyfog = CLAMP(0.0f, r_skyfog.value, 1.0f);
					fogcolor = Fog_GetGlobalColor();

					GL_SelectTexture (GL_TEXTURE2);
					GL_Bind(tex->fullbright);

					if (skyroom_drawn)
						continue;	//already drew them
					else if (lastprog != r_water[mode].program)
					{
						lastprog = r_water[mode].program;
						GL_UseProgramFunc (r_water[mode].program);
						GL_Uniform1fFunc (r_water[mode].time, Sky_GetTime()); // woods #skyspeed

						GL_Uniform1fFunc (r_water[mode].alpha_scale, r_skyalpha.value);
						GL_Uniform3fFunc (r_water[mode].eyepos, r_origin[0], r_origin[1], r_origin[2]);
						GL_Uniform1fFunc (r_water[mode].fogalpha, skyfog);
						if (r_water[mode].skyfogcolor != -1)
							GL_Uniform3fFunc (r_water[mode].skyfogcolor, fogcolor[0], fogcolor[1], fogcolor[2]);
						if (r_water[mode].colour != (GLuint)-1)
							GL_Uniform3fFunc (r_water[mode].colour, skyflatcolor[0], skyflatcolor[1], skyflatcolor[2]);
					}
				}
				else
				{
					if (lastprog != r_world_program)
					{
						lastprog = r_world_program;
						GL_UseProgramFunc (r_world_program);
						GL_Uniform1iFunc (useOverbrightLoc, overbright);
						GL_Uniform1iFunc (useLightmapWideLoc, wide10bits);
						GL_Uniform1iFunc (useLightmapOnlyLoc, 0);
						GL_Uniform1fFunc (alphaLoc, 1);			//worldmodel is never translucent.
						GL_Uniform1fFunc (grassAmountLoc, R_GrassAmount()); // woods #grass
						GL_Uniform1fFunc (grassTimeLoc, R_GrassAnimTime()); // woods #grass
						GL_Uniform1fFunc (grassMovementLoc, R_GrassMovement()); // woods #grass
						GL_Uniform1iFunc (fogModeLoc, Fog_GetMode());
						R_SetGrassColorUniforms(NULL); // woods #grass

						GL_Uniform1fFunc(clTimeLoc, cl.time);
						if (gl_caustics.value > 0 && underwatertexture)
						{
						   GL_Uniform1iFunc(causticsTexLoc, 3); // Tell shader caustics are on unit 3
						   GL_Uniform1fFunc(causticsOpacityLoc, gl_caustics.value);
						} else {
						   // Explicitly disable if necessary, though useCausticsTexLoc should handle it
						   // GL_Uniform1iFunc(causticsTexLoc, 0); // Maybe set to a dummy texture/unit?
						}
					}
					GL_Uniform1iFunc (useAlphaTestLoc, *tex->name == '{');	//update alphatest. some future qbsps might actually support it properly on the worldmodel. plus there's lots of buggy bsps where it was used anyway.
					if (R_TextureUsesSurfaceGrass(cache->worldmodel->textures[i]))
					{
						GL_Uniform1iFunc (useGrassLoc, 1); // woods #grass
						R_SetGrassColorUniforms(tex); // woods #grass
					}
					else
						GL_Uniform1iFunc (useGrassLoc, 0); // woods #grass

					if (tex->fullbright && gl_fullbrights.value)
					{
						GL_Uniform1iFunc (useFullbrightTexLoc, 1);
						GL_SelectTexture (GL_TEXTURE2);
						GL_Bind(tex->fullbright);
					}
					else
					{
						GL_Uniform1iFunc (useFullbrightTexLoc, 0);
						//don't bother unbinding. the glsl won't use it anyway.
					}
				}
			}

			GL_Uniform1iFunc(useCausticsTexLoc, gl_caustics.value > 0 && underwatertexture && uw);

			GL_SelectTexture (GL_TEXTURE1);
			if (lm_slot)
			    GL_Bind(lightmaps[lm_slot-1].texture);
			else
			    GL_Bind(NULL);   /* unlit */

			glDrawElements(GL_TRIANGLES, cache->batches[i*cache->lightmaps*2 + j].numidx, GL_UNSIGNED_INT, cache->batches[i*cache->lightmaps*2 + j].eboidx);
			rs_brushpasses++;
		}
	}

	if (gl_caustics.value > 0 && underwatertexture) // Unbind caustics texture once after all batches if it was bound
	{
		GL_SelectTexture(GL_TEXTURE3);
		GL_Bind(NULL);
		GL_SelectTexture(GL_TEXTURE0); // Switch back to default texture unit 0
	}

	if (alpha < 1.0f)
	{	//go back to a known state
		glDepthMask (GL_TRUE);
		glDisable (GL_BLEND);
	}

	GL_UseProgramFunc (0);

	GL_DisableVertexAttribArrayFunc (vertAttrIndex);
	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (LMCoordsAttrIndex);
	GL_SelectTexture (GL_TEXTURE0);

	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
}
qboolean RSceneCache_HasSky(void)
{
	struct rscenecache_s *cache = rscenecache.drawing;
	unsigned int i, j;
	texture_t *tex;

	if (cache)
	{
		for (i = 0; i < cache->numtextures; i++)
		{
			tex = cache->worldmodel->textures[i];
			if (!tex || !(tex->name[0]=='s'&&tex->name[1]=='k'&&tex->name[2]=='y'))
				continue;	//we only want sky textures.
			for (j = 0; j < cache->lightmaps * 2; j++)
				if (cache->batches[i * cache->lightmaps * 2 + j].numidx)
					return true;
		}
	}
	return false;
}
qboolean RSceneCache_DrawSkySurfDepth(void)
{	//legacy skyboxes are a serious pain, but oh well...
	//if we draw anything here then its JUST depth values. we don't need glsl nor even textures for this.
	struct rscenecache_s *cache = rscenecache.drawing;

	extern GLuint gl_bmodel_vbo;
	unsigned int i, j;
	texture_t *tex;
	qboolean ret = false;

	if (!cache)
		return false;
	rscenecache.doingskybox = true;

	RSceneCache_Finish(cache);

	for (i = 0; i < cache->numtextures; i++)
	{
		tex = cache->worldmodel->textures[i];
		if (!tex || !(tex->name[0]=='s'&&tex->name[1]=='k'&&tex->name[2]=='y'))
			continue;	//we only want sky textures.
		for (j = 0; j < cache->lightmaps * 2; j++) // Optional: Changed loop bound for robustness
		{
			if (!cache->batches[i*cache->lightmaps*2 + j].numidx)
				continue;	//don't waste time on it.

			if (!ret)
			{	//first batch of sky, set up the vertex array stuff.
				ret = true;
				GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
				GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, cache->ebo); // indices come from client memory!

				glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(float), ((float *)0));
				glEnableClientState(GL_VERTEX_ARRAY);
			}
			//then draw it
			glDrawElements(GL_TRIANGLES, cache->batches[i*cache->lightmaps*2 + j].numidx, GL_UNSIGNED_INT, cache->batches[i*cache->lightmaps*2 + j].eboidx);
			rs_brushpasses++;
		}
	}

	if (ret)
	{
		glDisableClientState(GL_VERTEX_ARRAY);
		GL_BindBuffer (GL_ARRAY_BUFFER, 0);
		GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
	}
	return ret;
}
void RSceneCache_Shutdown(void)
{	//clean up the scene cache stuff.
	struct rscenecache_s *cache;

	RSceneCache_ResetLightstyleTracking(NULL);

	while ((cache=rscenecache.cache))
	{
		rscenecache.cache = cache->next;
		RSceneCache_Uncache(cache);
	}

	if (rscenecache.thread)
	{
		SDL_LockMutex(rscenecache.mutex);
		rscenecache.die = true;
		SDL_CondSignal(rscenecache.wt_cond);	//make sure it wakes up so it knows it needs to die.
		SDL_UnlockMutex(rscenecache.mutex);

		SDL_WaitThread(rscenecache.thread, NULL);
		SDL_DestroyCond(rscenecache.wt_cond);
		SDL_DestroyCond(rscenecache.rt_cond);
		SDL_DestroyMutex(rscenecache.mutex);

		rscenecache.thread = NULL;
		rscenecache.wt_cond = NULL;
		rscenecache.rt_cond = NULL;
		rscenecache.mutex = NULL;
	}
	rscenecache.drawing = NULL;
	skipsubmodels = NULL;
}
#endif

qboolean R_WorldSkyVisible(void)
{
	qmodel_t *model = cl.worldmodel;
	texture_t *tex;
	msurface_t *surf, **mark;
	int i;

	if (!model)
		return false;

#ifndef SDL_THREADS_DISABLED
	if (RSceneCache_HasSky())
		return true;
#endif

	for (i = 0, surf = model->surfaces; i < model->numsurfaces; i++, surf++)
		if ((surf->flags & SURF_DRAWSKY) && surf->visframe == r_visframecount)
			return true;

	for (i = 0; i < model->numtextures; i++)
	{
		tex = model->textures[i];
		if (tex && tex->texturechains[chain_world] &&
			(tex->texturechains[chain_world]->flags & SURF_DRAWSKY))
			return true;
	}

	if (r_viewleaf)
	{
		for (i = 0, mark = r_viewleaf->firstmarksurface; i < r_viewleaf->nummarksurfaces; i++, mark++)
			if ((*mark)->flags & SURF_DRAWSKY)
				return true;
	}

	return false;
}
