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
cvar_t r_scenecache = {"r_scenecache",""};	//spike, an attempt to cope with abusive maps a bit better.

byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);
static qboolean RSceneCache_Queue(byte *vis);
static void RSceneCache_Draw(qboolean water);
void RSceneCache_Shutdown(void);
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
		return;
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
static GLuint alphaLoc;
GLuint clTimeLoc; // woods #caustics
static GLuint causticsOpacityLoc; // woods #caustics


static struct
{
	GLuint program;

	GLuint light_scale;
	GLuint alpha_scale;
	GLuint time;
	GLuint eyepos;
	GLuint fogalpha;
	GLuint colour;
} r_water[4];	//

#define vertAttrIndex 0
#define texCoordsAttrIndex 1
#define LMCoordsAttrIndex 2

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
		"\n"
		"varying float FogFragCoord;\n"
		"varying vec2 tc_tex;\n"
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
		"	float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
		"	fog = clamp(fog, 0.0, 1.0);\n"
		"	result.rgb = mix(gl_Fog.color.rgb, result.rgb, fog);\n"
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
			"if (gl_Fog.density > 0.0)\n"
				"sky.rgb = mix(sky.rgb, gl_Fog.color.rgb, FogAlpha);\n"
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
		"varying float FogFragCoord;\n"
		"void main ()\n"
		"{\n"
			"vec3 sky = SkyColour.rgb;\n"

#if 1	//sky is logically an infinite distance away, so fog is just an alpha blend with the colour, no distance calcs needed.
			"if (gl_Fog.density > 0.0)\n"
				"sky.rgb = mix(sky.rgb, gl_Fog.color.rgb, FogAlpha);\n"
#else	//do fog as normal. we actually have distance values.
			"float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
			"fog = clamp(fog, 0.0, 1.0) * FogAlpha + (1.0-FogAlpha);\n"
			"sky.rgb = mix(gl_Fog.color.rgb, sky.rgb, fog);\n"
#endif
			"gl_FragColor = vec4(sky, 1.0);\n"
		"}\n";

	size_t i;
	char vtext[1024];
	char ftext[1024];
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
					if (r_water[mode].light_scale != -1)
						GL_Uniform1fFunc (r_water[mode].light_scale, gl_overbright.value?2:1);
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
		"uniform float Alpha;\n"
		"uniform float ClTime;\n"
		"uniform float CausticsOpacity;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"varying vec2 tc_tex;\n"
		"varying vec2 tc_lm;\n"
		"\n"
		"void main()\n"
		"{\n"
		"	vec4 result = texture2D(Tex, tc_tex.xy);\n"
		"	vec4 lightmapColor = texture2D(LMTex, tc_lm.xy); // Sample lightmap early\n"
		"	if (UseAlphaTest && (result.a < 0.666))\n"
		"		discard;\n"
		"\n"
		"	result *= lightmapColor; // Apply base lightmap\n"
		"\n"
		"	if (UseOverbright)\n"
		"		result.rgb *= 2.0;\n"
		"	if (UseFullbrightTex)\n"
		"		result += texture2D(FullbrightTex, tc_tex.xy);\n"
		"\n"
		"	if (UseCausticsTex)\n"
		"	{\n"
		"       // --- Calculate Light Factor --- \n"
		"       // Calculate brightness from the lightmap (average RGB)\n"
		"       // Scale factor might need tuning (e.g., multiply by 2.0 if UseOverbright is often true)\n"
		"       float lightBrightness = (lightmapColor.r + lightmapColor.g + lightmapColor.b) / 3.0;\n"
		"       // Optional: Amplify factor if overbright is used, otherwise lightmap might be too dark\n"
		"       // if (UseOverbright) lightBrightness *= 2.0; \n"
		"       // Ensure factor is between 0 and 1. Adjust the scaling (e.g., 1.5*) for sensitivity.\n"
		"       float lightFactor = clamp(lightBrightness * 1.5, 0.0, 1.0); \n"
		"\n"
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
		"       float finalCausticsOpacity = CausticsOpacity * lightFactor;\n"
		"       result.rgb = mix(result.rgb, causticsRGB * result.rgb * 2.0, finalCausticsOpacity);\n"
		"	}\n"
		"\n"
		"	result = clamp(result, 0.0, 1.0);\n"
		"	float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
		"	fog = clamp(fog, 0.0, 1.0);\n"
		"	result = mix(gl_Fog.color, result, fog);\n"
		"	result.a = Alpha;\n" // FIXME: This will make almost transparent things cut holes though heavy fog
		"	gl_FragColor = result;\n"
		"}\n";

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
		alphaLoc = GL_GetUniformLocation (&r_world_program, "Alpha");
		clTimeLoc = GL_GetUniformLocation(&r_world_program, "ClTime"); // woods #caustics
		causticsOpacityLoc = GL_GetUniformLocation(&r_world_program, "CausticsOpacity"); // woods #caustics

		GL_UseProgramFunc (r_world_program);
		GL_Uniform1iFunc (texLoc, 0);
		GL_Uniform1iFunc (LMTexLoc, 1);
		GL_Uniform1iFunc (fullbrightTexLoc, 2);
		GL_UseProgramFunc (0);
	}

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
	int			i;
	msurface_t	*s;
	texture_t	*t;
	//qboolean	bound; //removed this cos it was pointless anyway
	int		lastlightmap;
	gltexture_t	*fullbright = NULL;
	float		entalpha;
	unsigned int enteffects;

	entalpha = (ent != NULL) ? ENTALPHA_DECODE(ent->alpha) : 1.0f;
	enteffects = (ent != NULL) ? ent->effects : 0;

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
	GL_Uniform1iFunc (useOverbrightLoc, (int)gl_overbright.value);
	GL_Uniform1iFunc(useCausticsTexLoc, 0); // woods #caustics
	GL_Uniform1iFunc (useAlphaTestLoc, 0);
	GL_Uniform1fFunc (alphaLoc, entalpha);
	GL_Uniform1fFunc(clTimeLoc, cl.time); // woods #caustics
	GL_Uniform1fFunc(causticsOpacityLoc, gl_caustics.value); // woods #caustics

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

	// Enable/disable TMU 2 (fullbrights)
	// FIXME: Move below to where we bind GL_TEXTURE0
		if (gl_fullbrights.value && (fullbright = R_TextureAnimation(t, ent != NULL ? ent->frame : 0)->fullbright))
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
		GL_Bind ((R_TextureAnimation(t, ent != NULL ? ent->frame : 0))->gltexture);
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
						GL_Bind((R_TextureAnimation(t, ent != NULL ? ent->frame : 0))->gltexture);
						if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
							GL_Uniform1iFunc(useAlphaTestLoc, 1); // Flip alpha test back on

						lastlightmap = s->lightmaptexturenum;

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
			if (memcmp(old_lightstylevalue, d_lightstylevalue, sizeof(old_lightstylevalue)))	//FIXME: only check lightstyles that are actually used! deathmatch maps can save the resulting expense.
			{
				old_lightstylevalue[0] = INT_MIN;	//something that'll force a regen pretty soon...
				cache = NULL;	//make sure its rebuilt (can still use the best while it computes).
			}
			else if (r_dynamic.value)
			{	//check if there's active dlights. FIXME: they may take a little longer to disappear
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
					cache = NULL;
					break;
				}
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
						if (r_water[mode].light_scale != -1)
							GL_Uniform1fFunc (r_water[mode].light_scale, gl_overbright.value?2:1);
					}
					GL_Uniform1fFunc (r_water[mode].alpha_scale, alpha);
				}
				else if (tex->name[0]=='s'&&tex->name[1]=='k'&&tex->name[2]=='y')
				{
					//sky. because why not.
					extern cvar_t r_skyalpha, r_skyfog, r_fastsky;
					extern float skyflatcolor[3];
					if (r_fastsky.value == 1)  // woods -- #fastsky2
						mode = 3;
					else if (r_fastsky.value == 2)
						mode = (skybox_name[0] || externalskyloaded) ? 2 : 3;
					else
						mode = 2;

					if (rscenecache.doingskybox)
						break;	//we're doing skies weirdly. FIXME: replace with cubemap skies, where possible.

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
						GL_Uniform1fFunc (r_water[mode].fogalpha, r_skyfog.value);
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
						GL_Uniform1iFunc (useOverbrightLoc, (int)gl_overbright.value);
						GL_Uniform1fFunc (alphaLoc, 1);			//worldmodel is never translucent.

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
	unsigned int i;
	texture_t *tex;

	if (cache)
	{
		for (i = 0; i < cache->numtextures; i++)
		{
			tex = cache->worldmodel->textures[i];
			if (!tex || !(tex->name[0]=='s'&&tex->name[1]=='k'&&tex->name[2]=='y'))
				continue;	//we only want sky textures.
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
