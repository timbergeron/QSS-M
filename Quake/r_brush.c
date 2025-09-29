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

int		gl_lightmap_format;
int		lightmap_bytes;
qboolean lightmaps_latecached;
qboolean lightmaps_skipupdates;

#define MAX_SANITY_LIGHTMAPS (1u<<20)
struct lightmap_s	*lightmaps;
int					lightmap_count;
static int			last_lightmap_allocated;
static int			*allocated;
int LMBLOCK_WIDTH, LMBLOCK_HEIGHT;


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

#if 0
/*
================
R_DrawSequentialPoly -- johnfitz -- rewritten
================
*/
void R_DrawSequentialPoly (msurface_t *s)
{
	glpoly_t	*p;
	texture_t	*t;
	float		*v;
	float		entalpha;
	int			i;

	t = R_TextureAnimation (s->texinfo->texture, currententity->frame);
	entalpha = ENTALPHA_DECODE(currententity->alpha);

// drawflat
	if (r_drawflat_cheatsafe)
	{
		if ((s->flags & SURF_DRAWTURB) && r_oldwater.value)
		{
			for (p = s->polys->next; p; p = p->next)
			{
				srand((unsigned int) (uintptr_t) p);
				glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
				DrawGLPoly (p);
				rs_brushpasses++;
			}
			return;
		}

		srand((unsigned int) (uintptr_t) s->polys);
		glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
		DrawGLPoly (s->polys);
		rs_brushpasses++;
		return;
	}

// fullbright
	if ((r_fullbright_cheatsafe) && !(s->flags & SURF_DRAWTILED))
	{
		if (entalpha < 1)
		{
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor4f(1, 1, 1, entalpha);
		}
		
		if (s->flags & SURF_DRAWFENCE)
			glEnable (GL_ALPHA_TEST); // Flip on alpha test
			
		GL_Bind (t->gltexture);
		DrawGLPoly (s->polys);
		rs_brushpasses++;
		
		if (s->flags & SURF_DRAWFENCE)
			glDisable (GL_ALPHA_TEST); // Flip alpha test back off
				
		if (entalpha < 1)
		{
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glColor3f(1, 1, 1);
		}
		goto fullbrights;
	}

// r_lightmap
	if (r_lightmap_cheatsafe)
	{
		if (s->flags & SURF_DRAWTILED)
		{
			glDisable (GL_TEXTURE_2D);
			DrawGLPoly (s->polys);
			glEnable (GL_TEXTURE_2D);
			rs_brushpasses++;
			return;
		}

		R_RenderDynamicLightmaps (s);
		GL_Bind (lightmap_textures[s->lightmaptexturenum]);
		if (!gl_overbright.value)
		{
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor3f(0.5, 0.5, 0.5);
		}
		glBegin (GL_POLYGON);
		v = s->polys->verts[0];
		for (i=0 ; i<s->polys->numverts ; i++, v+= VERTEXSIZE)
		{
			glTexCoord2f (v[5], v[6]);
			glVertex3fv (v);
		}
		glEnd ();
		if (!gl_overbright.value)
		{
			glColor3f(1,1,1);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		rs_brushpasses++;
		return;
	}

// sky poly -- skip it, already handled in gl_sky.c
	if (s->flags & SURF_DRAWSKY)
		return;

// water poly
	if (s->flags & SURF_DRAWTURB)
	{
		if (currententity->alpha == ENTALPHA_DEFAULT)
		{
			entalpha = GL_WaterAlphaForSurface(s);
			if (entalpha > 1.0f) entalpha = 1.0f;
			else if (entalpha < 0.0f) entalpha = 0.0f;
		}
		if (entalpha < 1)
		{
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor4f(1, 1, 1, entalpha);
		}
		if (r_oldwater.value)
		{
			GL_Bind (s->texinfo->texture->gltexture);
			for (p = s->polys->next; p; p = p->next)
			{
				DrawWaterPoly (p);
				rs_brushpasses++;
			}
			rs_brushpasses++;
		}
		else
		{
			GL_Bind (s->texinfo->texture->warpimage);
			s->texinfo->texture->update_warp = true; // FIXME: one frame too late!
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
		if (entalpha < 1)
		{
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glColor3f(1, 1, 1);
		}
		return;
	}

// missing texture
	if (s->flags & SURF_NOTEXTURE)
	{
		if (entalpha < 1)
		{
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor4f(1, 1, 1, entalpha);
		}
		GL_Bind (t->gltexture);
		DrawGLPoly (s->polys);
		rs_brushpasses++;
		if (entalpha < 1)
		{
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glColor3f(1, 1, 1);
		}
		return;
	}

// lightmapped poly
	if (entalpha < 1)
	{
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f(1, 1, 1, entalpha);
	}
	else
		glColor3f(1, 1, 1);
		
	if (s->flags & SURF_DRAWFENCE)
		glEnable (GL_ALPHA_TEST); // Flip on alpha test
		
	if (gl_overbright.value)
	{
		if (gl_texture_env_combine && gl_mtexable) //case 1: texture and lightmap in one pass, overbright using texture combiners
		{
			GL_DisableMultitexture(); // selects TEXTURE0
			GL_Bind (t->gltexture);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_EnableMultitexture(); // selects TEXTURE1
			GL_Bind (lightmap_textures[s->lightmaptexturenum]);
			R_RenderDynamicLightmaps (s);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_PREVIOUS_EXT);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_TEXTURE);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			glBegin(GL_POLYGON);
			v = s->polys->verts[0];
			for (i=0 ; i<s->polys->numverts ; i++, v+= VERTEXSIZE)
			{
				GL_MTexCoord2fFunc (GL_TEXTURE0_ARB, v[3], v[4]);
				GL_MTexCoord2fFunc (GL_TEXTURE1_ARB, v[5], v[6]);
				glVertex3fv (v);
			}
			glEnd ();
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DisableMultitexture ();
			rs_brushpasses++;
		}
		else if (entalpha < 1 || (s->flags & SURF_DRAWFENCE)) //case 2: can't do multipass if entity has alpha, so just draw the texture
		{
			GL_Bind (t->gltexture);
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
		else //case 3: texture in one pass, lightmap in second pass using 2x modulation blend func, fog in third pass
		{
			//first pass -- texture with no fog
			Fog_DisableGFog ();
			GL_Bind (t->gltexture);
			DrawGLPoly (s->polys);
			Fog_EnableGFog ();
			rs_brushpasses++;

			//second pass -- lightmap with black fog, modulate blended
			R_RenderDynamicLightmaps (s);
			GL_Bind (lightmap_textures[s->lightmaptexturenum]);
			glDepthMask (GL_FALSE);
			glEnable (GL_BLEND);
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR); //2x modulate
			Fog_StartAdditive ();
			glBegin (GL_POLYGON);
			v = s->polys->verts[0];
			for (i=0 ; i<s->polys->numverts ; i++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[5], v[6]);
				glVertex3fv (v);
			}
			glEnd ();
			Fog_StopAdditive ();
			rs_brushpasses++;

			//third pass -- black geo with normal fog, additive blended
			if (Fog_GetDensity() > 0)
			{
				glBlendFunc(GL_ONE, GL_ONE); //add
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glColor3f(0,0,0);
				DrawGLPoly (s->polys);
				glColor3f(1,1,1);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				rs_brushpasses++;
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
			GL_DisableMultitexture(); // selects TEXTURE0
			GL_Bind (t->gltexture);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_EnableMultitexture(); // selects TEXTURE1
			GL_Bind (lightmap_textures[s->lightmaptexturenum]);
			R_RenderDynamicLightmaps (s);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glBegin(GL_POLYGON);
			v = s->polys->verts[0];
			for (i=0 ; i<s->polys->numverts ; i++, v+= VERTEXSIZE)
			{
				GL_MTexCoord2fFunc (GL_TEXTURE0_ARB, v[3], v[4]);
				GL_MTexCoord2fFunc (GL_TEXTURE1_ARB, v[5], v[6]);
				glVertex3fv (v);
			}
			glEnd ();
			GL_DisableMultitexture ();
			rs_brushpasses++;
		}
		else if (entalpha < 1 || (s->flags & SURF_DRAWFENCE)) //case 5: can't do multipass if entity has alpha, so just draw the texture
		{
			GL_Bind (t->gltexture);
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
		else //case 6: texture in one pass, lightmap in a second pass, fog in third pass
		{
			//first pass -- texture with no fog
			Fog_DisableGFog ();
			GL_Bind (t->gltexture);
			DrawGLPoly (s->polys);
			Fog_EnableGFog ();
			rs_brushpasses++;

			//second pass -- lightmap with black fog, modulate blended
			R_RenderDynamicLightmaps (s);
			GL_Bind (lightmap_textures[s->lightmaptexturenum]);
			glDepthMask (GL_FALSE);
			glEnable (GL_BLEND);
			glBlendFunc (GL_ZERO, GL_SRC_COLOR); //modulate
			Fog_StartAdditive ();
			glBegin (GL_POLYGON);
			v = s->polys->verts[0];
			for (i=0 ; i<s->polys->numverts ; i++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[5], v[6]);
				glVertex3fv (v);
			}
			glEnd ();
			Fog_StopAdditive ();
			rs_brushpasses++;

			//third pass -- black geo with normal fog, additive blended
			if (Fog_GetDensity() > 0)
			{
				glBlendFunc(GL_ONE, GL_ONE); //add
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glColor3f(0,0,0);
				DrawGLPoly (s->polys);
				glColor3f(1,1,1);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				rs_brushpasses++;
			}

			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
			glDepthMask (GL_TRUE);

		}
	}
	if (entalpha < 1)
	{
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor3f(1, 1, 1);
	}
	
	if (s->flags & SURF_DRAWFENCE)
		glDisable (GL_ALPHA_TEST); // Flip alpha test back off
	
fullbrights:
	if (gl_fullbrights.value && t->fullbright)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
		glBlendFunc (GL_ONE, GL_ONE);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor3f (entalpha, entalpha, entalpha);
		GL_Bind (t->fullbright);
		Fog_StartAdditive ();
		DrawGLPoly (s->polys);
		Fog_StopAdditive ();
		glColor3f(1, 1, 1);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable (GL_BLEND);
		glDepthMask (GL_TRUE);
		rs_brushpasses++;
	}
}
#endif
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
	extern byte *skipsubmodels;

	if (e->model->submodelof == cl.worldmodel &&
		skipsubmodels &&
		skipsubmodels[e->model->submodelidx>>3]&(1u<<(e->model->submodelidx&7)))
		return;	//its in the scenecache that we're drawing. don't draw it twice (and certainly not the slow way).

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

// calculate dynamic lighting for bmodel if it's not an
// instanced model
	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value)
	if (e->model->submodelof == cl.worldmodel)	//R_MarkLights has a hacky assumption about cl.worldmodel that could crash if we imported some other model.
	{
		for (k=0 ; k<MAX_DLIGHTS ; k++)
		{
			if ((cl_dlights[k].die < cl.time) ||
				(!cl_dlights[k].radius))
				continue;

			VectorSubtract(cl_dlights[k].origin, e->origin, lightorg);
			R_MarkLights (&cl_dlights[k], lightorg, r_framecount, k,
				clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	glPushMatrix ();
	e->angles[0] = -e->angles[0];	// stupid quake bug
	if (gl_zfix.value)
	{
		e->origin[0] -= DIST_EPSILON;
		e->origin[1] -= DIST_EPSILON;
		e->origin[2] -= DIST_EPSILON;
	}
	R_RotateForEntity (e->origin, e->angles, e->netstate.scale);
	if (gl_zfix.value)
	{
		e->origin[0] += DIST_EPSILON;
		e->origin[1] += DIST_EPSILON;
		e->origin[2] += DIST_EPSILON;
	}
	e->angles[0] = -e->angles[0];	// stupid quake bug

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
	R_RotateForEntity (e->origin, e->angles, e->netstate.scale);
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
	glRect_t    *theRect;
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
			R_BuildLightMap (model, fa, base, LMBLOCK_WIDTH*lightmap_bytes, currententity, r_framecount, cl_dlights);
		}
	}
}

/*
========================
AllocBlock -- returns a texture number and the position inside it
========================
*/
int AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	// ericw -- rather than searching starting at lightmap 0 every time,
	// start at the last lightmap we allocated a surface in.
	// This makes AllocBlock much faster on large levels (can shave off 3+ seconds
	// of load time on a level with 180 lightmaps), at a cost of not quite packing
	// lightmaps as tightly vs. not doing this (uses ~5% more lightmaps)
	for (texnum=last_lightmap_allocated ; texnum<MAX_SANITY_LIGHTMAPS ; texnum++)
	{
		if (texnum == lightmap_count)
		{
			lightmap_count++;
			lightmaps = (struct lightmap_s *) realloc(lightmaps, sizeof(*lightmaps)*lightmap_count);
			memset(&lightmaps[texnum], 0, sizeof(lightmaps[texnum]));
			if (GL_BufferStorageFunc)
			{	//if we have bufferstorage then we have mapbufferrange+persistent+coherent
				GL_GenBuffersFunc(1, &lightmaps[texnum].pbohandle);
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lightmaps[texnum].pbohandle);
				GL_BufferStorageFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 4*LMBLOCK_WIDTH*LMBLOCK_HEIGHT, NULL, GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT|GL_CLIENT_STORAGE_BIT);
				lightmaps[texnum].pbodata = GL_MapBufferRangeFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0, 4*LMBLOCK_WIDTH*LMBLOCK_HEIGHT, GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT);
				GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
			}
			else
				lightmaps[texnum].pbodata = (byte *) calloc(1, 4*LMBLOCK_WIDTH*LMBLOCK_HEIGHT);
			//as we're only tracking one texture, we don't need multiple copies of allocated any more.
			memset(allocated, 0, sizeof(*allocated)*LMBLOCK_WIDTH);

			lightmaps[texnum].modified = true;
			lightmaps[texnum].rectchange.l = 0;
			lightmaps[texnum].rectchange.t = 0;
			lightmaps[texnum].rectchange.h = LMBLOCK_HEIGHT;
			lightmaps[texnum].rectchange.w = LMBLOCK_WIDTH;
		}
		best = LMBLOCK_HEIGHT;

		for (i=0 ; i<LMBLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (allocated[i+j] >= best)
					break;
				if (allocated[i+j] > best2)
					best2 = allocated[i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > LMBLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			allocated[*x + i] = best + h;

		last_lightmap_allocated = texnum;
		return texnum;
	}

	Sys_Error ("AllocBlock: full");
	return 0; //johnfitz -- shut up compiler
}


mvertex_t	*r_pcurrentvertbase;
qmodel_t	*currentmodel;

int	nColinElim;

/*
========================
GL_CreateSurfaceLightmap
========================
*/
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
	base = lightmaps[surf->lightmaptexturenum].pbodata;
	base += (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * lightmap_bytes;
	R_BuildLightMap (model, surf, base, LMBLOCK_WIDTH*lightmap_bytes, currententity, r_framecount, cl_dlights);
}

#ifdef MACBOOK_ARM_HACK // ezquake 22f39e2 by nano -- woods #collinear
#define EPSILON 1e-6

// Check if triangle has a ~zero area
// https://en.wikipedia.org/wiki/Collinearity
static qboolean R_ArePointsColinear(const vec3_t v1, const vec3_t v2, const vec3_t v3)
{
	vec3_t d0, d1, cross;

	VectorSubtract(v2, v1, d0);
	VectorSubtract(v3, v2, d1);

	CrossProduct(d0, d1, cross);

	return DotProduct(cross, cross) < EPSILON;
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

	if (new_numverts > 0) {
		memcpy(poly->verts, new_verts, new_numverts * sizeof(float) * VERTEXSIZE);
		poly->numverts = new_numverts;
	}
}
#endif

/*
================
BuildSurfaceDisplayList -- called at level load time
================
*/
static void BuildSurfaceDisplayList (msurface_t *fa)
{
#ifdef MACBOOK_ARM_HACK // woods #collinear
	extern cvar_t r_remove_collinear_vertices;
#endif
	int			i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	float		*vec;
	float		s, t, s0, t0;
	glpoly_t	*poly;

// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	//
	// draw texture
	//
	poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = fa->polys;
	fa->polys = poly;
	poly->numverts = lnumverts;

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
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = r_pcurrentvertbase[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = r_pcurrentvertbase[r_pedge->v[1]].position;
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
		//johnfitz -- rewritten to use SURF_DRAWTILED instead of the sky/water flags
		GL_CreateSurfaceLightmap (m, m->surfaces + i);
		BuildSurfaceDisplayList (m->surfaces + i);
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

	RSceneCache_Shutdown();	//make sure there's nothing poking them off-thread.

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
	allocated = realloc(allocated, sizeof(*allocated)*LMBLOCK_WIDTH);

	if (gl_texture_e5bgr9)// && cl.worldmodel && (cl.worldmodel->flags&MOD_HDRLIGHTING))
		gl_lightmap_format = GL_RGB9_E5; //requires gl3, allowing for hdr lighting.
	else
		gl_lightmap_format = GL_RGBA;//FIXME: hardcoded for now!

	switch (gl_lightmap_format)
	{
	case GL_RGB9_E5:
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

	//
	// upload all lightmaps that were filled
	//
	for (i=0; i<lightmap_count; i++)
	{
		lm = &lightmaps[i];
		lm->modified = false;
		lm->rectchange.l = LMBLOCK_WIDTH;
		lm->rectchange.t = LMBLOCK_HEIGHT;
		lm->rectchange.w = 0;
		lm->rectchange.h = 0;

		//johnfitz -- use texture manager
		sprintf(name, "lightmap%07i",i);

		if (lm->pbohandle)
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
}

/*
=============================================================

	VBO support

=============================================================
*/

GLuint gl_bmodel_vbo = 0;

void GL_DeleteBModelVertexBuffer (void)
{
	if (!(gl_vbo_able && gl_mtexable && gl_max_texture_units >= 3))
		return;

	GL_DeleteBuffersFunc (1, &gl_bmodel_vbo);
	gl_bmodel_vbo = 0;

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
	unsigned int	numverts, varray_bytes, varray_index;
	int		i, j;
	qmodel_t	*m;
	float		*varray;

	if (!(gl_vbo_able && gl_mtexable && gl_max_texture_units >= 3))
		return;

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
			numverts += m->surfaces[i].numedges;
		}
	}
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache_csqc[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			numverts += m->surfaces[i].numedges;
		}
	}
	
// build vertex array
	varray_bytes = VERTEXSIZE * sizeof(float) * numverts;
	varray = (float *) malloc (varray_bytes);
	varray_index = 0;
	
	for (j=1 ; j<MAX_MODELS ; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i=0 ; i<m->numsurfaces ; i++)
		{
			msurface_t *s = &m->surfaces[i];
			s->vbo_firstvert = varray_index;
			memcpy (&varray[VERTEXSIZE * varray_index], s->polys->verts, VERTEXSIZE * sizeof(float) * s->numedges);
			varray_index += s->numedges;
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
			s->vbo_firstvert = varray_index;
			memcpy (&varray[VERTEXSIZE * varray_index], s->polys->verts, VERTEXSIZE * sizeof(float) * s->numedges);
			varray_index += s->numedges;
		}
	}

// upload to GPU
	GL_BindBufferFunc (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BufferDataFunc (GL_ARRAY_BUFFER, varray_bytes, varray, GL_STATIC_DRAW);
	free (varray);
	
// invalidate the cached bindings
	GL_ClearBufferBindings ();
}

/*
===============
R_AddDynamicLights
===============
*/
static void R_AddDynamicLights (msurface_t *surf, unsigned *blocklights, entity_t *currentent, dlight_t *lights)
{
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	//johnfitz -- lit support via lordhavoc
	float		cred, cgreen, cblue, brightness;
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
		cred = lights[lnum].color[0] * 256.0f;
		cgreen = lights[lnum].color[1] * 256.0f;
		cblue = lights[lnum].color[2] * 256.0f;
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
void R_BuildLightMap (qmodel_t *model, msurface_t *surf, byte *dest, int stride, entity_t *currentent, int framecount, dlight_t *lights)
{
	int			smax, tmax;
	int			r,g,b;
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

		if (!(cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cls.demoplayback)) // woods #rambient
		{
			unsigned ambient_light = ((unsigned)CLAMP(0.0f, r_ambient.value, 255.0f)) << 8;

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
				scale = d_lightstylevalue[surf->styles[maps]];
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
				scale = d_lightstylevalue[surf->styles[maps]];
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
			R_AddDynamicLights (surf, blocklights, currentent, lights);
	}
	else
	{
	// set to full bright if no light data
		for (i=0 ; i<size ; i++)
			blocklights[i] = 0xffff;	//don't use memset, it oversaturates FAR too much with hdr...
	}

// bound, invert, and shift
// store:
	switch (gl_lightmap_format)
	{
	case GL_RGB9_E5:
		{
			int e;
			float m;
			float scale, identity = 1u<<((gl_overbright.value?8:7)+8);	//overbright is redundant with this, but its easier to leave it than conditionally block it.
			stride -= smax * 4;
			bl = blocklights;
			for (i=0 ; i<tmax ; i++, dest += stride)
			{
				for (j=0 ; j<smax ; j++)
				{
					e = 0;
					m = q_max(q_max(bl[0], bl[1]), bl[2])/identity;
					if (m >= 0.5)
					{	//positive exponent
						while (m >= (1<<(e)) && e < 30-15)	//don't do nans.
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
	case GL_RGBA:
		stride -= smax * 4;
		bl = blocklights;
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				if (gl_overbright.value)
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
				if (gl_overbright.value)
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

/*
===============
R_UploadLightmap -- johnfitz -- uploads the modified lightmap to opengl if necessary

assumes lightmap texture is already bound
===============
*/
static void R_UploadLightmap(int lmap)
{
	struct lightmap_s *lm = &lightmaps[lmap];

	if (!lm->modified)
		return;

	lm->modified = false;

	if (lm->pbohandle)
	{
		GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, lm->pbohandle);
		if (gl_lightmap_format == GL_RGB9_E5)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, lm->rectchange.t, LMBLOCK_WIDTH, lm->rectchange.h, GL_RGB,
					GL_UNSIGNED_INT_5_9_9_9_REV, (byte*)NULL+lm->rectchange.t*LMBLOCK_WIDTH*lightmap_bytes);
		else
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, lm->rectchange.t, LMBLOCK_WIDTH, lm->rectchange.h, gl_lightmap_format,
					GL_UNSIGNED_BYTE, (byte*)NULL+lm->rectchange.t*LMBLOCK_WIDTH*lightmap_bytes);
		GL_BindBufferFunc(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
	}
	else
	{
		if (gl_lightmap_format == GL_RGB9_E5)
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, lm->rectchange.t, LMBLOCK_WIDTH, lm->rectchange.h, GL_RGB,
					GL_UNSIGNED_INT_5_9_9_9_REV, lm->pbodata+lm->rectchange.t*LMBLOCK_WIDTH*lightmap_bytes);
		else
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, lm->rectchange.t, LMBLOCK_WIDTH, lm->rectchange.h, gl_lightmap_format,
					GL_UNSIGNED_BYTE, lm->pbodata+lm->rectchange.t*LMBLOCK_WIDTH*lightmap_bytes);
	}
	lm->rectchange.l = LMBLOCK_WIDTH;
	lm->rectchange.t = LMBLOCK_HEIGHT;
	lm->rectchange.h = 0;
	lm->rectchange.w = 0;

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
    R_RotateForEntity(e->origin, e->angles, e->netstate.scale);

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
	if (!(cl.gametype == GAME_DEATHMATCH && cls.state == ca_connected && !cls.demoplayback))
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