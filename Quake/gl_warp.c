/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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
//gl_warp.c -- warping animation support

#include "quakedef.h"

extern cvar_t r_drawflat;

cvar_t r_waterwarp = {"r_waterwarp", "1", CVAR_ARCHIVE};

int gl_warpimagesize;
float load_subdivide_size; //johnfitz -- remember what subdivide_size value was when this map was loaded

static const float	turbsin[] = {
#include "gl_warp_sin.h"
};

#define WARPCALC(s,t) ((s + turbsin[(int)((t*2)+(cl.time*(128.0/M_PI))) & 255]) * (1.0/64)) //johnfitz -- correct warp
#define WARPCALC2(s,t) ((s + turbsin[(int)((t*0.125+cl.time)*(128.0/M_PI)) & 255]) * (1.0/64)) //johnfitz -- old warp

//==============================================================================
//
//  OLD-STYLE WATER
//
//==============================================================================

msurface_t	*warpface;

cvar_t gl_subdivide_size = {"gl_subdivide_size", "128", CVAR_ARCHIVE};

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = FLT_MAX;
	maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;

	if (numverts > 60)
		Sys_Error ("SubdividePolygon: numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size.value * floor (m/gl_subdivide_size.value + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys->next;
	warpface->polys->next = poly;
	poly->numverts = numverts;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;
	}
}

/*
================
GL_SubdivideSurface
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t	verts[64];
	int		i;

	if (fa->polys->numverts > 64)
		Sys_Error ("GL_SubdivideSurface: numverts = %i", fa->polys->numverts);

	warpface = fa;

	//the first poly in the chain is the undivided poly for newwater rendering.
	//grab the verts from that.
	for (i=0; i<fa->polys->numverts; i++)
		VectorCopy (fa->polys->verts[i], verts[i]);

	SubdividePolygon (fa->polys->numverts, verts[0]);
}

/*
================
DrawWaterPoly -- johnfitz
================
*/
void DrawWaterPoly (glpoly_t *p)
{
	float	*v;
	int		i;

	if (load_subdivide_size > 48)
	{
		glBegin (GL_POLYGON);
		v = p->verts[0];
		for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
		{
			glTexCoord2f (WARPCALC2(v[3],v[4]), WARPCALC2(v[4],v[3]));
			glVertex3fv (v);
		}
		glEnd ();
	}
	else
	{
		glBegin (GL_POLYGON);
		v = p->verts[0];
		for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
		{
			glTexCoord2f (WARPCALC(v[3],v[4]), WARPCALC(v[4],v[3]));
			glVertex3fv (v);
		}
		glEnd ();
	}
}

/* FTE's altwater surface, plus one level of planar reflection/refraction.
 * Keep the normal-map encoding and shader constants together: changing either
 * changes the shape of the waves, even though no diffuse texture is displayed.
 */
cvar_t r_telestyle = {"r_telestyle", "1", CVAR_ARCHIVE};

qboolean r_teleport_view, r_teleport_reflection;
byte *r_teleport_pvs;
unsigned int r_teleport_visframe;

/* Bound GPU memory/work on maps with many differently oriented teleporters.
 * Coplanar faces share a pair of targets. Overflow uses the classic surface.
 */
#define MAX_TELEPORT_PLANES 16
#define TELEPORT_REFLECT_STRENGTH 1.0f
#define TELEPORT_REFRACT_STRENGTH 1.0f
#define TELEPORT_TARGET_SCALE 0.5f
typedef struct
{
	vec3_t normal, center;
	float dist;
	float screenmins[2], screenmaxs[2]; /* union of the coplanar faces in texture coordinates */
	GLuint image[2]; /* refraction, reflection */
	mleaf_t *pvsleaf[2]; /* last merged leaf per pass; reset on collection each view */
	qboolean ready;
} teleport_plane_t;

static teleport_plane_t teleport_planes[MAX_TELEPORT_PLANES];
static int teleport_numplanes, teleport_width, teleport_height;
static GLuint teleport_fbo, teleport_depth, teleport_program;
static qboolean teleport_failed;
static mat4_t teleport_viewmatrix, teleport_projection;
static mat4_t teleport_screenmatrix;
static entity_t **teleport_savedents;
static size_t teleport_maxents;
static byte *teleport_vis;
static size_t teleport_visbytes;
static GLint teleport_time, teleport_eye, teleport_normal, teleport_strength;
static GLint teleport_reflect, teleport_fogmode, teleport_alpha;
static int teleport_pvsbytes;

extern float r_fovx, r_fovy;
extern cvar_t gl_load24bit;
extern cvar_t gl_zfix;

qboolean R_TeleportActive (void)
{
	return ((int)r_telestyle.value == 2 || (int)r_telestyle.value == 3) &&
		gl_glsl_water_able && gl_fbo_able && gl_texture_NPOT && !teleport_failed &&
		cl.worldmodel && cl.worldmodel->hasteletextures;
}

void R_TeleportLoadNormal (qmodel_t *model, texture_t *texture, enum srcformat format,
	const byte *pixels, int width, int height)
{
	byte *heights, *normal;
	size_t count = (size_t)width * height, i;
	int x, y, mark, w, h;
	char name[MAX_QPATH], mapname[MAX_QPATH];
	qboolean malloced = false;
	enum srcformat fmt = SRC_RGBA;
	byte *external = NULL;

	/* Honor the same replacement normal-map name as FTE. */
	mark = Hunk_LowMark();
	COM_StripExtension(model->name + 5, mapname, sizeof(mapname));
	if (gl_load24bit.value)
	{
		q_snprintf(name, sizeof(name), "textures/%s/#%s_norm", mapname, texture->name + 1);
		external = Image_LoadImage(name, &w, &h, &fmt, &malloced);
		if (!external)
		{
			q_snprintf(name, sizeof(name), "textures/#%s_norm", texture->name + 1);
			external = Image_LoadImage(name, &w, &h, &fmt, &malloced);
		}
	}
	if (external)
		texture->tele_normal = TexMgr_LoadImage(model, name, w, h, fmt, external,
			name, 0, TEXPREF_LINEAR | TEXPREF_NOPICMIP);
	if (malloced)
		free(external);
	Hunk_FreeToLowMark(mark);
	if (texture->tele_normal)
		return;
	if (format != SRC_INDEXED && format != SRC_RGBA)
		return;
	if (width <= 0 || height <= 0 || count > INT_MAX / 4)
		return;

	heights = (byte *)malloc(count);
	if (!heights)
		return;
	normal = (byte *)malloc(count * 4);
	if (!normal)
	{
		free(heights);
		return;
	}
	for (i = 0; i < count; i++)
	{
		const byte *rgb = format == SRC_INDEXED ?
			(const byte *)&d_8to24table[pixels[i]] : pixels + i * 4;
		heights[i] = ((int)rgb[0] + rgb[1] + rgb[2]) / 3;
	}
	/* Image_GenerateNormalMap / TF_HEIGHT8PAL in FTE, default bump scale 4. */
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			float c = heights[y * width + x] * (1.0f / 255.0f);
			float dx = 4.0f * (c - heights[y * width + (x + 1) % width] * (1.0f / 255.0f));
			float dy = 4.0f * (c - heights[((y + 1) % height) * width + x] * (1.0f / 255.0f));
			float len = 1.0f / sqrtf(dx * dx + dy * dy + 1.0f);
			byte *out = normal + ((size_t)y * width + x) * 4;
			out[0] = (byte)(128 + 127 * dx * len);
			out[1] = (byte)(128 - 127 * dy * len);
			out[2] = (byte)(128 + 127 * len);
			out[3] = 255;
		}
	free(heights);
	q_snprintf(name, sizeof(name), "%s:#%s_norm", COM_SkipPath(model->name), texture->name + 1);
	texture->tele_normal = TexMgr_LoadImage(model, name, width, height, SRC_RGBA,
		normal, "", (src_offset_t)normal, TEXPREF_LINEAR | TEXPREF_NOPICMIP | TEXPREF_MALLOCSOURCE);
	if (!texture->tele_normal)
		free(normal);
}

static qboolean R_TeleportCreateShader (void)
{
	GLuint program;
	GLint refract, reflect, normalmap, diffuse;
	const GLchar *vs =
		"#version 110\n"
		"uniform vec3 Eye;\n"
		"varying vec2 tc; varying vec4 tf; varying vec3 eye;\n"
		"void main() {\n"
		" tc = gl_MultiTexCoord0.st;\n"
		" tf = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		" eye = Eye - gl_Vertex.xyz; gl_Position = tf;\n"
		"}\n";
	const GLchar *fs =
		"#version 110\n"
		"uniform sampler2D Refract, Reflect, NormalMap, Diffuse;\n"
		"uniform float Time, Alpha; uniform vec3 Normal; uniform vec2 Strength;\n"
		"uniform bool UseReflect; uniform int FogMode;\n"
		"varying vec2 tc; varying vec4 tf; varying vec3 eye;\n"
		"void main() {\n"
		" vec2 stc = (1.0 + tf.xy / tf.w) * 0.5;\n"
		" stc.t -= 1.5 * Normal.z / 1080.0;\n"
		" vec3 n = texture2D(NormalMap, 0.2 * tc + vec2(Time * 0.1, 0.0)).xyz;\n"
		" n += texture2D(NormalMap, 0.2 * tc - vec2(0.0, Time * 0.097)).xyz;\n"
		" n = normalize(n - (1.0 - 4.0 / 256.0));\n"
		/* FTE passes FRESNEL=4, but altwater actually uses FRESNEL_EXP=5. */
		" float fres = pow(1.0 - abs(dot(n, normalize(eye))), 5.0);\n"
		" vec3 refr = texture2D(Refract, stc + n.st * 0.1 * Strength.x).rgb * vec3(0.7, 0.8, 0.7);\n"
		" vec3 refl;\n"
		" if (UseReflect) refl = texture2D(Reflect, stc - n.st * 0.1 * Strength.y).rgb;\n"
		" else refl = texture2D(Diffuse, tc + sin(tc.ts + Time) * 0.125).rgb;\n"
		" vec3 color = mix(refr, refl, fres);\n"
		" float fog;\n"
		" if (FogMode == 1) fog = (gl_Fog.end - tf.w) / (gl_Fog.end - gl_Fog.start);\n"
		" else if (FogMode == 2) fog = exp(-gl_Fog.density * tf.w);\n"
		" else fog = exp(-gl_Fog.density * gl_Fog.density * tf.w * tf.w);\n"
		" gl_FragColor = vec4(mix(gl_Fog.color.rgb, color, clamp(fog, 0.0, 1.0)), Alpha);\n"
		"}\n";
	if (teleport_program)
		return true;
	if (teleport_failed)
		return false;
	program = GL_CreateProgram(vs, fs, 0, NULL);
	teleport_time = GL_GetUniformLocation(&program, "Time");
	teleport_alpha = GL_GetUniformLocation(&program, "Alpha");
	teleport_eye = GL_GetUniformLocation(&program, "Eye");
	teleport_normal = GL_GetUniformLocation(&program, "Normal");
	teleport_strength = GL_GetUniformLocation(&program, "Strength");
	teleport_reflect = GL_GetUniformLocation(&program, "UseReflect");
	teleport_fogmode = GL_GetUniformLocation(&program, "FogMode");
	refract = GL_GetUniformLocation(&program, "Refract");
	reflect = GL_GetUniformLocation(&program, "Reflect");
	normalmap = GL_GetUniformLocation(&program, "NormalMap");
	diffuse = GL_GetUniformLocation(&program, "Diffuse");
	if (!program)
	{
		teleport_failed = true; /* R_DeleteShaders owns any failed program. */
		return false;
	}
	GL_UseProgramFunc(program);
	GL_Uniform1iFunc(refract, 0);
	GL_Uniform1iFunc(reflect, 1);
	GL_Uniform1iFunc(normalmap, 2);
	GL_Uniform1iFunc(diffuse, 3);
	GL_UseProgramFunc(0);
	teleport_program = program;
	return true;
}

void R_TeleportCreateShaders (void)
{
	qboolean scr_was_disabled;
	/* Warm the program during map/context setup, not on first visibility.
	 * In particular, defer disconnected video restarts until R_NewMap. */
	if (cls.state != ca_connected || !R_TeleportActive() || teleport_program)
		return;
	// Compiler diagnostics must not cause a loading-screen render to reenter
	// this path while the program and its uniforms are only partially ready.
	scr_was_disabled = scr_disabled_for_loading;
	scr_disabled_for_loading = true;
	R_TeleportCreateShader();
	scr_disabled_for_loading = scr_was_disabled;
}

static void R_TeleportDeleteTargets (void)
{
	int i;
	for (i = 0; i < MAX_TELEPORT_PLANES; i++)
	{
		glDeleteTextures(2, teleport_planes[i].image);
		memset(&teleport_planes[i], 0, sizeof(teleport_planes[i]));
	}
	if (teleport_fbo)
		GL_DeleteFramebuffersFunc(1, &teleport_fbo);
	if (teleport_depth)
		GL_DeleteRenderbuffersFunc(1, &teleport_depth);
	teleport_fbo = teleport_depth = 0;
	teleport_width = teleport_height = teleport_numplanes = 0;
	GL_ClearBindings();
}

void R_TeleportShutdownGL (void)
{
	R_TeleportDeleteTargets();
	free(teleport_savedents);
	free(teleport_vis);
	teleport_savedents = NULL;
	teleport_vis = NULL;
	teleport_maxents = teleport_visbytes = 0;
	teleport_program = 0; /* owned by R_DeleteShaders */
	teleport_failed = r_teleport_view = r_teleport_reflection = false;
	r_teleport_pvs = NULL;
}

void R_TeleportStyleChanged (cvar_t *var)
{
	int i, style = (int)var->value;
	if (style != 2 && style != 3)
		R_TeleportDeleteTargets();
	else if (style == 2)
	{
		for (i = 0; i < MAX_TELEPORT_PLANES; i++)
		{
			glDeleteTextures(1, &teleport_planes[i].image[1]);
			teleport_planes[i].image[1] = 0;
		}
		GL_ClearBindings();
	}
	// Console style changes after signon should also pay this cost up front.
	// Earlier changes are handled once map setup has completed.
	if (cls.signon == SIGNONS)
		R_TeleportCreateShaders();
}

static void R_TeleportEntityMatrix (entity_t *ent, mat4_t matrix)
{
	vec3_t forward, right, up;
	float scale = ENTSCALE_DECODE(ent->netstate.scale), offset = 0;
	float zfix = gl_zfix.value && !ent->is_static ? DIST_EPSILON : 0;
	int i;
	/* AngleVectors already has the Ry(pitch) convention produced by the
	 * brush pitch flip followed by R_RotateForEntity's negative pitch. */
	AngleVectors(ent->angles, forward, right, up);
	/* Match R_RotateForEntity's local-Z scale pivot as well as its rotation. */
	switch ((ent->netstate.drawflags >> 5) & 3)
	{
	case 0: offset = (ent->model->mins[2] + ent->model->maxs[2]) * 0.5f; break;
	case 1: offset = ent->model->mins[2]; break;
	case 2: offset = ent->model->maxs[2]; break;
	}
	for (i = 0; i < 3; i++)
	{
		matrix[i] = forward[i] * scale;
		matrix[4+i] = -right[i] * scale;
		matrix[8+i] = up[i] * scale;
		/* R_DrawBrushModel nudges the origin before installing its GL matrix. */
		matrix[12+i] = (ent->origin[i] - zfix) + up[i] * offset * (1 - scale);
	}
	matrix[3] = matrix[7] = matrix[11] = 0;
	matrix[15] = 1;
}

/* Derive the plane from the actual brush transform, including entity scale.
 * The matrix is shared by all the entity's faces instead of querying GL for
 * each face during collection, validation, and drawing.
 */
static void R_TeleportPlane (msurface_t *surf, const float *matrix, vec3_t normal,
	float *dist, vec3_t center)
{
	int i, j;
	float sign = (surf->flags & SURF_PLANEBACK) ? -1.0f : 1.0f;
	VectorScale(surf->plane->normal, sign, normal);
	center[0] = center[1] = center[2] = 0;
	for (i = 0; i < surf->polys->numverts; i++)
		VectorAdd(center, surf->polys->verts[i], center);
	VectorScale(center, 1.0f / surf->polys->numverts, center);
	if (matrix)
	{
		vec3_t point, n;
		VectorCopy(center, point);
		VectorCopy(normal, n);
		for (j = 0; j < 3; j++)
		{
			center[j] = matrix[j] * point[0] + matrix[4+j] * point[1] + matrix[8+j] * point[2] + matrix[12+j];
			normal[j] = matrix[j] * n[0] + matrix[4+j] * n[1] + matrix[8+j] * n[2];
		}
		VectorNormalize(normal);
	}
	*dist = DotProduct(normal, center);
}

static int R_TeleportFindPlane (const vec3_t normal, float dist)
{
	int i;
	for (i = 0; i < teleport_numplanes; i++)
		if (DotProduct(normal, teleport_planes[i].normal) > 0.99999f &&
			fabsf(dist - teleport_planes[i].dist) < 0.01f)
			return i;
	return -1;
}

static void R_TeleportScreenBounds (msurface_t *surf, const float *matrix, float mins[2], float maxs[2])
{
	int i, j;
	mins[0] = mins[1] = 1;
	maxs[0] = maxs[1] = 0;
	for (i = 0; i < surf->polys->numverts; i++)
	{
		vec4_t point, world, clip;
		VectorCopy(surf->polys->verts[i], point);
		point[3] = 1;
		if (matrix)
			Matrix4_Transform4(matrix, point, world);
		else
			memcpy(world, point, sizeof(world));
		Matrix4_Transform4(teleport_screenmatrix, world, clip);
		if (clip[3] <= 0.00001f || !isfinite(clip[3]) || !isfinite(clip[0]) || !isfinite(clip[1]))
		{
			// A face crossing the eye plane can project beyond its vertices.
			// Keep the whole target in that case, including very close views.
			mins[0] = mins[1] = 0;
			maxs[0] = maxs[1] = 1;
			return;
		}
		for (j = 0; j < 2; j++)
		{
			float coord = CLAMP(0.0f, (1.0f + clip[j] / clip[3]) * 0.5f, 1.0f);
			mins[j] = q_min(mins[j], coord);
			maxs[j] = q_max(maxs[j], coord);
		}
	}
}

/* Coplanar faces share images, but each face contributes visibility on both
 * sides. Never let a probe in solid turn an otherwise visible face into novis. */
static void R_TeleportMergePVS (int index, const vec3_t center)
{
	int pass, j;
	for (pass = 0; pass < 2; pass++)
	{
		vec3_t origin;
		mleaf_t *leaf;
		byte *src, *dst = teleport_vis + (index * 2 + pass) * (size_t)teleport_pvsbytes;
		VectorMA(center, pass ? 0.1f : -0.1f, teleport_planes[index].normal, origin);
		leaf = Mod_PointInLeaf(origin, cl.worldmodel);
		if (leaf->contents == CONTENTS_SOLID || leaf->contents == CONTENTS_SKY)
			leaf = r_viewleaf;
		// Adjacent faces normally probe the same leaf. The union belongs to a
		// specific plane and pass; sharing this memo across planes loses vis.
		if (teleport_planes[index].pvsleaf[pass] == leaf)
			continue;
		src = r_novis.value ? Mod_NoVisPVS(cl.worldmodel) : Mod_LeafPVS(leaf, cl.worldmodel);
		for (j = 0; j < teleport_pvsbytes; j++)
			dst[j] |= src[j];
		teleport_planes[index].pvsleaf[pass] = leaf;
	}
}

static void R_TeleportCollect (msurface_t *surf, const float *matrix)
{
	vec3_t normal, center;
	float mins[2], maxs[2];
	float dist;
	int index, i;
	teleport_plane_t *plane;
	if (!(surf->flags & SURF_DRAWTELE) || !surf->polys || !surf->texinfo->texture->tele_normal)
		return;
	R_TeleportPlane(surf, matrix, normal, &dist, center);
	if (DotProduct(r_refdef.vieworg, normal) - dist < 0)
		return;
	R_TeleportScreenBounds(surf, matrix, mins, maxs);
	index = R_TeleportFindPlane(normal, dist);
	if (index >= 0)
	{
		plane = &teleport_planes[index];
		for (i = 0; i < 2; i++)
		{
			plane->screenmins[i] = q_min(plane->screenmins[i], mins[i]);
			plane->screenmaxs[i] = q_max(plane->screenmaxs[i], maxs[i]);
		}
		R_TeleportMergePVS(index, center);
		return;
	}
	if (teleport_numplanes == MAX_TELEPORT_PLANES)
		return;
	plane = &teleport_planes[teleport_numplanes++];
	VectorCopy(normal, plane->normal);
	VectorCopy(center, plane->center);
	plane->dist = dist;
	memcpy(plane->screenmins, mins, sizeof(mins));
	memcpy(plane->screenmaxs, maxs, sizeof(maxs));
	plane->pvsleaf[0] = plane->pvsleaf[1] = NULL;
	plane->ready = false;
	R_TeleportMergePVS(teleport_numplanes - 1, center);
}

/* The shader samples at most 0.1 * abs(strength) away from the projected face:
 * its distortion normal is normalized. Include the vertical shader offset and
 * two texels for linear filtering/roundoff, preserving full target resolution.
 */
static void R_TeleportScissorBounds (const teleport_plane_t *plane, int image, int lo[2], int hi[2])
{
	float strength = image ? TELEPORT_REFLECT_STRENGTH : TELEPORT_REFRACT_STRENGTH;
	int i, size[2] = {teleport_width, teleport_height};
	for (i = 0; i < 2; i++)
	{
		float margin = 0.1f * fabsf(strength) + 2.0f / size[i] + (i ? 1.5f / 1080.0f : 0.0f);
		if (!isfinite(margin))
		{
			lo[i] = 0;
			hi[i] = size[i];
		}
		else
		{
			lo[i] = (int)floorf(CLAMP(0.0f, plane->screenmins[i] - margin, 1.0f) * size[i]);
			hi[i] = (int)ceilf(CLAMP(0.0f, plane->screenmaxs[i] + margin, 1.0f) * size[i]);
		}
	}
}

static void R_TeleportScissor (const teleport_plane_t *plane, int image)
{
	int lo[2], hi[2];
	R_TeleportScissorBounds(plane, image, lo, hi);
	glScissor(lo[0], lo[1], hi[0] - lo[0], hi[1] - lo[1]);
	glEnable(GL_SCISSOR_TEST);
}

/* Cull before chaining surfaces/updating lightmaps/submitting draws. Scissoring
 * alone only saves fragments: it still sends the entire view through the CPU
 * and driver. Extract planes from the actual reflected, skewed and obliquely
 * clipped matrix, restricted to the same distortion-padded sample rectangle.
 */
static void R_TeleportFrustum (const teleport_plane_t *plane, int image)
{
	mat4_t matrix;
	int lo[2], hi[2], i, j;
	float bounds[4];
	R_TeleportScissorBounds(plane, image, lo, hi);
	bounds[0] = 2.0f * lo[0] / teleport_width - 1.0f;
	bounds[1] = 2.0f * hi[0] / teleport_width - 1.0f;
	bounds[2] = 2.0f * lo[1] / teleport_height - 1.0f;
	bounds[3] = 2.0f * hi[1] / teleport_height - 1.0f;
	Matrix4_Multiply(teleport_projection, teleport_viewmatrix, matrix);
	for (i = 0; i < 5; i++)
	{
		vec4_t equation;
		float length;
		mplane_t *p = &frustum[i];
		for (j = 0; j < 4; j++)
		{
			if (i == 4)
				equation[j] = matrix[j*4+3] + matrix[j*4+2]; // near: z >= -w
			else
				equation[j] = (i & 1 ? -1.0f : 1.0f) *
					(matrix[j*4+i/2] - bounds[i] * matrix[j*4+3]);
		}
		VectorCopy(equation, p->normal);
		length = VectorNormalize(p->normal);
		p->dist = length > 0 ? -equation[3] / length - 0.01f : 0;
		p->type = PLANE_ANYZ;
		p->signbits = (p->normal[0] < 0) | ((p->normal[1] < 0) << 1) | ((p->normal[2] < 0) << 2);
	}
	r_frustumplanes = 5;
}

static qboolean R_TeleportTarget (teleport_plane_t *plane, int image)
{
	qboolean newfbo = !teleport_fbo;
	qboolean newimage = !plane->image[image];
	if (!teleport_fbo)
	{
		GL_GenFramebuffersFunc(1, &teleport_fbo);
		GL_GenRenderbuffersFunc(1, &teleport_depth);
		GL_BindRenderbufferFunc(GL_RENDERBUFFER, teleport_depth);
		GL_RenderbufferStorageFunc(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, teleport_width, teleport_height);
	}
	if (!plane->image[image])
	{
		glGenTextures(1, &plane->image[image]);
		GL_SelectTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, plane->image[image]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, teleport_width, teleport_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		GL_ClearBindings();
	}
	GL_BindFramebufferFunc(GL_FRAMEBUFFER, teleport_fbo);
	GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, plane->image[image], 0);
	if (newfbo)
	{
		GL_FramebufferRenderbufferFunc(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, teleport_depth);
		GL_FramebufferRenderbufferFunc(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, teleport_depth);
	}
	/* All targets retain the same size/format until DeleteTargets. Validate
	 * each new attachment once instead of querying the driver for every view. */
	if ((newfbo || newimage) && GL_CheckFramebufferStatusFunc(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		Con_Warning("Teleporter framebuffer unavailable; using classic teleporters\n");
		teleport_failed = true;
		return false;
	}
	R_TeleportScissor(plane, image);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glStencilMask(~0u);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	return true;
}

void R_TeleportSetupGL (void)
{
	if (!r_teleport_view)
		return;
	memcpy(r_world_matrix, teleport_viewmatrix, sizeof(mat4_t));
	memcpy(r_projection_matrix, teleport_projection, sizeof(mat4_t));
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(r_projection_matrix);
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(r_world_matrix);
	glViewport(0, 0, teleport_width, teleport_height);
	r_viewport[0] = r_viewport[1] = 0;
	r_viewport[2] = teleport_width;
	r_viewport[3] = teleport_height;
	glFrontFace(r_teleport_reflection ? GL_CCW : GL_CW);
}

/* Replace the near plane, so GLSL and fixed-function geometry are both clipped. */
static void R_TeleportClip (const vec3_t normal, float dist)
{
	vec4_t p, q;
	vec3_t point;
	float scale;
	int i;
	for (i = 0; i < 3; i++)
	{
		p[i] = teleport_viewmatrix[i] * normal[0] + teleport_viewmatrix[4+i] * normal[1] + teleport_viewmatrix[8+i] * normal[2];
		point[i] = dist * p[i] + teleport_viewmatrix[12+i];
	}
	p[3] = -DotProduct(point, p);
	q[0] = ((p[0] < 0 ? -1.0f : 1.0f) + teleport_projection[8]) / teleport_projection[0];
	q[1] = ((p[1] < 0 ? -1.0f : 1.0f) + teleport_projection[9]) / teleport_projection[5];
	q[2] = -1.0f;
	q[3] = (1.0f + teleport_projection[10]) / teleport_projection[14];
	scale = p[0]*q[0] + p[1]*q[1] + p[2]*q[2] + p[3]*q[3];
	if (fabsf(scale) < 0.00001f)
		return;
	scale = 2.0f / scale;
	teleport_projection[2] = p[0] * scale;
	teleport_projection[6] = p[1] * scale;
	teleport_projection[10] = p[2] * scale + 1.0f;
	teleport_projection[14] = p[3] * scale;
}

static qboolean R_TeleportReserveStorage (size_t ents, size_t visbytes)
{
	if (ents > teleport_maxents)
	{
		entity_t **grown;
		if (ents > (size_t)-1 / sizeof(*grown))
			return false;
		grown = realloc(teleport_savedents, ents * sizeof(*grown));
		if (!grown)
			return false;
		teleport_savedents = grown;
		teleport_maxents = ents;
	}
	if (visbytes > teleport_visbytes)
	{
		byte *grown = realloc(teleport_vis, visbytes);
		if (!grown)
			return false;
		teleport_vis = grown;
		teleport_visbytes = visbytes;
	}
	return true;
}

/* This snapshot also survives Host_Error's longjmp. Restore it before error
 * handling draws the console or clears the client state. */
static struct
{
	qboolean active, skyroom;
	refdef_t savedref;
	mat4_t savedview, savedprojection;
	vec3_t savedorigin, savedvpn, savedright, savedup;
	mleaf_t *savedleaf;
	entity_t *savedentity;
	GLint savedfbo, savedrenderbuffer, savedfront, savedscissor[4], stencilmask;
	GLfloat clearcolor[4];
	GLboolean colormask[4], scissor, depthmask, stenciltest;
	int savedcount;
} teleport_restore;

static void R_TeleportRestoreView (void)
{
	r_teleport_pvs = NULL;
	r_teleport_reflection = r_teleport_view = false;
	r_refdef = teleport_restore.savedref;
	r_viewleaf = teleport_restore.savedleaf;
	VectorCopy(teleport_restore.savedorigin, r_origin);
	VectorCopy(teleport_restore.savedvpn, vpn);
	VectorCopy(teleport_restore.savedright, vright);
	VectorCopy(teleport_restore.savedup, vup);
	skyroom_drawn = teleport_restore.skyroom;
	R_SetFrustum(r_fovx, r_fovy);
	GL_BindFramebufferFunc(GL_FRAMEBUFFER, teleport_restore.savedfbo);
	GL_BindRenderbufferFunc(GL_RENDERBUFFER, teleport_restore.savedrenderbuffer);
	glFrontFace(teleport_restore.savedfront);
	glClearColor(teleport_restore.clearcolor[0], teleport_restore.clearcolor[1], teleport_restore.clearcolor[2], teleport_restore.clearcolor[3]);
	glColorMask(teleport_restore.colormask[0], teleport_restore.colormask[1], teleport_restore.colormask[2], teleport_restore.colormask[3]);
	glScissor(teleport_restore.savedscissor[0], teleport_restore.savedscissor[1], teleport_restore.savedscissor[2], teleport_restore.savedscissor[3]);
	if (teleport_restore.scissor)
		glEnable(GL_SCISSOR_TEST);
	else
		glDisable(GL_SCISSOR_TEST);
	R_SetupGL();
	if (teleport_restore.stenciltest)
		glEnable(GL_STENCIL_TEST);
	else
		glDisable(GL_STENCIL_TEST);
	glDepthMask(teleport_restore.depthmask);
	glStencilMask(teleport_restore.stencilmask);
	currententity = teleport_restore.savedentity;
}

void R_TeleportAbort (void)
{
	if (!teleport_restore.active)
		return;
	R_TeleportRestoreView();
	memcpy(cl_visedicts, teleport_savedents, teleport_restore.savedcount * sizeof(*cl_visedicts));
	cl_numvisedicts = teleport_restore.savedcount;
#ifndef SDL_THREADS_DISABLED
	RSceneCache_AbortTeleport();
#endif
	GL_UseProgramFunc(0);
	GL_ClearBindings();
	teleport_numplanes = 0;
	teleport_restore.active = false;
}

void R_TeleportPrepare (void)
{
	mat4_t mirror;
	entity_t **savedents, reflectedplayer;
	int i, j, k, pass, width, height;

	teleport_numplanes = 0;
	if (!R_TeleportActive() || !r_refdef.drawworld || !r_drawworld_cheatsafe || r_drawflat_cheatsafe || r_lightmap_cheatsafe)
		return;
	width = CLAMP(1, (int)(r_viewport[2] * TELEPORT_TARGET_SCALE), gl_hardware_maxsize);
	height = CLAMP(1, (int)(r_viewport[3] * TELEPORT_TARGET_SCALE), gl_hardware_maxsize);
	if (width != teleport_width || height != teleport_height)
	{
		R_TeleportDeleteTargets();
		teleport_width = width;
		teleport_height = height;
	}
	teleport_restore.savedcount = cl_numvisedicts;
	teleport_pvsbytes = (cl.worldmodel->numleafs + 7) / 8;
	if (!R_TeleportReserveStorage(q_max(1, teleport_restore.savedcount),
		q_max(1, teleport_pvsbytes) * (size_t)(MAX_TELEPORT_PLANES * 2)))
		return;
	memset(teleport_vis, 0, teleport_visbytes);
	Matrix4_Multiply(r_projection_matrix, r_world_matrix, teleport_screenmatrix);
	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		texture_t *t = cl.worldmodel->textures[i];
		msurface_t *s;
		if (t && t->tele_normal)
			for (s = t->texturechains[chain_world]; s; s = s->texturechain)
				R_TeleportCollect(s, NULL);
	}
	if (r_drawentities.value)
	{
		for (i = 0; i < cl_numvisedicts; i++)
		{
			entity_t *e = cl_visedicts[i];
			mat4_t matrix;
			qboolean have_matrix = false;
			if (!e->model || e->model->type != mod_brush ||
				!e->model->hastelesurfaces || (e->eflags & EFLAGS_EXTERIORMODEL) || R_CullModelForEntity(e))
				continue;
			for (j = 0; j < e->model->nummodelsurfaces; j++)
			{
				msurface_t *surf = e->model->surfaces + e->model->firstmodelsurface + j;
				if (!(surf->flags & SURF_DRAWTELE))
					continue;
				if (!have_matrix)
				{
					R_TeleportEntityMatrix(e, matrix);
					have_matrix = true;
				}
				R_TeleportCollect(surf, matrix);
			}
		}
	}
	/* Normally warmed during setup; retain a fallback for style changes made
	 * between map setup and completed signon, or by a custom client scene. */
	if (!teleport_numplanes || !R_TeleportCreateShader())
		return;

	savedents = teleport_savedents;
	memcpy(savedents, cl_visedicts, teleport_restore.savedcount * sizeof(*savedents));
	teleport_restore.savedentity = currententity;
	teleport_restore.savedref = r_refdef;
	teleport_restore.savedleaf = r_viewleaf;
	VectorCopy(r_origin, teleport_restore.savedorigin);
	VectorCopy(vpn, teleport_restore.savedvpn);
	VectorCopy(vright, teleport_restore.savedright);
	VectorCopy(vup, teleport_restore.savedup);
	memcpy(teleport_restore.savedview, r_world_matrix, sizeof(mat4_t));
	memcpy(teleport_restore.savedprojection, r_projection_matrix, sizeof(mat4_t));
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &teleport_restore.savedfbo);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &teleport_restore.savedrenderbuffer);
	glGetIntegerv(GL_FRONT_FACE, &teleport_restore.savedfront);
	glGetIntegerv(GL_SCISSOR_BOX, teleport_restore.savedscissor);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, teleport_restore.clearcolor);
	glGetBooleanv(GL_COLOR_WRITEMASK, teleport_restore.colormask);
	teleport_restore.scissor = glIsEnabled(GL_SCISSOR_TEST);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &teleport_restore.depthmask);
	glGetIntegerv(GL_STENCIL_WRITEMASK, &teleport_restore.stencilmask);
	teleport_restore.stenciltest = glIsEnabled(GL_STENCIL_TEST);
	teleport_restore.skyroom = skyroom_drawn;
	teleport_restore.active = true;
	skyroom_drawn = false;
	r_teleport_view = true;

	for (i = 0; i < teleport_numplanes && !teleport_failed; i++)
	{
		teleport_plane_t *plane = &teleport_planes[i];
		for (pass = 0; pass < ((int)r_telestyle.value == 3 ? 2 : 1); pass++)
		{
			vec3_t clipnormal, pvsorigin;
			float clipdist;
			if (!R_TeleportTarget(plane, pass))
				break;
			r_refdef = teleport_restore.savedref;
			VectorCopy(teleport_restore.savedvpn, vpn);
			VectorCopy(teleport_restore.savedright, vright);
			VectorCopy(teleport_restore.savedup, vup);
			memcpy(teleport_viewmatrix, teleport_restore.savedview, sizeof(mat4_t));
			memcpy(teleport_projection, teleport_restore.savedprojection, sizeof(mat4_t));
			r_teleport_reflection = pass == 1;
			VectorScale(plane->normal, pass ? 1.0f : -1.0f, clipnormal);
			clipdist = pass ? plane->dist : -plane->dist;
			VectorMA(plane->center, 0.1f, clipnormal, pvsorigin);
			r_viewleaf = Mod_PointInLeaf(pvsorigin, cl.worldmodel);
			if (r_viewleaf->contents == CONTENTS_SOLID || r_viewleaf->contents == CONTENTS_SKY)
				r_viewleaf = teleport_restore.savedleaf;
			r_teleport_pvs = teleport_vis + (i * 2 + pass) * (size_t)teleport_pvsbytes;
			if (++r_teleport_visframe == 0)
			{
				/* Keep zero available for newly allocated entities on wraparound. */
				for (j = 0; j < cl.num_statics; j++)
					cl.static_entities[j].ent->teleportvisframe = 0;
				r_teleport_visframe = 1;
			}
			if (pass)
			{
				memset(mirror, 0, sizeof(mirror));
				for (j = 0; j < 3; j++)
				{
					for (k = 0; k < 3; k++)
						mirror[j*4+k] = (j == k ? 1.0f : 0.0f) - 2.0f * plane->normal[j] * plane->normal[k];
					mirror[12+j] = 2.0f * plane->dist * plane->normal[j];
				}
				mirror[15] = 1.0f;
				Matrix4_Multiply(teleport_restore.savedview, mirror, teleport_viewmatrix);
				VectorMA(teleport_restore.savedref.vieworg, -2.0f * (DotProduct(teleport_restore.savedref.vieworg, plane->normal) - plane->dist), plane->normal, r_refdef.vieworg);
				VectorMA(teleport_restore.savedvpn, -2.0f * DotProduct(teleport_restore.savedvpn, plane->normal), plane->normal, vpn);
				VectorMA(teleport_restore.savedright, -2.0f * DotProduct(teleport_restore.savedright, plane->normal), plane->normal, vright);
				VectorMA(teleport_restore.savedup, -2.0f * DotProduct(teleport_restore.savedup, plane->normal), plane->normal, vup);
				VectorAngles(vpn, vup, r_refdef.viewangles);
			}
			VectorCopy(r_refdef.vieworg, r_origin);
			R_SetFrustum(r_fovx, r_fovy);
			R_TeleportClip(clipnormal, clipdist);
			R_TeleportFrustum(plane, pass);
			cl_numvisedicts = 0;
			for (j = 0; j < teleport_restore.savedcount; j++)
				if (!savedents[j]->is_static)
					cl_visedicts[cl_numvisedicts++] = savedents[j];
			/* CL_RelinkEntities hides our body in the first-person view. */
			if (pass && cl.viewentity > 0 && cl.viewentity < cl.num_entities && cl_numvisedicts < cl_maxvisedicts)
			{
				for (j = 0; j < cl_numvisedicts; j++)
					if (cl_visedicts[j] == &cl.entities[cl.viewentity])
						break;
				reflectedplayer = cl.entities[cl.viewentity];
				reflectedplayer.angles[0] *= 0.3f;
				cl_visedicts[j] = &reflectedplayer;
				if (j == cl_numvisedicts)
					cl_numvisedicts++;
			}
			R_MarkSurfaces();
			R_RenderScene();
		}
		plane->ready = !teleport_failed;
	}

	R_TeleportRestoreView();
	/* Static entity stamps remain the main view's stamps. Rebuild chains with
	 * an empty list, then restore exactly the original visible entities. */
	cl_numvisedicts = 0;
	R_MarkSurfaces();
	memcpy(cl_visedicts, savedents, teleport_restore.savedcount * sizeof(*savedents));
	cl_numvisedicts = teleport_restore.savedcount;
	teleport_restore.active = false;
}

qboolean R_TeleportDrawChain (msurface_t *chain, entity_t *ent)
{
	msurface_t *s;
	mat4_t inverse, entitymatrix;
	vec4_t worldeye, eye;
	const float *matrix = NULL;
	vec3_t normal, center;
	float dist, alpha;
	int index;
	if (r_teleport_view)
		return true; /* Match FTE's default one-level portal recursion. */
	if (skyroom_drawing || !R_TeleportActive() || !teleport_program || !chain->texinfo->texture->tele_normal)
		return false;
	if (ent)
	{
		R_TeleportEntityMatrix(ent, entitymatrix);
		matrix = entitymatrix;
	}
	for (s = chain; s; s = s->texturechain)
	{
		R_TeleportPlane(s, matrix, normal, &dist, center);
		index = R_TeleportFindPlane(normal, dist);
		if (index < 0 || !teleport_planes[index].ready)
			return false;
	}
	VectorCopy(r_refdef.vieworg, worldeye); // includes the current stereo eye offset
	worldeye[3] = 1;
	if (matrix)
	{
		if (!Matrix4_Invert(matrix, inverse))
			return false;
		Matrix4_Transform4(inverse, worldeye, eye);
	}
	else
		memcpy(eye, worldeye, sizeof(eye));
	alpha = GL_WaterAlphaForEntitySurface(ent, chain);
	GL_DisableMultitexture();
	GL_UseProgramFunc(teleport_program);
	GL_Uniform1fFunc(teleport_time, cl.time);
	GL_Uniform3fFunc(teleport_eye, eye[0], eye[1], eye[2]);
	GL_Uniform1fFunc(teleport_alpha, alpha);
	GL_Uniform2fFunc(teleport_strength, TELEPORT_REFRACT_STRENGTH, TELEPORT_REFLECT_STRENGTH);
	GL_Uniform1iFunc(teleport_reflect, (int)r_telestyle.value == 3);
	GL_Uniform1iFunc(teleport_fogmode, Fog_GetMode());
	GL_DisableVertexAttribArrayFunc(0);
	if (alpha < 1)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
	glDepthMask(alpha < 1 ? GL_FALSE : GL_TRUE);
	GL_SelectTexture(GL_TEXTURE2);
	GL_Bind(chain->texinfo->texture->tele_normal);
	GL_SelectTexture(GL_TEXTURE3);
	GL_Bind(chain->texinfo->texture->gltexture);
	for (s = chain; s; s = s->texturechain)
	{
		R_TeleportPlane(s, matrix, normal, &dist, center);
		index = R_TeleportFindPlane(normal, dist);
		GL_SelectTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, teleport_planes[index].image[(int)r_telestyle.value == 3 ? 1 : 0]);
		GL_SelectTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, teleport_planes[index].image[0]);
		/* FTE's vertex normal and Eye are both in model space here. */
		VectorScale(s->plane->normal, (s->flags & SURF_PLANEBACK) ? -1.0f : 1.0f, normal);
		GL_Uniform3fFunc(teleport_normal, normal[0], normal[1], normal[2]);
		DrawGLPoly(s->polys);
		rs_brushpasses++;
	}
	GL_UseProgramFunc(0);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	GL_ClearBindings();
	return true;
}
