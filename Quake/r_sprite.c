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
//r_sprite.c -- sprite model rendering

#include "quakedef.h"

/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *currentent)
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int				i, numframes, frame;
	float			*pintervals, fullinterval, targettime, time;

	psprite = (msprite_t *) currentent->model->cache.data;
	frame = currentent->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_DrawSprite: no such frame %d for '%s'\n", frame, currentent->model->name);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else if (psprite->frames[frame].type == SPR_ANGLED)
	{
		// erysdren - angled sprites code backported from FTEQW
		vec3_t axis[3];
		AngleVectors(currentent->angles, axis[0], axis[1], axis[2]);
		{
			float f = DotProduct(vpn, axis[0]);
			float r = DotProduct(vright, axis[0]);
			float ang = atan2(r, f);
			float slice;
			int dir;

			pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
			numframes = pspritegroup->numframes;

			// map the yaw difference onto 0..numframes, biased by half a slice so
			// each frame is centred on its own direction. any positive count works.
			slice = isfinite(ang) ? ((ang + M_PI) / (2 * M_PI)) * numframes + 0.5f : 0;
			dir = (int)slice % numframes;	// slice is in [0, numframes], so this stays in range

			pspriteframe = pspritegroup->frames[dir];
		}
	}
	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		time = cl.time + currentent->syncbase;

	// the loader guarantees the intervals are positive and finite, but a legally
	// tiny one makes time/fullinterval exceed int range, and that conversion is
	// undefined. fmodf has no such trap.
		targettime = fmodf (time, fullinterval);
		if (targettime < 0)	// fmodf takes the sign of the dividend
			targettime += fullinterval;

		for (i = 0; i < (numframes-1); i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}

typedef struct spritevert_s
{
	float	xyz[3];
	float	st[2];
	GLubyte	rgba[4];
} spritevert_t;

#define SPRITE_BATCH_MAX 512

static spritevert_t		sprite_batch_verts[SPRITE_BATCH_MAX * 4];
static qboolean			sprite_batch_active;
static int				sprite_batch_count;
static gltexture_t		*sprite_batch_tex;
static qboolean			sprite_batch_blend;
static qboolean			sprite_batch_additive;
static qboolean			sprite_batch_oriented;
static float			sprite_batch_alpharef;

void R_BeginSpriteBatch (void)
{
	sprite_batch_active = true;
	sprite_batch_count = 0;
	sprite_batch_tex = NULL;
}

void R_FlushSpriteBatch (void)
{
	int i, nverts;

	if (!sprite_batch_count)
		return;

	if (sprite_batch_oriented)
		GL_PolygonOffset (OFFSET_DECAL);

	GL_DisableMultitexture ();
	GL_Bind (sprite_batch_tex);
	glEnable (GL_ALPHA_TEST);
	glAlphaFunc (GL_GREATER, sprite_batch_alpharef);

	if (sprite_batch_blend)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
		if (sprite_batch_additive)
			glBlendFunc (GL_SRC_ALPHA, GL_ONE);
		else
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	nverts = sprite_batch_count * 4;
	glBegin (GL_TRIANGLES); // preserve diagonal triangle edges for r_showtris
	for (i = 0; i < nverts; i += 4)
	{
		const spritevert_t *v = &sprite_batch_verts[i];
		glColor4ubv (v[0].rgba);
		glTexCoord2fv (v[0].st);
		glVertex3fv (v[0].xyz);
		glTexCoord2fv (v[1].st);
		glVertex3fv (v[1].xyz);
		glTexCoord2fv (v[2].st);
		glVertex3fv (v[2].xyz);
		glTexCoord2fv (v[0].st);
		glVertex3fv (v[0].xyz);
		glTexCoord2fv (v[2].st);
		glVertex3fv (v[2].xyz);
		glTexCoord2fv (v[3].st);
		glVertex3fv (v[3].xyz);
	}
	glEnd ();

	glDisable (GL_ALPHA_TEST);
	glAlphaFunc (GL_GREATER, 0.666f);

	if (sprite_batch_blend)
	{
		glDepthMask (GL_TRUE);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable (GL_BLEND);
	}

	glColor4f (1, 1, 1, 1);

	if (sprite_batch_oriented)
		GL_PolygonOffset (OFFSET_NONE);

	sprite_batch_count = 0;
	sprite_batch_tex = NULL;
}

void R_EndSpriteBatch (void)
{
	R_FlushSpriteBatch ();
	sprite_batch_active = false;
}

/*
=================
R_DrawSpriteModel -- johnfitz -- rewritten: now supports all orientations
=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	vec3_t			v_forward, v_right, v_up;
	msprite_t		*psprite;
	mspriteframe_t	*frame;
	float			*s_up, *s_right;
	float			angle, sr, cr;
	float			scale = ENTSCALE_DECODE(e->netstate.scale);
	float			entalpha = ENTALPHA_DECODE(e->alpha);
	float			alpharef;
	qboolean		blend, additive, oriented;
	GLubyte			rgba[4];
	spritevert_t	*v;
	float			down, up, left, right;

	if (entalpha <= 0.0f)
		return;

	// advance the rotation before culling, like R_SetupAliasFrame does, so an
	// offscreen sprite's spin keeps running instead of popping when it returns
	if (e->model && CL_ApplyModelRotation(e, e->angles, host_frametime)) // woods #clmrotate
	{
		e->effects &= ~EF_ROTATE; // EF_ROTATE already cleared server-side, but if mapper forgot
	}

	// the viewmodel sits on top of the camera, where the frustum planes all meet,
	// so culling it is meaningless. R_DrawAliasModel skips it for the same reason.
	psprite = (msprite_t *) e->model->cache.data;
	if (e != &cl.viewent &&
		(!(e->csqc_renderflags & CSQC_RENDER_AXIS) || psprite->type == SPR_ORIENTED) &&
		R_CullModelForEntity (e)) // woods #spriteharden
		return;

	frame = R_GetSpriteFrame (e);

	switch(psprite->type)
	{
	case SPR_VP_PARALLEL_UPRIGHT: //faces view plane, up is towards the heavens
		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;
		CrossProduct(vpn, v_up, v_right);
		VectorNormalizeFast(v_right);
		s_up = v_up;
		s_right = v_right;
		break;
	case SPR_FACING_UPRIGHT: //faces camera origin, up is towards the heavens
		VectorSubtract(e->origin, r_origin, v_forward);
		v_forward[2] = 0;
		VectorNormalizeFast(v_forward);
		v_right[0] = v_forward[1];
		v_right[1] = -v_forward[0];
		v_right[2] = 0;
		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;
		s_up = v_up;
		s_right = v_right;
		break;
	case SPR_VP_PARALLEL: //faces view plane, up is towards the top of the screen
		s_up = vup;
		s_right = vright;
		break;
	case SPR_ORIENTED: //pitch yaw roll are independent of camera
		if (e->csqc_renderflags & CSQC_RENDER_AXIS)
		{
			VectorScale(e->csqc_axis[1], -1, v_right);
			VectorCopy(e->csqc_axis[2], v_up);
		}
		else
			AngleVectors (e->angles, v_forward, v_right, v_up);
		s_up = v_up;
		s_right = v_right;
		break;
	case SPR_VP_PARALLEL_ORIENTED: //faces view plane, but obeys roll value
		if (e->csqc_renderflags & CSQC_RENDER_AXIS)
		{
			vec3_t angles;
			VectorAngles(e->csqc_axis[0], e->csqc_axis[2], angles);
			angle = angles[ROLL] * M_PI_DIV_180;
		}
		else
			angle = e->angles[ROLL] * M_PI_DIV_180;
		sr = sin(angle);
		cr = cos(angle);
		v_right[0] = vright[0] * cr + vup[0] * sr;
		v_right[1] = vright[1] * cr + vup[1] * sr;
		v_right[2] = vright[2] * cr + vup[2] * sr;
		v_up[0] = vright[0] * -sr + vup[0] * cr;
		v_up[1] = vright[1] * -sr + vup[1] * cr;
		v_up[2] = vright[2] * -sr + vup[2] * cr;
		s_up = v_up;
		s_right = v_right;
		break;
	default:
		return;
	}

	oriented = (psprite->type == SPR_ORIENTED);
	additive = (e->effects & EF_ADDITIVE) != 0;
	blend = (frame->gltexture->flags & TEXPREF_ALPHA) || entalpha < 1.0f || additive;
	alpharef = 0.1f * entalpha;

	if (sprite_batch_count > 0 &&
		(sprite_batch_count >= SPRITE_BATCH_MAX ||
		sprite_batch_tex != frame->gltexture ||
		sprite_batch_blend != blend ||
		sprite_batch_additive != additive ||
		sprite_batch_oriented != oriented ||
		sprite_batch_alpharef != alpharef))
	{
		R_FlushSpriteBatch ();
	}

	if (sprite_batch_count == 0)
	{
		sprite_batch_tex = frame->gltexture;
		sprite_batch_blend = blend;
		sprite_batch_additive = additive;
		sprite_batch_oriented = oriented;
		sprite_batch_alpharef = alpharef;
	}

	rgba[0] = (GLubyte)(CLAMP(0.0f, e->netstate.colormod[0] * (1.0f / 32.0f), 1.0f) * 255.0f + 0.5f);
	rgba[1] = (GLubyte)(CLAMP(0.0f, e->netstate.colormod[1] * (1.0f / 32.0f), 1.0f) * 255.0f + 0.5f);
	rgba[2] = (GLubyte)(CLAMP(0.0f, e->netstate.colormod[2] * (1.0f / 32.0f), 1.0f) * 255.0f + 0.5f);
	rgba[3] = (GLubyte)(CLAMP(0.0f, entalpha, 1.0f) * 255.0f + 0.5f);

	down = frame->down * scale;
	up = frame->up * scale;
	left = frame->left * scale;
	right = frame->right * scale;

	v = &sprite_batch_verts[sprite_batch_count * 4];

	v[0].xyz[0] = e->origin[0] + down * s_up[0] + left * s_right[0];
	v[0].xyz[1] = e->origin[1] + down * s_up[1] + left * s_right[1];
	v[0].xyz[2] = e->origin[2] + down * s_up[2] + left * s_right[2];
	v[0].st[0] = 0.0f;
	v[0].st[1] = frame->tmax;
	memcpy (v[0].rgba, rgba, 4);

	v[1].xyz[0] = e->origin[0] + up * s_up[0] + left * s_right[0];
	v[1].xyz[1] = e->origin[1] + up * s_up[1] + left * s_right[1];
	v[1].xyz[2] = e->origin[2] + up * s_up[2] + left * s_right[2];
	v[1].st[0] = 0.0f;
	v[1].st[1] = 0.0f;
	memcpy (v[1].rgba, rgba, 4);

	v[2].xyz[0] = e->origin[0] + up * s_up[0] + right * s_right[0];
	v[2].xyz[1] = e->origin[1] + up * s_up[1] + right * s_right[1];
	v[2].xyz[2] = e->origin[2] + up * s_up[2] + right * s_right[2];
	v[2].st[0] = frame->smax;
	v[2].st[1] = 0.0f;
	memcpy (v[2].rgba, rgba, 4);

	v[3].xyz[0] = e->origin[0] + down * s_up[0] + right * s_right[0];
	v[3].xyz[1] = e->origin[1] + down * s_up[1] + right * s_right[1];
	v[3].xyz[2] = e->origin[2] + down * s_up[2] + right * s_right[2];
	v[3].st[0] = frame->smax;
	v[3].st[1] = frame->tmax;
	memcpy (v[3].rgba, rgba, 4);

	sprite_batch_count++;

	if (!sprite_batch_active)
		R_FlushSpriteBatch ();
}
