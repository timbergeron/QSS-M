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
//gl_sky.c

#include "quakedef.h"
#include "q_ctype.h"
#include <time.h>

#define	MAX_CLIP_VERTS 64

void Sky_EmitSkyBoxVertex (float s, float t, int axis);

extern	int	rs_skypolys; // for r_speeds readout
extern	int	rs_skypasses; // for r_speeds readout

float	skyflatcolor[3];
static float	skymins[2][6], skymaxs[2][6];

qboolean skyroom_drawing;
qboolean skyroom_drawn;
qboolean skyroom_enabled;
vec4_t skyroom_origin;
vec4_t skyroom_orientation;
static vec4_t skybox_orientation;

char	skybox_name[1024]; //name of current skybox, or "" if no skybox
qboolean externalskyloaded; // woods #fastsky2

static gltexture_t	*skybox_textures[6];
static qboolean	skybox_complete; // all 6 faces loaded; allows reuse across map changes
static gltexture_t	*solidskytexture, *alphaskytexture;

static const char	*suf[6];

cvar_t r_fastsky = {"r_fastsky", "0", CVAR_ARCHIVE};
cvar_t r_fastskycolor = {"r_fastskycolor", "", CVAR_ARCHIVE}; // woods #fastskycolor
static cvar_t r_sky_quality = {"r_sky_quality", "12", CVAR_NONE};
cvar_t r_skyalpha = {"r_skyalpha", "1", CVAR_NONE};
cvar_t r_skyfog = {"r_skyfog","0.5",CVAR_ARCHIVE};
cvar_t r_skyspeed = {"r_skyspeed","1",CVAR_ARCHIVE}; // woods #skyspeed
cvar_t r_skywind = {"r_skywind", "0", CVAR_ARCHIVE};
cvar_t allow_download_sky = {"allow_download_sky", "1", CVAR_ARCHIVE}; // woods automatic skybox downloading #skydownloads
cvar_t r_globalsky = {"r_globalsky", "", CVAR_ARCHIVE};

qboolean Sky_DownloadSkybox(const char* name);
extern cvar_t	cl_web_download_url;
extern cvar_t	cl_web_download_url2;
extern qboolean IsGithubRepoPath(const char* s);
static char pending_skybox_name[1024];
static qboolean skybox_download_pending = false;
static char map_skybox_name[1024];
extern qboolean Curl_DownloadFile(const char* url, const char* filename, const char* local_path, qboolean is_skybox, const char* display_name, web_download_result_t *result_out);
extern qboolean scr_disabled_for_loading;

static void Sky_ApplyGlobalSkybox(void);
static void Sky_GlobalSkyboxChanged(cvar_t *var);
static void Sky_SetMapSkybox(const char *name);
static qboolean Sky_HasAllSkyboxFaces(const char *name);

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(arr)   (sizeof(arr) / sizeof((arr)[0]))
#endif

#define MAX_MAP_SKYBOX_CHOICES 16

static qboolean Sky_IsNumberedSkyboxKey(const char *key, const char *prefix)
{
	const char *p;

	if (!key || !prefix || !*key || !*prefix)
		return false;

	while (*prefix)
	{
		if (!*key)
			return false;
		if (q_tolower((unsigned char)*key) != q_tolower((unsigned char)*prefix))
			return false;
		key++;
		prefix++;
	}

	if (!*key)
		return false; // plain "sky" / "skybox" is handled separately

	for (p = key; *p; p++)
	{
		if (!q_isdigit((unsigned char)*p))
			return false;
	}

	return true;
}

// Recognises every worldspawn key that names a skybox (incl. numbered variants
// like sky1 / skybox2). Shared with the skybox auto-downloader in cl_main.c so
// the two stay in sync about what counts as a sky key.
qboolean Sky_IsSkyboxWorldspawnKey(const char *key)
{
	if (!key || !*key)
		return false;

	if (!q_strcasecmp("sky", key) || !q_strcasecmp("skybox", key))
		return true;

	if (!q_strcasecmp("skyname", key) || !q_strcasecmp("qlsky", key)) //half-life / quake lives
		return true;

	if (Sky_IsNumberedSkyboxKey(key, "sky"))
		return true;

	if (Sky_IsNumberedSkyboxKey(key, "skybox"))
		return true;

	return false;
}

// True only for the numbered opt-in variants (sky1, skybox2, ...). The bare
// aliases (sky/skybox/skyname/qlsky) are deliberately excluded so they keep
// legacy last-wins behaviour unless they carry a multi-value list.
static qboolean Sky_IsNumberedSkyboxWorldspawnKey(const char *key)
{
	return Sky_IsNumberedSkyboxKey(key, "sky") || Sky_IsNumberedSkyboxKey(key, "skybox");
}

// Counts how many whitespace/comma/semicolon separated tokens a value holds,
// without allocating or warning. Used to decide whether a bare alias opts into
// randomization (a list) or stays a single legacy value.
static int Sky_CountSkyboxTokens(const char *value)
{
	const char *p = value;
	int n = 0;

	if (!p)
		return 0;

	while (*p)
	{
		while (*p && (q_isspace((unsigned char)*p) || *p == ',' || *p == ';'))
			p++;
		if (!*p)
			break;
		n++;
		while (*p && !q_isspace((unsigned char)*p) && *p != ',' && *p != ';')
			p++;
	}

	return n;
}

// Parses a worldspawn skybox value (space/comma/semicolon separated) and appends
// each unique name (case-insensitive) to choices[], up to maxchoices. Returns the
// new count. Shared by the render path (Sky_NewMap) and the auto-downloader
// (CL_CheckDownloads) so both dedupe and cap identically.
int Sky_AddMapSkyboxChoices(char choices[][MAX_QPATH], int count, int maxchoices, const char *value)
{
	char list[4096];
	char *p;

	if (!value || !value[0])
		return count;

	q_strlcpy(list, value, sizeof(list));
	p = list;

	while (*p)
	{
		char *start;
		int i;

		while (*p && (q_isspace((unsigned char)*p) || *p == ',' || *p == ';'))
			p++;

		if (!*p)
			break;

		start = p;

		while (*p && !q_isspace((unsigned char)*p) && *p != ',' && *p != ';')
			p++;

		if (*p)
			*p++ = 0;

		if (!start[0])
			continue;

		if (strlen(start) >= MAX_QPATH)
		{
			Con_Warning("skybox name too long; ignoring random skybox entry\n");
			continue;
		}

		for (i = 0; i < count; i++)
		{
			if (!q_strcasecmp(choices[i], start))
				break;
		}
		if (i < count)
			continue;

		if (count >= maxchoices)
		{
			Con_Warning("too many random skyboxes; ignoring \"%s\"\n", start);
			continue;
		}

		q_strlcpy(choices[count], start, MAX_QPATH);
		count++;
	}

	return count;
}

static int Sky_RandomChoiceIndex(int count)
{
	unsigned seed;

	if (count <= 1)
		return 0;

	seed = (unsigned)(Sys_DoubleTime() * 1000000.0);
	seed ^= (unsigned)rand();

	return (int)(seed % (unsigned)count);
}

// Picks one skybox out of the opt-in candidate pool gathered from worldspawn.
//
// Randomization is OPT-IN (see Sky_NewMap): it only happens when a map uses
// numbered keys (sky1/skybox2/...) or gives a bare key a multi-value list. A
// legacy map with single-valued sky/skybox/skyname/qlsky keys never reaches here
// and keeps the old "last recognized key wins" precedence.
//
// We prefer choices whose faces are all present locally so we never render a
// broken sky. The auto-downloader (CL_CheckDownloads) fetches *every* listed
// skybox up front, so once downloads finish the whole list is local and the pick
// randomizes across all of them. If downloads are disabled/failed we only pick
// from what is actually on disk, which is the desired fallback.
static void Sky_SetRandomMapSkybox(char choices[MAX_MAP_SKYBOX_CHOICES][MAX_QPATH], int count)
{
	int complete[MAX_MAP_SKYBOX_CHOICES];
	int complete_count = 0;
	int choice;
	int i;

	if (count <= 0)
		return;

	for (i = 0; i < count; i++)
	{
		if (Sky_HasAllSkyboxFaces(choices[i]))
			complete[complete_count++] = i;
	}

	if (complete_count > 0)
	{
		choice = complete[Sky_RandomChoiceIndex(complete_count)];
		if (complete_count != count)
			Con_DPrintf("random skybox: using %d complete local choices out of %d listed\n", complete_count, count);
	}
	else
	{
		choice = Sky_RandomChoiceIndex(count);
	}

	Sky_SetMapSkybox(choices[choice]);

	if (count > 1)
		Con_DPrintf("random skybox: %s (%d of %d)\n", choices[choice], choice + 1, count);
}

static const int skytexorder[6] = {0,2,1,3,4,5}; //for skybox

static const vec3_t skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1}
};

static const int st_to_vec[6][3] =
{
	{3,-1,2},
	{-3,1,2},
	{1,3,2},
	{-1,-3,2},
	{-2,-1,3},		// straight up
	{2,-1,-3}		// straight down
};

static const int vec_to_st[6][3] =
{
	{-2,3,1},
	{2,3,-1},
	{1,3,2},
	{-1,3,-2},
	{-2,-1,3},
	{-2,1,-3}
};

static float	skyfog; // ericw

static void Sky_ClearSkyboxOrientation(void)
{
	skybox_orientation[0] = 0.0f;
	skybox_orientation[1] = 0.0f;
	skybox_orientation[2] = 0.0f;
	skybox_orientation[3] = 0.0f;
}

static qboolean Sky_SkyboxRotates(void)
{
	return skybox_orientation[3] != 0.0f && !skyroom_drawing;
}

static void Sky_ParseSkyboxAxis(const char *value)
{
	int i;

	skybox_orientation[0] = 0.0f;
	skybox_orientation[1] = 0.0f;
	skybox_orientation[2] = 0.0f;

	for (i = 0; i < 3; i++)
	{
		value = COM_Parse(value);
		if (!value)
			break;
		skybox_orientation[i] = (float)atof(com_token);
	}
}

static void Sky_RotateSkyboxDir(const vec3_t dir, vec3_t out)
{
	vec3_t axis;

	if (!Sky_SkyboxRotates())
	{
		VectorCopy(dir, out);
		return;
	}

	VectorCopy(skybox_orientation, axis);
	if (!axis[0] && !axis[1] && !axis[2])
		axis[2] = 1.0f;
	else
		VectorNormalize(axis);

	RotatePointAroundVector(out, axis, dir, skybox_orientation[3] * cl.time);
}

static void Sky_ExpandSkyboxBoundsForRotation(void)
{
	int i;

	if (!Sky_SkyboxRotates())
		return;

	for (i = 0; i < 6; i++)
	{
		if (skymins[0][i] < skymaxs[0][i] && skymins[1][i] < skymaxs[1][i])
			break;
	}
	if (i == 6)
		return;

	for (i = 0; i < 6; i++)
	{
		skymins[0][i] = -1;
		skymins[1][i] = -1;
		skymaxs[0][i] = 1;
		skymaxs[1][i] = 1;
	}
}

static float skywind_dist = 0.0f;
static float skywind_yaw = 45.0f;
static float skywind_pitch = 0.0f;
static float skywind_period = 30.0f;
typedef enum
{
	SKYWIND_SRC_NONE = 0,
	SKYWIND_SRC_DEFAULT,
	SKYWIND_SRC_CONFIG,
	SKYWIND_SRC_WORLDSPAWN,
	SKYWIND_SRC_COMMAND
} skywind_source_t;
static skywind_source_t skywind_source = SKYWIND_SRC_NONE;

#define SKYWIND_CFG "wind.cfg"
#define SKYWIND_DEFAULT_CFG "gfx/env/skywind_default.cfg"

static qboolean skywind_apply_offsets = false;
static qboolean skywind_shader_enabled = false;
static GLuint skywind_program = 0;
static GLint skywind_uniform_sampler = -1;
static GLint skywind_uniform_phase = -1;
static GLint skywind_uniform_fogcolor = -1;
static GLint skywind_uniform_fogdensity = -1;
static qboolean skywind_shader_initialized = false;
static GLuint skywind_cubemap_program = 0;
static GLint skywind_cubemap_uniform_sampler = -1;
static GLint skywind_cubemap_uniform_phase = -1;
static GLint skywind_cubemap_uniform_winddir = -1;
static GLint skywind_cubemap_uniform_fogcolor = -1;
static GLint skywind_cubemap_uniform_fogdensity = -1;
static qboolean skywind_cubemap_shader_initialized = false;
static qboolean skywind_cubemap_sampling = false;
static int skywind_frame_serial = -1;
static qboolean skywind_frame_valid = false;
static vec3_t skywind_frame_dir = {0.0f, 0.0f, 0.0f};
static float skywind_frame_phase = 0.0f;
static float skywind_primary_phase = 0.0f;
static float skywind_secondary_phase = 0.0f;
static GLuint skybox_cubemap = 0;
static qboolean skybox_cubemap_attempted = false;

static void Skywind_InvalidateFrame(void);
static void Skywind_UpdateFrame(void);
void Skywind_SetupFrame(void);
static void Skywind_Clear(void);
static void Sky_FreeCubemap(void);
static qboolean Sky_CreateCubemap(byte *data[6], int width[6], int height[6], enum srcformat fmt[6], int samesize);
static void Sky_TryRebuildCubemap(void);
static qboolean Skywind_HasSkybox(void);
static qboolean Skywind_Active(void);
static qboolean Skywind_GetDirectionAndPhase(vec3_t wind_dir, float *wind_phase);
static float Skywind_Normalize360(float value);
static float Skywind_NormalizePitch(float value);
static float Skywind_WrapCoord(float coord);
static void Skywind_ProjectDirToST(const vec3_t dir, int axis, float *s, float *t);
static float Skywind_ComputeSecondaryOffset(float phase);
static qboolean Skywind_ParseCvarDefaults(float *dist, float *yaw, float *period, float *pitch);
static float Skywind_GetRate(void);
static qboolean Skywind_EnsureShader(void);
static qboolean Skywind_EnsureCubemapShader(void);
static qboolean Skywind_DrawSkyBox_Cubemap(const vec3_t wind_dir, float phase);
static qboolean Skywind_DrawSkyBox_Shader(const vec3_t wind_dir, float phase);
static qboolean Skywind_LoadConfigInternal(qboolean quiet);
static void Skywind_LoadConfig(void);
static void Skywind_Load_f(void);
static void Skywind_Save_f(void);
static void Skywind_LookDir_f(void);
static void Skywind_Rotate_f(void);
static void Skywind_f(void);
static void Skywind_Cvar_OnChange(cvar_t *var);
static qboolean Skywind_ApplyCvarDefaultsInternal(qboolean quiet);
static qboolean Skywind_ApplyWorldspawn(const char *value);
static void Sky_DrawSkyBox_Static(void);
static void Sky_DrawSkyBoxFogOverlay(void);

static float Skywind_Normalize360(float value)
{
	value = fmodf(value, 360.0f);
	if (value < 0.0f)
		value += 360.0f;
	return value;
}

static float Skywind_NormalizePitch(float value)
{
	value = fmodf(value + 90.0f, 180.0f);
	if (value < 0.0f)
		value += 180.0f;
	return value - 90.0f;
}

static void Skywind_InvalidateFrame(void)
{
	skywind_frame_serial = -1;
	skywind_frame_valid = false;
}

static void Skywind_Clear(void)
{
	skywind_dist = 0.0f;
	skywind_yaw = 45.0f;
	skywind_pitch = 0.0f;
	skywind_period = 30.0f;
	skywind_apply_offsets = false;
	skywind_shader_enabled = false;
	skywind_primary_phase = 0.0f;
	skywind_secondary_phase = 0.0f;
	skywind_source = SKYWIND_SRC_NONE;
	Skywind_InvalidateFrame();
}

static void Sky_FreeCubemap(void)
{
	if (skybox_cubemap)
	{
		glDeleteTextures(1, &skybox_cubemap);
		skybox_cubemap = 0;
		GL_ClearBindings();
	}
}

static void Sky_TryRebuildCubemap(void)
{
	int i, mark;
	int width[6], height[6];
	enum srcformat fmt[6];
	byte *data[6];
	qboolean malloced[6];
	int samesize, numloaded;
	char filename[MAX_OSPATH];

	if (skybox_cubemap_attempted || !skybox_name[0])
		return;

	skybox_cubemap_attempted = true;

	mark = Hunk_LowMark();
	for (i = 0, numloaded = 0, samesize = 0; i < 6; ++i)
	{
		q_snprintf(filename, sizeof(filename), "gfx/env/%s%s", skybox_name, suf[i]);
		data[i] = Image_LoadImage(filename, &width[i], &height[i], &fmt[i], &malloced[i]);
		if (data[i])
		{
			numloaded++;
			if (width[i] != height[i])
				samesize = -1;
			else if (samesize == 0)
				samesize = width[i];
			else if (samesize != width[i])
				samesize = -1;
		}
		else
		{
			fmt[i] = SRC_RGBA;
			malloced[i] = false;
		}
	}

	if (numloaded > 0 && samesize > 0)
	{
		if (!Sky_CreateCubemap(data, width, height, fmt, samesize))
			Sky_FreeCubemap();
	}

	for (i = 0; i < 6; ++i)
	{
		if (malloced[i])
			free(data[i]);
	}
	Hunk_FreeToLowMark(mark);
}

static qboolean Sky_CreateCubemap(byte *data[6], int width[6], int height[6], enum srcformat fmt[6], int samesize)
{
	static const int cubemap_order[6] = {3, 1, 4, 5, 0, 2}; // ft/bk/up/dn/rt/lf
	byte *zeroface = NULL;
	size_t facebytes;
	int i;

	if (!gl_glsl_able || samesize <= 0)
		return false;

	for (i = 0; i < 6; ++i)
	{
		if (data[i] && fmt[i] != SRC_RGBA)
			return false;
	}

	facebytes = (size_t)samesize * (size_t)samesize * 4u;

	if (!skybox_cubemap)
		glGenTextures(1, &skybox_cubemap);

	if (!skybox_cubemap)
		return false;

	GL_SelectTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, skybox_cubemap);
	GL_ClearBindings();

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	for (i = 0; i < 6; ++i)
	{
		int src = cubemap_order[i];
		byte *pixels = data[src];

		if (!pixels)
		{
			if (!zeroface)
			{
				zeroface = (byte *)calloc(1, facebytes);
				if (!zeroface)
					return false;
			}
			pixels = zeroface;
		}

		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, samesize, samesize, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	}

	if (zeroface)
		free(zeroface);

	skybox_cubemap_attempted = true;
	return true;
}

static qboolean Skywind_HasSkybox(void)
{
	int i;

	if (!skybox_name[0])
		return false;

	for (i = 0; i < 6; ++i)
	{
		if (skybox_textures[i] && skybox_textures[i] != notexture)
			return true;
	}

	return false;
}

static qboolean Skywind_Active(void)
{
	if (!Skywind_HasSkybox())
		return false;

	if (Skywind_GetRate() == 0.0f)
		return false;

	return (skywind_dist != 0.0f);
}

void Skywind_SetupFrame(void)
{
	Skywind_UpdateFrame();
}

static void Skywind_UpdateFrame(void)
{
	float dist, yaw, pitch, sy, cy, sp, cp;
	double phase;
	float period;
	float rate;

	if (skywind_frame_serial == r_framecount)
		return;

	skywind_frame_serial = r_framecount;
	skywind_frame_valid = false;

	if (!Skywind_Active())
		return;

	dist = bound(-2.0f, skywind_dist, 2.0f);
	if (dist == 0.0f)
		return;

	yaw = DEG2RAD(skywind_yaw);
	pitch = DEG2RAD(skywind_pitch);
	sy = sinf(yaw);
	cy = cosf(yaw);
	sp = sinf(pitch);
	cp = cosf(pitch);

	skywind_frame_dir[0] = dist * cp * sy;
	skywind_frame_dir[1] = dist * sp;
	skywind_frame_dir[2] = -dist * cp * cy;

	rate = Skywind_GetRate();
	period = skywind_period / rate;
	phase = (period != 0.0f) ? cl.time * 0.5 / period : 0.5;
	phase -= floor(phase) + 0.5;

	skywind_frame_phase = (float)phase;
	skywind_frame_valid = true;
}

static qboolean Skywind_GetDirectionAndPhase(vec3_t wind_dir, float *wind_phase)
{
	Skywind_UpdateFrame();

	if (!skywind_frame_valid)
		return false;

	if (wind_dir)
		VectorCopy(skywind_frame_dir, wind_dir);

	if (wind_phase)
		*wind_phase = skywind_frame_phase;

	return true;
}

static float Skywind_GetRate(void)
{
	return Skywind_ParseCvarDefaults(NULL, NULL, NULL, NULL) ? 1.0f : r_skywind.value;
}

static qboolean Skywind_ParseCvarDefaults(float *dist, float *yaw, float *period, float *pitch)
{
	float d, y, p, t;
	int count;

	if (!r_skywind.string || !r_skywind.string[0])
		return false;

	count = sscanf(r_skywind.string, "%f %f %f %f", &d, &y, &p, &t);
	if (count < 4)
		return false;

	if (dist)
		*dist = d;
	if (yaw)
		*yaw = y;
	if (period)
		*period = p;
	if (pitch)
		*pitch = t;

	return true;
}

/*
================
Skywind_WrapCoord

Mirrored-repeat wrapping for texture coordinates in range [-1, 1].
This ensures non-tiling skybox textures "bounce" at edges instead of
wrapping with a hard seam. The math:
  1. Map coord from [-1,1] to [0,1] via u = (coord + 1) * 0.5
  2. Compute mirrored position: m = u - 2*floor(u/2), giving [0,2)
  3. Triangle wave: w = 1 - |m - 1|, giving [0,1] that bounces at edges
  4. Map back to [-1,1] via return w * 2 - 1
================
*/
static float Skywind_WrapCoord(float coord)
{
	float u = (coord + 1.0f) * 0.5f;
	float m = u - 2.0f * floorf(u * 0.5f);
	float w = 1.0f - fabsf(m - 1.0f);
	return w * 2.0f - 1.0f;
}

static void Skywind_ProjectDirToST(const vec3_t dir, int axis, float *s, float *t)
{
	int j;
	float dv;

	j = vec_to_st[axis][2];
	if (j > 0)
		dv = dir[j - 1];
	else
		dv = -dir[-j - 1];

	if (dv == 0.0f)
	{
		if (s)
			*s = 0.0f;
		if (t)
			*t = 0.0f;
		return;
	}

	j = vec_to_st[axis][0];
	if (j < 0)
		*s = -dir[-j - 1] / dv;
	else
		*s = dir[j - 1] / dv;

	j = vec_to_st[axis][1];
	if (j < 0)
		*t = -dir[-j - 1] / dv;
	else
		*t = dir[j - 1] / dv;
}

static float Skywind_ComputeSecondaryOffset(float phase)
{
	float offset;

	offset = phase - floorf(phase);
	offset -= 0.5f;

	return offset;
}

static qboolean Skywind_EnsureShader(void)
{
	static const GLchar *skywind_vert_shader =
		"#version 110\n"
		"varying vec2 vBase;\n"
		"varying vec2 vPrimary;\n"
		"varying vec2 vSecondary;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		"	vBase = gl_MultiTexCoord0.st;\n"
		"	vPrimary = gl_MultiTexCoord1.st;\n"
		"	vSecondary = gl_MultiTexCoord2.st;\n"
		"}\n";
	static const GLchar *skywind_frag_shader =
		"#version 110\n"
		"uniform sampler2D uSkyTex;\n"
		"uniform float uWindPhase;\n"
		"uniform vec3 uFogColor;\n"
		"uniform float uFogDensity;\n"
		"varying vec2 vBase;\n"
		"varying vec2 vPrimary;\n"
		"varying vec2 vSecondary;\n"
		"void main()\n"
		"{\n"
		"	vec4 base = texture2D(uSkyTex, vBase);\n"
		"	vec4 layer1 = texture2D(uSkyTex, vPrimary);\n"
		"	vec4 layer2 = texture2D(uSkyTex, vSecondary);\n"
		"	float blend = clamp(abs(uWindPhase * 2.0), 0.0, 1.0);\n"
		"	float w1 = 1.0 - blend;\n"
		"	float w2 = blend;\n"
		"	layer1.a *= w1;\n"
		"	layer2.a *= w2;\n"
		"	layer1.rgb *= layer1.a;\n"
		"	layer2.rgb *= layer2.a;\n"
		"	vec4 combined = layer1 + layer2;\n"
		"	vec3 colour = base.rgb * (1.0 - combined.a) + combined.rgb;\n"
		"	colour = mix(colour, uFogColor, uFogDensity);\n"
		"	gl_FragColor = vec4(colour, 1.0);\n"
		"}\n";

	if (skywind_shader_initialized)
		return skywind_program != 0;

	skywind_shader_initialized = true;

	if (!gl_glsl_able || !gl_mtexable || gl_max_texture_units < 3 || !GL_CreateProgramFunc || !GL_UseProgramFunc || !GL_Uniform1fFunc || !GL_Uniform1iFunc || !GL_MTexCoord2fFunc)
		return false;

	skywind_program = GL_CreateProgram(skywind_vert_shader, skywind_frag_shader, 0, NULL);
	if (!skywind_program)
		return false;

	skywind_uniform_sampler = GL_GetUniformLocation(&skywind_program, "uSkyTex");
	skywind_uniform_phase = GL_GetUniformLocation(&skywind_program, "uWindPhase");
	skywind_uniform_fogcolor = GL_GetUniformLocation(&skywind_program, "uFogColor");
	skywind_uniform_fogdensity = GL_GetUniformLocation(&skywind_program, "uFogDensity");

	GL_UseProgramFunc(skywind_program);
	if (skywind_uniform_sampler != -1)
		GL_Uniform1iFunc(skywind_uniform_sampler, 0);
	GL_UseProgramFunc(0);

	return true;
}

static qboolean Skywind_EnsureCubemapShader(void)
{
	static const GLchar *skywind_cubemap_vert_shader =
		"#version 110\n"
		"varying vec3 vDir;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
		"	gl_Position.z = gl_Position.w;\n"
		"	vDir = gl_MultiTexCoord0.xyz;\n"
		"}\n";
	static const GLchar *skywind_cubemap_frag_shader =
		"#version 110\n"
		"uniform samplerCube uSkyCube;\n"
		"uniform vec3 uWindDir;\n"
		"uniform float uWindPhase;\n"
		"uniform vec3 uFogColor;\n"
		"uniform float uFogDensity;\n"
		"varying vec3 vDir;\n"
		"void main()\n"
		"{\n"
		"	vec3 dir = normalize(vDir);\n"
		"	float t1 = uWindPhase;\n"
		"	float t2 = fract(t1) - 0.5;\n"
		"	float blend = clamp(abs(t1 * 2.0), 0.0, 1.0);\n"
		"	vec4 base = textureCube(uSkyCube, dir);\n"
		"	vec4 layer1 = textureCube(uSkyCube, dir + t1 * uWindDir);\n"
		"	vec4 layer2 = textureCube(uSkyCube, dir + t2 * uWindDir);\n"
		"	layer1.a *= 1.0 - blend;\n"
		"	layer2.a *= blend;\n"
		"	layer1.rgb *= layer1.a;\n"
		"	layer2.rgb *= layer2.a;\n"
		"	vec4 combined = layer1 + layer2;\n"
		"	vec3 colour = base.rgb * (1.0 - combined.a) + combined.rgb;\n"
		"	colour = mix(colour, uFogColor, uFogDensity);\n"
		"	gl_FragColor = vec4(colour, 1.0);\n"
		"}\n";

	if (skywind_cubemap_shader_initialized)
		return skywind_cubemap_program != 0;

	skywind_cubemap_shader_initialized = true;

	if (!gl_glsl_able || !GL_CreateProgramFunc || !GL_UseProgramFunc || !GL_Uniform1fFunc || !GL_Uniform1iFunc || !GL_Uniform3fFunc)
		return false;

	skywind_cubemap_program = GL_CreateProgram(skywind_cubemap_vert_shader, skywind_cubemap_frag_shader, 0, NULL);
	if (!skywind_cubemap_program)
		return false;

	skywind_cubemap_uniform_sampler = GL_GetUniformLocation(&skywind_cubemap_program, "uSkyCube");
	skywind_cubemap_uniform_phase = GL_GetUniformLocation(&skywind_cubemap_program, "uWindPhase");
	skywind_cubemap_uniform_winddir = GL_GetUniformLocation(&skywind_cubemap_program, "uWindDir");
	skywind_cubemap_uniform_fogcolor = GL_GetUniformLocation(&skywind_cubemap_program, "uFogColor");
	skywind_cubemap_uniform_fogdensity = GL_GetUniformLocation(&skywind_cubemap_program, "uFogDensity");

	GL_UseProgramFunc(skywind_cubemap_program);
	if (skywind_cubemap_uniform_sampler != -1)
		GL_Uniform1iFunc(skywind_cubemap_uniform_sampler, 0);
	GL_UseProgramFunc(0);

	return true;
}

static qboolean Skywind_DrawSkyBox_Cubemap(const vec3_t wind_dir, float phase)
{
	int i;

	if (!skybox_cubemap)
	{
		Sky_TryRebuildCubemap();
	}
	if (!skybox_cubemap)
		return false;

	if (!Skywind_EnsureCubemapShader())
		return false;

	GL_SelectTexture(GL_TEXTURE0);
	glDisable(GL_BLEND);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	GL_UseProgramFunc(skywind_cubemap_program);
	if (skywind_cubemap_uniform_phase != -1)
		GL_Uniform1fFunc(skywind_cubemap_uniform_phase, phase);
	if (skywind_cubemap_uniform_winddir != -1)
		GL_Uniform3fFunc(skywind_cubemap_uniform_winddir, wind_dir[0], wind_dir[1], wind_dir[2]);

	{
		float fog_density = Fog_GetGlobalDensity();
		float *fog_color = Fog_GetGlobalColor();
		float density = 0.0f;
		static float fog_fallback[3] = {0.5f, 0.5f, 0.5f};

		if (!fog_color)
			fog_color = fog_fallback;

		if (fog_density > 0.0f && skyfog > 0.0f)
			density = CLAMP(0.0f, skyfog, 1.0f);

		if (skywind_cubemap_uniform_fogdensity != -1)
			GL_Uniform1fFunc(skywind_cubemap_uniform_fogdensity, density);
		if (skywind_cubemap_uniform_fogcolor != -1)
			GL_Uniform3fFunc(skywind_cubemap_uniform_fogcolor, fog_color[0], fog_color[1], fog_color[2]);
	}

	glBindTexture(GL_TEXTURE_CUBE_MAP, skybox_cubemap);
	GL_ClearBindings();

	skywind_apply_offsets = false;
	skywind_shader_enabled = false;
	skywind_cubemap_sampling = true;

	for (i = 0; i < 6; ++i)
	{
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
			continue;

		skymins[0][i] = -1;
		skymins[1][i] = -1;
		skymaxs[0][i] = 1;
		skymaxs[1][i] = 1;

		glBegin(GL_QUADS);
		Sky_EmitSkyBoxVertex(skymins[0][i], skymins[1][i], i);
		Sky_EmitSkyBoxVertex(skymins[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex(skymaxs[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex(skymaxs[0][i], skymins[1][i], i);
		glEnd();

		rs_skypolys++;
		rs_skypasses++;
	}

	skywind_cubemap_sampling = false;
	GL_UseProgramFunc(0);

	return true;
}

static qboolean Skywind_DrawSkyBox_Shader(const vec3_t wind_dir, float phase)
{
	int i;
	float secondary_offset;

	if (Skywind_DrawSkyBox_Cubemap(wind_dir, phase))
		return true;

	if (!Skywind_EnsureShader())
		return false;

	VectorCopy(wind_dir, skywind_frame_dir);

	GL_SelectTexture(GL_TEXTURE0);
	glDisable(GL_BLEND);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	GL_UseProgramFunc(skywind_program);
	if (skywind_uniform_phase != -1)
		GL_Uniform1fFunc(skywind_uniform_phase, phase);

	{
		float fog_density = Fog_GetGlobalDensity();
		float *fog_color = Fog_GetGlobalColor();
		float density = 0.0f;
		static float fog_fallback[3] = {0.5f, 0.5f, 0.5f};

		if (!fog_color)
			fog_color = fog_fallback;

		if (fog_density > 0.0f && skyfog > 0.0f)
			density = CLAMP(0.0f, skyfog, 1.0f);

		if (skywind_uniform_fogdensity != -1)
			GL_Uniform1fFunc(skywind_uniform_fogdensity, density);
		if (skywind_uniform_fogcolor != -1)
			GL_Uniform3fFunc(skywind_uniform_fogcolor, fog_color[0], fog_color[1], fog_color[2]);
	}

	skywind_apply_offsets = true;
	skywind_shader_enabled = true;

	secondary_offset = Skywind_ComputeSecondaryOffset(phase);
	skywind_primary_phase = phase;
	skywind_secondary_phase = secondary_offset;

	for (i = 0; i < 6; ++i)
	{
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
			continue;

		if (!skybox_textures[skytexorder[i]] || skybox_textures[skytexorder[i]] == notexture)
			continue;

		GL_Bind(skybox_textures[skytexorder[i]]);

		skymins[0][i] = -1;
		skymins[1][i] = -1;
		skymaxs[0][i] = 1;
		skymaxs[1][i] = 1;

		glBegin(GL_QUADS);
		Sky_EmitSkyBoxVertex(skymins[0][i], skymins[1][i], i);
		Sky_EmitSkyBoxVertex(skymins[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex(skymaxs[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex(skymaxs[0][i], skymins[1][i], i);
		glEnd();

		rs_skypolys++;
		rs_skypasses++;
	}

	skywind_shader_enabled = false;
	skywind_apply_offsets = false;

	GL_UseProgramFunc(0);

	return true;
}

static qboolean Skywind_ApplyCvarDefaultsInternal(qboolean quiet)
{
	float dist, yaw, period, pitch;

	if (!Skywind_ParseCvarDefaults(&dist, &yaw, &period, &pitch))
		return false;

	Skywind_Clear();
	skywind_dist = bound(-2.0f, dist, 2.0f);
	skywind_yaw = Skywind_Normalize360(yaw);
	skywind_period = period;
	skywind_pitch = Skywind_NormalizePitch(pitch);

	Skywind_InvalidateFrame();
	skywind_source = SKYWIND_SRC_DEFAULT;

	if (!quiet)
		Con_Printf("Loaded sky wind defaults from r_skywind\n");

	return true;
}

static qboolean Skywind_LoadConfigInternal(qboolean quiet)
{
	char relpath[MAX_QPATH];
	char *buffer;
	const char *data;
	qboolean loaded = false;
	qboolean is_default = false;
	qboolean has_include = false;
	qboolean include_match = false;
	qboolean exclude_match = false;

	if (!Skywind_HasSkybox())
	{
		if (!quiet)
			Con_Printf("No skybox loaded\n");
		return false;
	}

	q_snprintf(relpath, sizeof(relpath), "gfx/env/%s%s", skybox_name, SKYWIND_CFG);

	buffer = (char *)COM_LoadMallocFile(relpath, NULL);
	if (!buffer)
	{
		q_snprintf(relpath, sizeof(relpath), "%s", SKYWIND_DEFAULT_CFG);
		buffer = (char *)COM_LoadMallocFile(relpath, NULL);
		is_default = (buffer != NULL);
	}
	if (!buffer)
	{
		if (Skywind_ApplyCvarDefaultsInternal(quiet))
			return true;

		if (!quiet)
			Con_Printf("Couldn't load sky wind config \"%s\"\n", relpath);
		return false;
	}

	data = COM_Parse(buffer);
	if (!data)
	{
		if (!quiet)
			Con_Printf("Sky wind config \"%s\" is empty\n", relpath);
		if (Skywind_ApplyCvarDefaultsInternal(quiet))
			loaded = true;
		goto cleanup;
	}

	Skywind_Clear();

	if (q_strcasecmp(com_token, "skywind"))
	{
		if (!quiet)
			Con_Printf("Sky wind config \"%s\" is invalid\n", relpath);
		if (Skywind_ApplyCvarDefaultsInternal(quiet))
			loaded = true;
		goto cleanup;
	}

	if ((data = COM_Parse(data)) != NULL)
		skywind_dist = bound(-2.0f, (float)atof(com_token), 2.0f);

	if ((data = COM_Parse(data)) != NULL)
		skywind_yaw = Skywind_Normalize360((float)atof(com_token));

	if ((data = COM_Parse(data)) != NULL)
		skywind_period = (float)atof(com_token);

	if ((data = COM_Parse(data)) != NULL)
		skywind_pitch = Skywind_NormalizePitch((float)atof(com_token));

	while ((data = COM_Parse(data)) != NULL)
	{
		if (!q_strcasecmp(com_token, "include"))
		{
			if ((data = COM_Parse(data)) == NULL)
				break;
			if (is_default)
			{
				has_include = true;
				if (!q_strcasecmp(com_token, skybox_name))
					include_match = true;
			}
		}
		if (!q_strcasecmp(com_token, "exclude"))
		{
			if ((data = COM_Parse(data)) == NULL)
				break;
			if (is_default && !q_strcasecmp(com_token, skybox_name))
				exclude_match = true;
		}
	}

	if (is_default && (exclude_match || (has_include && !include_match)))
	{
		Skywind_Clear();
		Skywind_InvalidateFrame();
		if (!quiet)
		{
			if (exclude_match)
				Con_Printf("Sky wind default config excludes \"%s\"\n", skybox_name);
			else
				Con_Printf("Sky wind default config does not include \"%s\"\n", skybox_name);
		}
		goto cleanup;
	}

	Skywind_InvalidateFrame();
	skywind_source = SKYWIND_SRC_CONFIG;
	loaded = true;

	if (!quiet)
		Con_Printf("Loaded sky wind config \"%s\"\n", relpath);

cleanup:
	free (buffer);
	return loaded;
}

static qboolean Skywind_ApplyWorldspawn(const char *value)
{
	const char *data;
	float dist, yaw, period, pitch;

	if (!value || !value[0])
		return false;

	dist = skywind_dist;
	yaw = skywind_yaw;
	period = skywind_period;
	pitch = skywind_pitch;

	data = COM_Parse(value);
	if (!data)
		return false;

	if (!q_strcasecmp(com_token, "skywind"))
	{
		data = COM_Parse(data);
		if (!data)
			return false;
	}

	dist = (float)atof(com_token);

	if ((data = COM_Parse(data)) != NULL)
		yaw = (float)atof(com_token);
	if ((data = COM_Parse(data)) != NULL)
		period = (float)atof(com_token);
	if ((data = COM_Parse(data)) != NULL)
		pitch = (float)atof(com_token);

	skywind_dist = bound(-2.0f, dist, 2.0f);
	skywind_yaw = Skywind_Normalize360(yaw);
	skywind_period = period;
	skywind_pitch = Skywind_NormalizePitch(pitch);
	Skywind_InvalidateFrame();
	skywind_source = SKYWIND_SRC_WORLDSPAWN;

	return true;
}

static void Skywind_Cvar_OnChange(cvar_t *var)
{
	float dist, yaw, period, pitch;

	(void)var;

	if (!Skywind_ParseCvarDefaults(&dist, &yaw, &period, &pitch))
		return;

	if (skywind_source == SKYWIND_SRC_CONFIG || skywind_source == SKYWIND_SRC_WORLDSPAWN)
		return;

	skywind_dist = bound(-2.0f, dist, 2.0f);
	skywind_yaw = Skywind_Normalize360(yaw);
	skywind_period = period;
	skywind_pitch = Skywind_NormalizePitch(pitch);
	Skywind_InvalidateFrame();
	skywind_source = SKYWIND_SRC_DEFAULT;
}

static void Skywind_LoadConfig(void)
{
	Skywind_LoadConfigInternal(true);
}

static void Skywind_Load_f(void)
{
	Skywind_LoadConfigInternal(false);
}

static void Skywind_Save_f(void)
{
	char relpath[MAX_QPATH];
	char filepath[MAX_OSPATH];
	FILE *f;

	if (!Skywind_HasSkybox())
	{
		Con_Printf("No skybox loaded\n");
		return;
	}

	q_snprintf(relpath, sizeof(relpath), "gfx/env/%s%s", skybox_name, SKYWIND_CFG);
	q_snprintf(filepath, sizeof(filepath), "%s/%s", com_gamedir, relpath);
	COM_CreatePath(filepath);

	f = fopen(filepath, "wt");
	if (!f)
	{
		Con_Printf("Couldn't write sky wind config \"%s\"\n", relpath);
		return;
	}

	fprintf(f,
		"// distance yaw period pitch\n"
		"skywind %g %g %g %g\n",
		skywind_dist,
		skywind_yaw,
		skywind_period,
		skywind_pitch);

	fclose(f);

	Con_SafePrintf("Saved sky wind config \"");
	Con_LinkPrintf(filepath, "%s", relpath);
	Con_SafePrintf("\"\n");
}

static void Skywind_LookDir_f(void)
{
	if (cls.state != ca_connected)
		return;

	if (!Skywind_HasSkybox())
	{
		Con_Printf("No skybox loaded\n");
		return;
	}

	skywind_yaw = Skywind_Normalize360(cl.viewangles[YAW] + 180.0f);
	skywind_pitch = Skywind_NormalizePitch(-cl.viewangles[PITCH]);

	if (Cmd_Argc() >= 2)
		skywind_period = (float)atof(Cmd_Argv(1));
	else if (skywind_period <= 0.0f)
		skywind_period = 30.0f;

	if (Cmd_Argc() >= 3)
		skywind_dist = bound(-2.0f, (float)atof(Cmd_Argv(2)), 2.0f);
	else if (skywind_dist <= 0.0f)
		skywind_dist = 1.0f;

	Skywind_InvalidateFrame();
	skywind_source = SKYWIND_SRC_COMMAND;
}

static void Skywind_Rotate_f(void)
{
	if (cls.state != ca_connected)
		return;

	if (!Skywind_HasSkybox())
	{
		Con_Printf("No skybox loaded\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Con_Printf("usage: %s <yawdelta> [pitchdelta]\n", Cmd_Argv(0));
		return;
	}

	skywind_yaw = Skywind_Normalize360(skywind_yaw + (float)atof(Cmd_Argv(1)));

	if (Cmd_Argc() >= 3)
		skywind_pitch = Skywind_NormalizePitch(skywind_pitch + (float)atof(Cmd_Argv(2)));

	Skywind_InvalidateFrame();
	skywind_source = SKYWIND_SRC_COMMAND;
}

static void Skywind_f(void)
{
	if (!Skywind_HasSkybox())
	{
		Con_Printf("No skybox loaded\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		float rate = Skywind_GetRate();
		const char *defaults_line = "";
		char defaults_buf[256];

		if (Skywind_ParseCvarDefaults(NULL, NULL, NULL, NULL))
		{
			q_snprintf(defaults_buf, sizeof(defaults_buf), "   r_skywind defaults: %s\n", r_skywind.string);
			defaults_line = defaults_buf;
		}

		Con_Printf(
			"^bSkywind^b - Animated sky scrolling effect\n"
			"\n"
			"^musage:^m %s <dist> [yaw] [period] [pitch]\n"
			"\n"
			"^mparameters:^m\n"
			"   dist   [-2..2]   wind strength (0 = off, negative = reverse)\n"
			"   yaw    [0..360)  horizontal wind direction in degrees\n"
			"   period [seconds] time for one full scroll cycle\n"
			"   pitch  [-90..90] vertical wind angle\n"
			"\n"
			"^mrelated commands:^m\n"
			"   skywind_save     save config to gfx/env/<sky>wind.cfg\n"
			"   skywind_load     reload config from file\n"
			"   skywind_lookdir  set yaw/pitch from current view\n"
			"   skywind_rotate   adjust yaw/pitch by delta\n"
			"   default cfg      include/exclude <skyname> (exclude wins)\n"
			"\n"
			"^mcurrent values:^m\n"
			"   r_skywind (rate): %g\n"
			"%s"
			"   distance: %g\n"
			"   yaw:      %g\n"
			"   period:   %g\n"
			"   pitch:    %g\n",
			Cmd_Argv(0),
			rate,
			defaults_line,
			skywind_dist,
			skywind_yaw,
			skywind_period,
			skywind_pitch);
		return;
	}

	skywind_dist = bound(-2.0f, (float)atof(Cmd_Argv(1)), 2.0f);

	if (Cmd_Argc() >= 3)
		skywind_yaw = Skywind_Normalize360((float)atof(Cmd_Argv(2)));

	if (Cmd_Argc() >= 4)
		skywind_period = (float)atof(Cmd_Argv(3));

	if (Cmd_Argc() >= 5)
		skywind_pitch = Skywind_NormalizePitch((float)atof(Cmd_Argv(4)));

	Skywind_InvalidateFrame();
	skywind_source = SKYWIND_SRC_COMMAND;
}

void Sky_GetWindParams(float *dist, float *yaw, float *period, float *pitch)
{
	if (dist)   *dist   = skywind_dist;
	if (yaw)    *yaw    = skywind_yaw;
	if (period) *period = skywind_period;
	if (pitch)  *pitch  = skywind_pitch;
}

void Sky_SetWindParams(float dist, float yaw, float period, float pitch)
{
	char buf[128];

	skywind_dist   = bound(-2.0f, dist, 2.0f);
	skywind_yaw    = Skywind_Normalize360(yaw);
	skywind_period = period > 0.1f ? period : 0.1f;
	skywind_pitch  = Skywind_NormalizePitch(pitch);
	// Set source before writing cvar so the OnChange callback doesn't bail
	// when a map's worldspawn or a config previously claimed ownership.
	skywind_source = SKYWIND_SRC_COMMAND;
	Skywind_InvalidateFrame();

	q_snprintf(buf, sizeof(buf), "%g %g %g %g",
		skywind_dist, skywind_yaw, skywind_period, skywind_pitch);
	Cvar_Set("r_skywind", buf);
}

//==============================================================================
//
//  INIT
//
//==============================================================================

static float skyflatcolor_avg[3];
static qboolean skyflatcolor_avg_valid;

/*
=============
Sky_ApplyFlatColor

Sets skyflatcolor from r_fastskycolor if the cvar parses to a real colour,
otherwise from the cached pixel average computed at map-load.
==============
*/
static void Sky_ApplyFlatColor (void)
{
	plcolour_t sky_color = CL_PLColours_Parse (r_fastskycolor.string);

	if (sky_color.type != 0)
	{
		byte *rgb = CL_PLColours_ToRGB (&sky_color);
		if (rgb)
		{
			skyflatcolor[0] = (float)rgb[0] / 255.0f;
			skyflatcolor[1] = (float)rgb[1] / 255.0f;
			skyflatcolor[2] = (float)rgb[2] / 255.0f;
			return;
		}
		skyflatcolor[0] = skyflatcolor[1] = skyflatcolor[2] = 0.0f;
		return;
	}

	if (skyflatcolor_avg_valid)
	{
		skyflatcolor[0] = skyflatcolor_avg[0];
		skyflatcolor[1] = skyflatcolor_avg[1];
		skyflatcolor[2] = skyflatcolor_avg[2];
	}
	else
	{
		skyflatcolor[0] = skyflatcolor[1] = skyflatcolor[2] = 0.0f;
	}
}

/*
=============
Sky_SetFlatColorAverage / Sky_InvalidateFlatColorAverage

Cache (or clear) the map-load pixel average and reapply skyflatcolor so
r_fastskycolor still wins when it's set.
==============
*/
static void Sky_SetFlatColorAverage (float r, float g, float b)
{
	skyflatcolor_avg[0] = r;
	skyflatcolor_avg[1] = g;
	skyflatcolor_avg[2] = b;
	skyflatcolor_avg_valid = true;
	Sky_ApplyFlatColor ();
}

static void Sky_InvalidateFlatColorAverage (void)
{
	skyflatcolor_avg_valid = false;
	Sky_ApplyFlatColor ();
}

/*
=============
Sky_FastSkyColorChanged

Cvar callback so runtime changes to r_fastskycolor take effect without
a map reload.
==============
*/
static void Sky_FastSkyColorChanged (cvar_t *var)
{
	(void)var;
	Sky_ApplyFlatColor ();
}

/*
=============
Sky_AverageBspFront

Average opaque pixels of the left (front) half of a BSP sky miptex.
src is the full miptex pixel buffer; each row is [front | back] of
`width = srcwidth/2` blocks. Returns false if no opaque pixels or
unsupported format.
==============
*/
static qboolean Sky_AverageBspFront (const byte *src, int srcwidth, int height, enum srcformat fmt,
									 float *out_r, float *out_g, float *out_b)
{
	int width = srcwidth / 2;
	uint64_t r = 0, g = 0, b = 0, count = 0;
	int x, y;

	if (srcwidth <= 0 || height <= 0)
		return false;

	if (fmt == SRC_INDEXED)
	{
		for (y = 0; y < height; y++)
		{
			const byte *row = src + (size_t)y * (size_t)srcwidth;
			for (x = 0; x < width; x++)
			{
				byte p = row[x];
				if (p != 0)
				{
					unsigned *rgba = &d_8to24table[p];
					r += ((byte *)rgba)[0];
					g += ((byte *)rgba)[1];
					b += ((byte *)rgba)[2];
					count++;
				}
			}
		}
	}
	else if (fmt == SRC_RGBA)
	{
		for (y = 0; y < height; y++)
		{
			const byte *row = src + (size_t)y * (size_t)srcwidth * 4;
			for (x = 0; x < width; x++)
			{
				const byte *px = row + (size_t)x * 4;
				if (px[3] != 0)
				{
					r += px[0];
					g += px[1];
					b += px[2];
					count++;
				}
			}
		}
	}
	else
	{
		return false;
	}

	if (count == 0)
		return false;

	*out_r = (float)((double)r / ((double)count * 255.0));
	*out_g = (float)((double)g / ((double)count * 255.0));
	*out_b = (float)((double)b / ((double)count * 255.0));
	return true;
}

/*
=============
Sky_AverageQ64Front

Average pixels of a Quake64 BSP sky miptex front layer. Q64 skies are
stacked vertically: full-width front on top (halfheight), back below.
All pixels contribute (Q64 doesn't reserve index 0 as transparent for the
front layer), matching Sky_LoadTextureQ64's own averaging.
==============
*/
static qboolean Sky_AverageQ64Front (const byte *src, int width, int halfheight,
									  float *out_r, float *out_g, float *out_b)
{
	uint64_t r = 0, g = 0, b = 0, count = 0;
	size_t i, n;

	if (width <= 0 || halfheight <= 0)
		return false;
	n = (size_t)width * (size_t)halfheight;

	for (i = 0; i < n; i++)
	{
		unsigned *rgba = &d_8to24table[src[i]];
		r += ((byte *)rgba)[0];
		g += ((byte *)rgba)[1];
		b += ((byte *)rgba)[2];
		count++;
	}

	if (count == 0)
		return false;

	*out_r = (float)((double)r / ((double)count * 255.0));
	*out_g = (float)((double)g / ((double)count * 255.0));
	*out_b = (float)((double)b / ((double)count * 255.0));
	return true;
}

/*
=============
Sky_AverageFullImage

Average opaque pixels of an externally-loaded image (no front/back split).
Only SRC_RGBA and SRC_INDEXED are supported; other formats (e.g. DDS
compressed) return false so callers can fall back.
==============
*/
static qboolean Sky_AverageFullImage (const byte *data, int width, int height, enum srcformat fmt,
									  float *out_r, float *out_g, float *out_b)
{
	uint64_t r = 0, g = 0, b = 0, count = 0;
	size_t i, n;

	if (width <= 0 || height <= 0)
		return false;
	n = (size_t)width * (size_t)height;

	if (fmt == SRC_RGBA)
	{
		for (i = 0; i < n; i++)
		{
			const byte *px = data + i * 4;
			if (px[3] != 0)
			{
				r += px[0];
				g += px[1];
				b += px[2];
				count++;
			}
		}
	}
	else if (fmt == SRC_INDEXED)
	{
		for (i = 0; i < n; i++)
		{
			byte p = data[i];
			if (p != 0)
			{
				unsigned *rgba = &d_8to24table[p];
				r += ((byte *)rgba)[0];
				g += ((byte *)rgba)[1];
				b += ((byte *)rgba)[2];
				count++;
			}
		}
	}
	else
	{
		return false;
	}

	if (count == 0)
		return false;

	*out_r = (float)((double)r / ((double)count * 255.0));
	*out_g = (float)((double)g / ((double)count * 255.0));
	*out_b = (float)((double)b / ((double)count * 255.0));
	return true;
}

/*
=============
Sky_LoadTexture

A sky texture is 256*128, with the left side being a masked overlay
==============
*/
void Sky_LoadTexture (qmodel_t *mod, texture_t *mt, enum srcformat fmt, unsigned int srcwidth, unsigned int height)
{
	char		texturename[64];
	unsigned int i;
	byte		*src;
	byte	*front_data;
	byte	*back_data;
	unsigned int rows, columns;
	int bb,bw,bh;
	int width = srcwidth/2;

	TexMgr_BlockSize(fmt, &bb, &bw, &bh);
	columns = (width+bw-1) / bw;
	rows = (height+bh-1) / bh;

	front_data = Hunk_AllocName (bb*columns*rows*2, "skytex");
	back_data = front_data+bb*columns*rows;

	src = (byte *)(mt+1);

// extract back layer and upload
	for (i=0 ; i<rows ; i++)
		memcpy(back_data+bb*i*columns, src+bb*(i*columns*2 + columns), columns*bb);

	q_snprintf(texturename, sizeof(texturename), "%s:%s_back", mod->name, mt->name);
	mt->gltexture = solidskytexture = TexMgr_LoadImage (mod, texturename, width, height, fmt, back_data, "", (src_offset_t)back_data, TEXPREF_NONE);

// extract front layer and upload
	for (i=0 ; i<rows ; i++)
		memcpy(front_data+bb*i*columns, src+bb*(i*columns*2), columns*bb);
	if (fmt == SRC_INDEXED)
	{	//the lame texmgr only knows one transparent index...
		for (i=0 ; i<width*height ; i++)
		{
			if (front_data[i] == 0)
				front_data[i] = 255;
		}
	}
	q_snprintf(texturename, sizeof(texturename), "%s:%s_front", mod->name, mt->name);
	mt->fullbright = alphaskytexture = TexMgr_LoadImage (mod, texturename, width, height, fmt, front_data, "", (src_offset_t)front_data, TEXPREF_ALPHA);

	// average opaque front pixels using the raw miptex (the in-place 0->255
	// remap above destroyed transparency in front_data for SRC_INDEXED)
	{
		float ar, ag, ab;
		if (Sky_AverageBspFront (src, srcwidth, height, fmt, &ar, &ag, &ab))
			Sky_SetFlatColorAverage (ar, ag, ab);
		else
			Sky_InvalidateFlatColorAverage ();
	}
}

/*
=============
Sky_LoadTextureQ64

Quake64 sky textures are 32*64
==============
*/
void Sky_LoadTextureQ64 (qmodel_t *mod, texture_t *mt)
{
	char		texturename[64];
	unsigned	i, p, r, g, b, count, halfheight, *rgba;
	byte		*front, *back, *front_rgba;

	if (mt->width != 32 || mt->height != 64)
	{
		Con_DWarning ("Q64 sky texture %s is %d x %d, expected 32 x 64\n", mt->name, mt->width, mt->height);
		if (mt->width < 1 || mt->height < 2)
			return;
	}

	// pointers to both layer textures
	halfheight = mt->height / 2;
	front = (byte *)(mt+1);
	back = (byte *)(mt+1) + mt->width*halfheight;
	front_rgba = (byte *) Hunk_AllocName (4*mt->width*halfheight, "q64_skytex");

	// Normal indexed texture for the back layer
	q_snprintf(texturename, sizeof(texturename), "%s:%s_back", mod->name, mt->name);
	mt->gltexture = solidskytexture = TexMgr_LoadImage (mod, texturename, mt->width, halfheight, SRC_INDEXED, back, "", (src_offset_t)back, TEXPREF_NONE);

	// front layer, convert to RGBA and upload; also average for fast-sky colour
	p = r = g = b = count = 0;

	for (i = mt->width * halfheight; i != 0; i--)
	{
		rgba = &d_8to24table[*front++];

		// RGB
		front_rgba[p++] = ((byte*)rgba)[0];
		front_rgba[p++] = ((byte*)rgba)[1];
		front_rgba[p++] = ((byte*)rgba)[2];
		// Alpha
		front_rgba[p++] = 128; // this look ok to me!

		// Fast sky
		r += ((byte *)rgba)[0];
		g += ((byte *)rgba)[1];
		b += ((byte *)rgba)[2];
		count++;
	}

	q_snprintf(texturename, sizeof(texturename), "%s:%s_front", mod->name, mt->name);
	mt->fullbright = alphaskytexture = TexMgr_LoadImage (mod, texturename, mt->width, halfheight, SRC_RGBA, front_rgba, "", (src_offset_t)front_rgba, TEXPREF_ALPHA);

	if (count > 0)
		Sky_SetFlatColorAverage ((float)r/(count*255), (float)g/(count*255), (float)b/(count*255));
	else
		Sky_InvalidateFlatColorAverage ();
}

/*
=============
Sky_LoadExternalTextures -- woods #extsky
Load external sky textures
==============
*/
qboolean Sky_LoadExternalTextures (qmodel_t* mod, texture_t* mt)
{
	if (r_fastsky.value == 1)
		return false;
	
	char texturename_back[MAX_OSPATH], texturename_front[MAX_OSPATH];
	char mapname[MAX_OSPATH];
	byte* back_data = NULL, * front_data = NULL;
	int fwidth_back = 0, fheight_back = 0, fwidth_front = 0, fheight_front = 0;
	enum srcformat rfmt_back = SRC_EXTERNAL, rfmt_front = SRC_EXTERNAL;
	qboolean malloced_back = false, malloced_front = false;

	int mark = Hunk_LowMark();

	COM_StripExtension(mod->name + 5, mapname, sizeof(mapname));

	q_snprintf(texturename_back, sizeof(texturename_back), "textures/%s/%s_back", mapname, mt->name);
	back_data = Image_LoadImage(texturename_back, &fwidth_back, &fheight_back, &rfmt_back, &malloced_back);
	if (!back_data) {
		q_snprintf(texturename_back, sizeof(texturename_back), "textures/%s_back", mt->name);
		back_data = Image_LoadImage(texturename_back, &fwidth_back, &fheight_back, &rfmt_back, &malloced_back);
	}

	q_snprintf(texturename_front, sizeof(texturename_front), "textures/%s/%s_front", mapname, mt->name);
	front_data = Image_LoadImage(texturename_front, &fwidth_front, &fheight_front, &rfmt_front, &malloced_front);
	if (!front_data) {
		q_snprintf(texturename_front, sizeof(texturename_front), "textures/%s_front", mt->name);
		front_data = Image_LoadImage(texturename_front, &fwidth_front, &fheight_front, &rfmt_front, &malloced_front);
	}

	if (back_data && front_data) // If both textures loaded successfully
	{
		mt->gltexture = solidskytexture = TexMgr_LoadImage(mod, texturename_back, fwidth_back, fheight_back, rfmt_back, back_data, texturename_back, 0, TEXPREF_NONE);
		mt->fullbright = alphaskytexture = TexMgr_LoadImage(mod, texturename_front, fwidth_front, fheight_front, rfmt_front, front_data, texturename_front, 0, TEXPREF_ALPHA);

		// fast-sky colour: average the external front if its pixels are addressable,
		// otherwise (compressed DDS etc.) fall back to the BSP miptex using the
		// right layout for this map's BSP version
		{
			float ar, ag, ab;
			qboolean got = Sky_AverageFullImage (front_data, fwidth_front, fheight_front, rfmt_front, &ar, &ag, &ab);
			if (!got)
			{
				if (mod->bspversion == BSPVERSION_QUAKE64)
					got = Sky_AverageQ64Front ((const byte *)(mt + 1), mt->width, mt->height / 2, &ar, &ag, &ab);
				else
					got = Sky_AverageBspFront ((const byte *)(mt + 1), mt->width, mt->height, SRC_INDEXED, &ar, &ag, &ab);
			}
			if (got)
				Sky_SetFlatColorAverage (ar, ag, ab);
			else
				Sky_InvalidateFlatColorAverage ();
		}

		if (malloced_back) free(back_data);
		if (malloced_front) free(front_data);
		Hunk_FreeToLowMark(mark);
		externalskyloaded = true; // #fastsky2
		return true; // success: both textures loaded
	}

	if (malloced_back) free(back_data);
	if (malloced_front) free(front_data);
	Hunk_FreeToLowMark(mark);

	externalskyloaded = false; // #fastsky2
	return false;
}

/*
==================
Sky_LoadSkyBox
==================
*/
static const char *suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
static const char *skybox_image_extensions[] = {"dds", "tga", "png", "jpeg", "jpg", "pcx"};

static qboolean Sky_FileExistsWithExtension(const char *name, const char *extension)
{
	char filename[MAX_OSPATH];
	const char *skipname;

	if (!name || !name[0])
		return false;

	q_snprintf(filename, sizeof(filename), "%s.%s", name, extension);
	if (COM_FileExists(filename, NULL))
		return true;

	q_snprintf(filename, sizeof(filename), "textures/%s.%s", name, extension);
	if (COM_FileExists(filename, NULL))
		return true;

	skipname = COM_SkipPath(name);
	if (skipname != name)
	{
		q_snprintf(filename, sizeof(filename), "textures/%s.%s", skipname, extension);
		if (COM_FileExists(filename, NULL))
			return true;
	}

	return false;
}

static qboolean Sky_HasSkyboxFace(const char *name, const char *suffix)
{
	char filename[MAX_OSPATH];

	if (!name || !name[0])
		return false;

	q_snprintf(filename, sizeof(filename), "gfx/env/%s%s", name, suffix);

	for (int i = 0; i < (int)ARRAY_COUNT(skybox_image_extensions); i++)
	{
		if (Sky_FileExistsWithExtension(filename, skybox_image_extensions[i]))
			return true;
	}

	return false;
}

static qboolean Sky_HasAllSkyboxFaces(const char *name)
{
	if (!name || !name[0])
		return false;

	for (int i = 0; i < 6; i++)
	{
		if (!Sky_HasSkyboxFace(name, suf[i]))
			return false;
	}

	return true;
}

void Sky_WarnMissingSkybox(const char *name)
{
	Con_Printf("this map uses an external sky, couldn't load skybox ^m%s^m\n",
		name ? name : "");
}

static void Sky_LoadSkyBoxInternal (const char *name, qboolean quiet) // woods #skydownloads
{
	int		i, mark;
	int		width[6], height[6];
	enum srcformat fmt[6];
	byte	*data[6];
	qboolean malloced[6];
	int		samesize, numloaded;
	char	filename[MAX_OSPATH];
	qboolean nonefound = true;

	if (strcmp(skybox_name, name) == 0 && skybox_complete)
	{	//no change. the textures survive map changes (NULL texmgr owner), so a
		//matching fully-loaded skybox skips the expensive 6-face reload here.
		//partial skyboxes fall through so newly downloaded faces get picked up.
		Skywind_LoadConfig();
		return;
	}
	Skywind_Clear();
	Sky_FreeCubemap();
	skybox_cubemap_attempted = false;
	skybox_complete = false;

	//purge old textures
	for (i=0; i<6; i++)
	{
		if (skybox_textures[i] && skybox_textures[i] != notexture)
			TexMgr_FreeTexture (skybox_textures[i]);
		skybox_textures[i] = NULL;
	}

	//turn off skybox if sky is set to ""
	if (name[0] == 0)
	{
		skybox_name[0] = 0;
		return;
	}

	//load textures
	mark = Hunk_LowMark ();
	for (i = 0, numloaded = 0, samesize = 0; i < 6; i++)
	{
		q_snprintf (filename, sizeof(filename), "gfx/env/%s%s", name, suf[i]);
		data[i] = Image_LoadImage (filename, &width[i], &height[i], &fmt[i], &malloced[i]);
		if (data[i])
		{
			numloaded++;
			if (width[i] != height[i])
				samesize = -1;
			else if (samesize == 0)
				samesize = width[i];
			else if (samesize != width[i])
				samesize = -1;
		}
		else
		{
			fmt[i] = SRC_RGBA;
			malloced[i] = false;
		}
	}

	if (numloaded > 0 && samesize > 0)
	{
		if (!Sky_CreateCubemap(data, width, height, fmt, samesize))
			Sky_FreeCubemap();
	}
	skybox_cubemap_attempted = true;

	for (i = 0; i < 6; i++)
	{
		if (data[i])
		{
			q_snprintf (filename, sizeof(filename), "gfx/env/%s%s", name, suf[i]);
			//NULL owner so the faces survive map unloads and can be reused when
			//the next map wants the same skybox (freed explicitly on change)
			skybox_textures[i] = TexMgr_LoadImage (NULL, filename, width[i], height[i], fmt[i], data[i], filename, 0, TEXPREF_NONE);
			nonefound = false;
		}
		else
		{
			//Con_Printf ("Couldn't load skybox %s\n", filename); // woods
			skybox_textures[i] = notexture;
		}
		if (malloced[i])
			free(data[i]);
	}
	Hunk_FreeToLowMark (mark);

	if (numloaded < 6 && !quiet) // woods, verbose missing sky + limit spam
	{
		Sky_WarnMissingSkybox(name);
	}

	if (nonefound) // go back to scrolling sky if skybox is totally missing
	{
		for (i=0; i<6; i++)
		{
			if (skybox_textures[i] && skybox_textures[i] != notexture)
				TexMgr_FreeTexture (skybox_textures[i]);
			skybox_textures[i] = NULL;
		}
		Sky_FreeCubemap();
		skybox_name[0] = 0;
		return;
	}

	q_strlcpy(skybox_name, name, sizeof(skybox_name));
	skybox_complete = (numloaded == 6);
	Skywind_LoadConfig();
}

void Sky_LoadSkyBox (const char *name) // woods #skydownloads
{
	Sky_LoadSkyBoxInternal(name, false);
}

qboolean Sky_DownloadsDisabled(void) // woods #skydownloads
{
	return (allow_download_sky.value == 0);
}

#define SKYBOX_DOWNLOAD_CACHE_FILENAME				"skybox_download_cache.json"
#define SKYBOX_DOWNLOAD_CACHE_SCHEMA_VERSION		1
#define SKYBOX_DOWNLOAD_CACHE_MAX_FILE_SIZE			(1024 * 1024)
#define SKYBOX_DOWNLOAD_CACHE_MAX_MIRROR			1024
#define SKYBOX_DOWNLOAD_CACHE_MISS_RETRY_SECONDS	(30 * 24 * 60 * 60)
#define SKYBOX_DOWNLOAD_CACHE_TRANSIENT_SECONDS		(30 * 60)
#define SKYBOX_DOWNLOAD_REMOTE_EXTENSIONS			"tga,png,jpg,dds"

static com_negative_cache_t skybox_download_cache;

static qboolean SkyboxDownloadCache_Validate(const char *mirror, const char *face, void *ctx)
{
	(void)ctx;
	return mirror && mirror[0] && face && face[0] && !strncmp(face, "gfx/env/", 8);
}

static const com_negative_cache_config_t skybox_download_cache_config =
{
	SKYBOX_DOWNLOAD_CACHE_FILENAME,
	SKYBOX_DOWNLOAD_CACHE_SCHEMA_VERSION,
	SKYBOX_DOWNLOAD_CACHE_MAX_FILE_SIZE,
	"mirror",
	"face",
	SKYBOX_DOWNLOAD_CACHE_MAX_MIRROR,
	MAX_QPATH,
	"remote_extensions",
	SKYBOX_DOWNLOAD_REMOTE_EXTENSIONS,
	SkyboxDownloadCache_Validate,
	NULL
};

static qboolean SkyboxDownloadCache_NormalizeMirror(const char *input, char *output, size_t output_size)
{
	int slash_count = 0;
	const char *p;

	if (!input || !output || output_size == 0 || !IsGithubRepoPath(input))
		return false;

	for (p = input; *p; p++)
	{
		if (*p == '/')
			slash_count++;
	}

	if (slash_count == 1)
		return (size_t)q_snprintf(output, output_size, "%s/main", input) < output_size;

	return q_strlcpy(output, input, output_size) < output_size;
}

static qboolean SkyboxDownloadCache_ShouldSkip(const char *mirror, const char *face, time_t now)
{
	time_t next_retry;

	if (now == (time_t)-1)
		return false;

	if (!COM_NegativeCache_ShouldSkip(&skybox_download_cache,
		&skybox_download_cache_config, mirror, face, now, &next_retry))
	{
		return false;
	}

	Con_DPrintf("skybox download cache: skipping %s from %s until %lld\n",
		face, mirror, (long long)next_retry);
	return true;
}

static void SkyboxDownloadCache_Update(const char *mirror, const char *face,
	com_negative_cache_result_t result, time_t now)
{
	COM_NegativeCache_Update(&skybox_download_cache,
		&skybox_download_cache_config,
		mirror, face, result, now,
		SKYBOX_DOWNLOAD_CACHE_MISS_RETRY_SECONDS,
		SKYBOX_DOWNLOAD_CACHE_TRANSIENT_SECONDS);
}

static void SkyboxDownloadCache_Remove(const char *mirror, const char *face)
{
	COM_NegativeCache_Remove(&skybox_download_cache,
		&skybox_download_cache_config, mirror, face);
}

static void SkyboxDownloadCache_SaveIfDirty(void)
{
	COM_NegativeCache_SaveIfDirty(&skybox_download_cache,
		&skybox_download_cache_config);
}

/*
==============================================================================
Sky_DownloadSkybox - woods #skydownloads
Only downloads from mirrors that are in user/repo/branch form
Leaves tag formatting to Curl_DownloadFile (display_name = NULL)
==============================================================================
*/
qboolean Sky_DownloadSkybox(const char* name)
{
	if (Sky_DownloadsDisabled())
		return false;

	const char* bases[2] = {
		cl_web_download_url.string,
		cl_web_download_url2.string
	};

	static const char* extensions[] = { "tga", "png", "jpg", "dds" };

	time_t now = time(NULL);
	char face_path[MAX_QPATH];
	char remote_path[MAX_QPATH];
	char local_path[MAX_OSPATH];

	if (Sky_HasAllSkyboxFaces(name))
		return true;

	for (int i = 0; i < 6; ++i)
	{
		int face_path_len;

		if (Sky_HasSkyboxFace(name, suf[i]))
			continue;

		face_path_len = q_snprintf(face_path, sizeof(face_path),
			"gfx/env/%s%s", name, suf[i]);
		if (face_path_len < 0 || (size_t)face_path_len >= sizeof(face_path))
		{
			Con_DPrintf("Skybox face path too long for %s%s\n", name, suf[i]);
			continue;
		}

		for (int b = 0; b < 2; ++b)
		{
			char normalized_base[SKYBOX_DOWNLOAD_CACHE_MAX_MIRROR];
			int not_found_count = 0;
			int attempt_count = 0;
			qboolean downloaded = false;
			qboolean cacheable = true;

			if (!SkyboxDownloadCache_NormalizeMirror(bases[b],
				normalized_base, sizeof(normalized_base)))      /* skip plain hosts       */
				continue;

			if (SkyboxDownloadCache_ShouldSkip(normalized_base, face_path, now))
				continue;

			for (int e = 0; e < (int)ARRAY_COUNT(extensions); ++e)
			{
				web_download_result_t download_result = WEB_DOWNLOAD_RESULT_NONE;
				int remote_path_len;
				int local_path_len;

				remote_path_len = q_snprintf(remote_path, sizeof(remote_path),
					"%s.%s", face_path, extensions[e]);
				if (remote_path_len < 0 || (size_t)remote_path_len >= sizeof(remote_path))
				{
					attempt_count++;
					not_found_count++;
					continue;
				}

				local_path_len = q_snprintf(local_path, sizeof(local_path),
					"%s/%s", com_gamedir, remote_path);
				if (local_path_len < 0 || (size_t)local_path_len >= sizeof(local_path))
				{
					attempt_count++;
					not_found_count++;
					continue;
				}

				if (Curl_DownloadFile(bases[b],
					remote_path,
					local_path,
					/* is_skybox   */ true,
					/* display_tag */ NULL,
					&download_result))
				{
					downloaded = true;
					SkyboxDownloadCache_Remove(normalized_base, face_path);
					break;                         /* face is present       */
				}

				if (download_result == WEB_DOWNLOAD_RESULT_NONE)
				{
					cacheable = false;
					break;
				}

				attempt_count++;
				if (download_result == WEB_DOWNLOAD_RESULT_NOT_FOUND ||
					download_result == WEB_DOWNLOAD_RESULT_INVALID)
				{
					not_found_count++;
				}
			}

			if (downloaded || Sky_HasSkyboxFace(name, suf[i]))
				break;

			if (cacheable && attempt_count > 0)
			{
				if (not_found_count == attempt_count &&
					attempt_count == (int)ARRAY_COUNT(extensions))
				{
					SkyboxDownloadCache_Update(normalized_base, face_path,
						COM_NEGATIVE_CACHE_MISSING, now);
				}
				else
				{
					SkyboxDownloadCache_Update(normalized_base, face_path,
						COM_NEGATIVE_CACHE_TRANSIENT, now);
				}
			}
		}
	}

	SkyboxDownloadCache_SaveIfDirty();
	return Sky_HasAllSkyboxFaces(name);
}

static void Sky_LoadSkyBoxAuto(const char *name)
{
	qboolean allow_downloads;

	if (!name)
		name = "";

	allow_downloads = !Sky_DownloadsDisabled();
	pending_skybox_name[0] = 0;
	skybox_download_pending = false;

	Sky_LoadSkyBoxInternal(name, allow_downloads); // quiet if downloads enabled, verbose if disabled

	if (name[0] && !skybox_name[0] && allow_downloads)
	{
		// Store the name for delayed download when connection is complete
		q_strlcpy(pending_skybox_name, name, sizeof(pending_skybox_name));
		skybox_download_pending = true;
	}
}

static void Sky_SetMapSkybox(const char *name)
{
	if (!name)
		name = "";

	q_strlcpy(map_skybox_name, name, sizeof(map_skybox_name));
}

static void Sky_ApplyGlobalSkybox(void)
{
	const char *preferred = r_globalsky.string[0] ? r_globalsky.string : map_skybox_name;

	if (!cl.worldmodel)
		return;

	Sky_LoadSkyBoxAuto(preferred);

	if (r_globalsky.string[0] && !skybox_name[0] && map_skybox_name[0])
		Sky_LoadSkyBoxAuto(map_skybox_name);
}

static void Sky_GlobalSkyboxChanged(cvar_t *var)
{
	(void)var;

	Sky_ApplyGlobalSkybox();
}

/*
=================
Sky_ClearAll

Called on map unload/game change to avoid keeping pointers to freed data
=================
*/
static void Sky_ClearInternal (qboolean purge_skybox)
{
	int i;

	skyroom_enabled = false;
	externalskyloaded = false; // woods #fastsky2
	skyflatcolor_avg[0] = skyflatcolor_avg[1] = skyflatcolor_avg[2] = 0.0f;
	Sky_InvalidateFlatColorAverage (); // also resets skyflatcolor[] via Sky_ApplyFlatColor
	map_skybox_name[0] = 0;
	pending_skybox_name[0] = 0;
	skybox_download_pending = false;
	Sky_ClearSkyboxOrientation();
	solidskytexture = NULL;
	alphaskytexture = NULL;
	Skywind_Clear();

	if (purge_skybox)
	{
		skybox_name[0] = 0;
		skybox_complete = false;
		for (i=0; i<6; i++)
		{
			if (skybox_textures[i] && skybox_textures[i] != notexture)
				TexMgr_FreeTexture (skybox_textures[i]);
			skybox_textures[i] = NULL;
		}
		Sky_FreeCubemap();
		skybox_cubemap_attempted = false;
	}

	Cvar_SetQuick (&r_skyfog, r_skyfog.default_string);
}

void Sky_ClearAll (void)
{
	Sky_ClearInternal (true);
}

/*
=================
Sky_NewMapClear

Called on map unload. Clears per-map sky state but keeps the loaded skybox
faces/cubemap (they have no texmgr owner) so a map change that wants the same
skybox skips the 6-face reload in Sky_LoadSkyBoxInternal.
=================
*/
void Sky_NewMapClear (void)
{
	Sky_ClearInternal (false);
}

void Sky_ResetGL (void)
{
	skywind_shader_initialized = false;
	skywind_program = 0;
	skywind_uniform_sampler = -1;
	skywind_uniform_phase = -1;
	skywind_uniform_fogcolor = -1;
	skywind_uniform_fogdensity = -1;

	skywind_cubemap_shader_initialized = false;
	skywind_cubemap_program = 0;
	skywind_cubemap_uniform_sampler = -1;
	skywind_cubemap_uniform_phase = -1;
	skywind_cubemap_uniform_winddir = -1;
	skywind_cubemap_uniform_fogcolor = -1;
	skywind_cubemap_uniform_fogdensity = -1;

	skywind_shader_enabled = false;
	skywind_apply_offsets = false;

	Sky_FreeCubemap();
	skybox_cubemap_attempted = false;
}

/*
=================
Sky_NewMap
=================
*/
void Sky_NewMap (void)
{
	char	key[128], value[4096];
	char	skywind_value[4096];
	char	skybox_choices[MAX_MAP_SKYBOX_CHOICES][MAX_QPATH];
	char	legacy_skybox_name[MAX_QPATH];
	const char	*data;
	int		skybox_choice_count = 0;
	qboolean	have_legacy_skybox = false;
	qboolean	skywind_from_worldspawn = false;

	skyfog = r_skyfog.value;
	map_skybox_name[0] = 0;
	skywind_value[0] = 0;
	legacy_skybox_name[0] = 0;
	memset(skybox_choices, 0, sizeof(skybox_choices));
	Sky_ClearSkyboxOrientation();

	//
	// read worldspawn (this is so ugly, and shouldn't it be done on the server?)
	//
	data = cl.worldmodel->entities;
	if (!data)
		return; //FIXME: how could this possibly ever happen? -- if there's no
	// worldspawn then the sever wouldn't send the loadmap message to the client

	data = COM_Parse(data);
	if (!data) //should never happen
		return; // error
	if (com_token[0] != '{') //should never happen
		return; // error
	while (1)
	{
		data = COM_Parse(data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			q_strlcpy(key, com_token + 1, sizeof(key));
		else
			q_strlcpy(key, com_token, sizeof(key));
		while (key[0] && key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;
		data = COM_ParseEx(data, CPE_ALLOWTRUNC);
		if (!data)
			return; // error
		q_strlcpy(value, com_token, sizeof(value));

		if (Sky_IsSkyboxWorldspawnKey(key))
		{
			// Numbered keys (sky1/skybox2/...) and multi-value lists opt into
			// random selection. A bare alias with a single value keeps legacy
			// last-wins precedence instead of joining the random pool.
			if (Sky_IsNumberedSkyboxWorldspawnKey(key) || Sky_CountSkyboxTokens(value) > 1)
				skybox_choice_count = Sky_AddMapSkyboxChoices(skybox_choices, skybox_choice_count, MAX_MAP_SKYBOX_CHOICES, value);
			else
			{
				q_strlcpy(legacy_skybox_name, value, sizeof(legacy_skybox_name));
				have_legacy_skybox = true;
			}
		}
		else if (!strcmp("skyrotate", key))
			skybox_orientation[3] = (float)atof(value);
		else if (!strcmp("skyaxis", key))
			Sky_ParseSkyboxAxis(value);
		else if (!strcmp("skyroom", key))
		{	//"_skyroom" "X Y Z". ideally the gamecode would do this with an entity, but people want to use the vanilla gamecode from 1996 for some reason.
			const char *t = COM_Parse(value);
			skyroom_origin[0] = atof(com_token);
			t = COM_Parse(t);
			skyroom_origin[1] = atof(com_token);
			t = COM_Parse(t);
			skyroom_origin[2] = atof(com_token);
			t = COM_Parse(t);
			skyroom_origin[3] = atof(com_token);
			skyroom_enabled = true;

			t = COM_Parse(t);
			skyroom_orientation[3] = atof(com_token);
			t = COM_Parse(t);
			skyroom_orientation[0] = atof(com_token);
			t = COM_Parse(t);
			skyroom_orientation[1] = atof(com_token);
			t = COM_Parse(t);
			skyroom_orientation[2] = atof(com_token);
		}

		else if (!strcmp("skyfog", key))
			skyfog = atof(value);
		else if (!strcmp("skywind", key))
		{
			q_strlcpy(skywind_value, value, sizeof(skywind_value));
			skywind_from_worldspawn = true;
		}
	}

	// Opt-in random pool takes precedence; otherwise honour the legacy single
	// alias (last recognized key wins), matching pre-randomization behaviour.
	if (skybox_choice_count > 0)
		Sky_SetRandomMapSkybox(skybox_choices, skybox_choice_count);
	else if (have_legacy_skybox)
		Sky_SetMapSkybox(legacy_skybox_name);

	Sky_ApplyGlobalSkybox();

	if (skywind_from_worldspawn)
		Skywind_ApplyWorldspawn(skywind_value);
}

/*
=================
Sky_SkyCommand_f
=================
*/
void Sky_SkyCommand_f (void)
{
	switch (Cmd_Argc())
	{
	case 1:
		Con_Printf("\"sky\" is \"%s\"\n", skybox_name);
		break;
	case 2:
		Sky_LoadSkyBox(Cmd_Argv(1));
		break;
	default:
		Con_Printf("usage: sky <skyname>\n");
	}
}

static void Sky_SkyRoomCommand_f (void)
{
	switch (Cmd_Argc())
	{
	case 1:
		if (skyroom_enabled)
			Con_Printf("\"skyroom\" is \"%f %f %f %f %f %f %f %f\"\n", skyroom_origin[0],skyroom_origin[1],skyroom_origin[2],skyroom_origin[3], skyroom_orientation[3],skyroom_orientation[0],skyroom_orientation[1],skyroom_orientation[2]);
		else
			Con_Printf("\"skyroom\" is \"\"\n");
		break;
	case 4:	//xyz
	case 5:	//xyz paralax
	case 6:	//+speed
	case 7:	//+axis_x
	case 8:	//+axis_y
	case 9:	//+axis_z
		skyroom_enabled = true;
		skyroom_origin[0] = atof(Cmd_Argv(1));
		skyroom_origin[1] = atof(Cmd_Argv(2));
		skyroom_origin[2] = atof(Cmd_Argv(3));
		skyroom_origin[3] = atof(Cmd_Argv(4));	//paralax

		skyroom_orientation[3] = atof(Cmd_Argv(5));	//speed
		skyroom_orientation[0] = atof(Cmd_Argv(6));
		skyroom_orientation[1] = atof(Cmd_Argv(7));
		skyroom_orientation[2] = atof(Cmd_Argv(8));
		break;
	case 2:	//x
		if (!*Cmd_Argv(1) || !q_strcasecmp(Cmd_Argv(1), "off"))
		{
			skyroom_enabled = false;
			break;
		}
		//fallthrough
	case 3:	//xy
	default:
		Con_Printf("usage: skyroom origin_x origin_y origin_z paralax_scale speed axis_x axis_y axis_z\n");
	}
}

/*
====================
R_SetSkyfog_f -- ericw
====================
*/
static void R_SetSkyfog_f (cvar_t *var)
{
// clear any skyfog setting from worldspawn
	skyfog = var->value;
}

/*
===============
SKy_Color_Completion_f -- woods #iwtabcomplete #fastskycolor
===============
*/
static void SKy_Color_Completion_f(cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("0x465f6b", partial, "blue", NULL); // #demolistsort add arg
	Con_AddToTabList("0x211529", partial, "purple", NULL); // #demolistsort add arg
	Con_AddToTabList("0x000000", partial, "black", NULL); // #demolistsort add arg
	Con_AddToTabList("0x1e1e1e", partial, "dark grey", NULL); // #demolistsort add arg
	Con_AddToTabList("\"\"", partial, NULL, NULL); // #demolistsort add arg for literal ""

	for (int i = 1; i <= 13; i++)
	{
		char num_str[3];
		snprintf(num_str, sizeof(num_str), "%d", i);
		Con_AddToTabList(num_str, partial, NULL, NULL); // #demolistsort add arg
	}

	return;
}

/*
===============
Sky_FastSky_Completion_f
===============
*/
static void Sky_FastSky_Completion_f(cvar_t* cvar, const char* partial)
{
	(void)cvar;

	Con_AddToTabList("0", partial, "normal textured sky or skybox", NULL);
	Con_AddToTabList("1", partial, "flat-color fast sky", NULL);
	Con_AddToTabList("2", partial, "prefer skybox; otherwise flat color", NULL);
}

/*
=================
Skywind_Cvar_Completion_f
=================
*/
static void Skywind_Cvar_Completion_f(cvar_t* cvar, const char* partial)
{
	static const char *const rate_options[] = {
		"0", "0.25", "0.5", "0.75", "1", "1.5", "2", "-1", "-2"
	};
	static const char *const default_options[] = {
		"\"0.25 45 30 0\"",
		"\"0.5 45 30 0\"",
		"\"0.75 45 30 0\"",
		"\"1 45 30 0\"",
		"\"1 90 30 0\"",
		"\"1 180 30 0\"",
		"\"1 45 60 0\"",
		"\"1 45 30 15\"",
		"\"-0.5 45 30 0\""
	};

	for (size_t i = 0; i < (sizeof(rate_options) / sizeof(rate_options[0])); ++i)
		Con_AddToTabList(rate_options[i], partial, "rate", NULL);

	for (size_t i = 0; i < (sizeof(default_options) / sizeof(default_options[0])); ++i)
		Con_AddToTabList(default_options[i], partial, "defaults", NULL);
}

/*
=============
Sky_Init
=============
*/
void Sky_Init (void)
{
	int		i;

	Cvar_RegisterVariable (&r_fastsky);
	Cvar_SetCompletion (&r_fastsky, &Sky_FastSky_Completion_f);
	Cvar_RegisterVariable (&r_fastskycolor); // woods #fastskycolor
	Cvar_SetCompletion (&r_fastskycolor, &SKy_Color_Completion_f); // woods #iwtabcomplete #fastskycolor
	Cvar_SetCallback (&r_fastskycolor, Sky_FastSkyColorChanged);
	Cvar_RegisterVariable (&r_sky_quality);
	Cvar_RegisterVariable (&r_skyalpha);
	Cvar_RegisterVariable (&r_skyfog);
	Cvar_SetCallback (&r_skyfog, R_SetSkyfog_f);
	Cvar_RegisterVariable (&r_skyspeed); // woods #skyspeed
	Cvar_RegisterVariable (&allow_download_sky); // woods automatic skybox downloading #skydownloads
	Cvar_RegisterVariable (&r_globalsky);
	Cvar_SetCallback (&r_globalsky, Sky_GlobalSkyboxChanged);
	Cvar_RegisterVariable (&r_skywind);
	Cvar_SetCompletion (&r_skywind, &Skywind_Cvar_Completion_f); // woods #iwtabcomplete
	Cvar_SetCallback (&r_skywind, &Skywind_Cvar_OnChange);

	Cmd_AddCommand ("sky",Sky_SkyCommand_f);
	Cmd_AddCommand ("skyroom",Sky_SkyRoomCommand_f);
	Cmd_AddCommand ("skywind",Skywind_f);
	Cmd_AddCommand ("skywind_save",Skywind_Save_f);
	Cmd_AddCommand ("skywind_load",Skywind_Load_f);
	Cmd_AddCommand ("skywind_lookdir",Skywind_LookDir_f);
	Cmd_AddCommand ("skywind_rotate",Skywind_Rotate_f);

	skybox_name[0] = 0;
	map_skybox_name[0] = 0;
	pending_skybox_name[0] = 0;
	skybox_download_pending = false;
	for (i=0; i<6; i++)
		skybox_textures[i] = NULL;
}

//==============================================================================
//
//  PROCESS SKY SURFS
//
//==============================================================================

/*
=================
Sky_ProjectPoly

update sky bounds
=================
*/
void Sky_ProjectPoly (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];

		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}

/*
=================
Sky_ClipPoly
=================
*/
static void Sky_ClipPoly (int nump, vec3_t vecs, int stage)
{
	const float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	int		newc[2];
	int		i, j;

	const int max_clip_verts = nump + 2;
	const int on_heap = max_clip_verts > MAX_CLIP_VERTS;
	int	*sides;
	float	*dists;
	vec3_t	*newv_0;
	vec3_t	*newv_1;

	if (stage == 6) // fully clipped
	{
		Sky_ProjectPoly (nump, vecs);
		return;
	}

	front = back = false;
	norm = skyclip[stage];

	sides = (int *) (on_heap ? malloc(max_clip_verts * sizeof(int)) : alloca(max_clip_verts * sizeof(int)));
	dists = (float *) (on_heap ? malloc(max_clip_verts * sizeof(float)) : alloca(max_clip_verts * sizeof(float)));

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		Sky_ClipPoly (nump, vecs, stage+1);
		if (on_heap) {
			free(dists);
			free(sides);
		}
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	// 2-dim vec3_t	 newv[2][MAX_CLIP_VERTS]; as 2 arrays
	newv_0 = (vec3_t *) (on_heap ? malloc(max_clip_verts * sizeof(vec3_t)) : alloca(max_clip_verts * sizeof(vec3_t)));
	newv_1 = (vec3_t *) (on_heap ? malloc(max_clip_verts * sizeof(vec3_t)) : alloca(max_clip_verts * sizeof(vec3_t)));

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv_0[newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv_1[newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv_0[newc[0]]);
			newc[0]++;
			VectorCopy (v, newv_1[newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv_0[newc[0]][j] = e;
			newv_1[newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	Sky_ClipPoly (newc[0], newv_0[0], stage+1);
	Sky_ClipPoly (newc[1], newv_1[0], stage+1);

	if (on_heap)
	{
		free(dists);
		free(sides);
		free(newv_0);
		free(newv_1);
	}
}

/*
================
Sky_ProcessPoly
================
*/
void Sky_ProcessPoly (glpoly_t	*p)
{
	//draw it
	DrawGLPoly(p);
	rs_brushpasses++;

	//update sky bounds
	if ((r_fastsky.value == 0) ||
		(r_fastsky.value == 2 && (skybox_name[0] || externalskyloaded)))
	{
		const int max_clip_verts = p->numverts + 2;
		const int num_verts = p->numverts;
		const int on_heap = max_clip_verts > MAX_CLIP_VERTS;
		vec3_t *verts = (vec3_t *) (on_heap ?
				 malloc(max_clip_verts * sizeof(vec3_t)) :
				 alloca(max_clip_verts * sizeof(vec3_t)));
		int i = 0;

		for ( ; i < num_verts; i++) {
			VectorSubtract (p->verts[i], r_origin, verts[i]);
		}
		Sky_ClipPoly (num_verts, verts[0], 0);

		if (on_heap) free(verts);
	}

}

/*
================
Sky_ProcessTextureChains -- handles sky polys in world model
================
*/
void Sky_ProcessTextureChains (void)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;

	if (!r_drawworld_cheatsafe)
		return;

	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t || !t->texturechains[chain_world] || !(t->texturechains[chain_world]->flags & SURF_DRAWSKY))
			continue;

		for (s = t->texturechains[chain_world]; s; s = s->texturechain)
			Sky_ProcessPoly (s->polys);
	}
}

/*
================
Sky_ProcessEntities -- handles sky polys on brush models
================
*/
void Sky_ProcessEntities (void)
{
	entity_t	*e;
	msurface_t	*s;
	glpoly_t	*p;
	int			i,j,k,mark;
	float		dot;
	qboolean	rotated;
	vec3_t		temp, forward, right, up;

	if (!r_drawentities.value)
		return;

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		e = cl_visedicts[i];

		if (!e->model || e->model->needload || e->model->type != mod_brush)
			continue;

		if (e->model->submodelof == cl.worldmodel &&
			skipsubmodels &&
			skipsubmodels[e->model->submodelidx>>3]&(1u<<(e->model->submodelidx&7)))
			return;	//its in the scenecache that we're drawing. don't draw it twice (and certainly not the slow way).

		if (R_CullModelForEntity(e))
			continue;

		if (e->alpha == ENTALPHA_ZERO)
			continue;

		VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
		if (e->angles[0] || e->angles[1] || e->angles[2])
		{
			rotated = true;
			AngleVectors (e->angles, forward, right, up);
			VectorCopy (modelorg, temp);
			modelorg[0] = DotProduct (temp, forward);
			modelorg[1] = -DotProduct (temp, right);
			modelorg[2] = DotProduct (temp, up);
		}
		else
			rotated = false;

		s = &e->model->surfaces[e->model->firstmodelsurface];

		for (j=0 ; j<e->model->nummodelsurfaces ; j++, s++)
		{
			if (s->flags & SURF_DRAWSKY)
			{
				dot = DotProduct (modelorg, s->plane->normal) - s->plane->dist;
				if (((s->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
					(!(s->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
				{
					//copy the polygon and translate manually, since Sky_ProcessPoly needs it to be in world space
					mark = Hunk_LowMark();
					p = (glpoly_t *) Hunk_Alloc (sizeof(*s->polys)); //FIXME: don't allocate for each poly
					p->numverts = s->polys->numverts;
					for (k=0; k<p->numverts; k++)
					{
						if (rotated)
						{
							p->verts[k][0] = e->origin[0] + s->polys->verts[k][0] * forward[0]
														  - s->polys->verts[k][1] * right[0]
														  + s->polys->verts[k][2] * up[0];
							p->verts[k][1] = e->origin[1] + s->polys->verts[k][0] * forward[1]
														  - s->polys->verts[k][1] * right[1]
														  + s->polys->verts[k][2] * up[1];
							p->verts[k][2] = e->origin[2] + s->polys->verts[k][0] * forward[2]
														  - s->polys->verts[k][1] * right[2]
														  + s->polys->verts[k][2] * up[2];
						}
						else
							VectorAdd(s->polys->verts[k], e->origin, p->verts[k]);
					}
					Sky_ProcessPoly (p);
					Hunk_FreeToLowMark (mark);
				}
			}
		}
	}
}

//==============================================================================
//
//  RENDER SKYBOX
//
//==============================================================================

/*
==============
Sky_EmitSkyBoxVertex
==============
*/
void Sky_EmitSkyBoxVertex (float s, float t, int axis)
{
	vec3_t		v, b, dir, drawdir;
	int			j, k;
	float		w, h;
	float		geom_s, geom_t;
	float		base_s, base_t;
	float		primary_s, primary_t;
	float		secondary_s, secondary_t;
	float		tex_base_s, tex_base_t;
	float		tex_primary_s, tex_primary_t;
	float		tex_secondary_s, tex_secondary_t;
	float		sample_s, sample_t;

	geom_s = s;
	geom_t = t;
	base_s = s;
	base_t = t;
	primary_s = base_s;
	primary_t = base_t;
	secondary_s = base_s;
	secondary_t = base_t;

	b[0] = geom_s * gl_farclip.value / sqrt(3.0);
	b[1] = geom_t * gl_farclip.value / sqrt(3.0);
	b[2] = gl_farclip.value / sqrt(3.0);

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			dir[j] = -b[-k - 1];
		else
			dir[j] = b[k - 1];
	}

	Sky_RotateSkyboxDir(dir, drawdir);
	for (j=0 ; j<3 ; j++)
		v[j] = drawdir[j] + r_origin[j];

	if (skywind_cubemap_sampling)
	{
		glTexCoord3f (-dir[1], dir[2], dir[0]);
		glVertex3fv (v);
		return;
	}

	if (skywind_apply_offsets)
	{
		vec3_t dir_cube;
		vec3_t dir_norm;
		vec3_t dir_shift;
		vec3_t dir_world;

		dir_cube[0] = -dir[1];
		dir_cube[1] = dir[2];
		dir_cube[2] = dir[0];

		VectorCopy(dir_cube, dir_norm);
		if (VectorNormalize(dir_norm) == 0.0f)
		{
			dir_norm[0] = 0.0f;
			dir_norm[1] = 0.0f;
			dir_norm[2] = 0.0f;
		}

		VectorMA(dir_norm, skywind_primary_phase, skywind_frame_dir, dir_shift);
		dir_world[0] = dir_shift[2];
		dir_world[1] = -dir_shift[0];
		dir_world[2] = dir_shift[1];
		Skywind_ProjectDirToST(dir_world, axis, &primary_s, &primary_t);
		primary_s = Skywind_WrapCoord(primary_s);
		primary_t = Skywind_WrapCoord(primary_t);

		if (skywind_shader_enabled)
		{
			VectorMA(dir_norm, skywind_secondary_phase, skywind_frame_dir, dir_shift);
			dir_world[0] = dir_shift[2];
			dir_world[1] = -dir_shift[0];
			dir_world[2] = dir_shift[1];
			Skywind_ProjectDirToST(dir_world, axis, &secondary_s, &secondary_t);
			secondary_s = Skywind_WrapCoord(secondary_s);
			secondary_t = Skywind_WrapCoord(secondary_t);
		}
		else
		{
			base_s = primary_s;
			base_t = primary_t;
		}
	}

	w = skybox_textures[skytexorder[axis]]->width;
	h = skybox_textures[skytexorder[axis]]->height;

	tex_base_s = (base_s + 1) * 0.5f;
	tex_base_t = (base_t + 1) * 0.5f;
	tex_primary_s = (primary_s + 1) * 0.5f;
	tex_primary_t = (primary_t + 1) * 0.5f;
	tex_secondary_s = (secondary_s + 1) * 0.5f;
	tex_secondary_t = (secondary_t + 1) * 0.5f;

	tex_base_s = tex_base_s * (w - 1) / w + 0.5f / w;
	tex_base_t = tex_base_t * (h - 1) / h + 0.5f / h;
	tex_primary_s = tex_primary_s * (w - 1) / w + 0.5f / w;
	tex_primary_t = tex_primary_t * (h - 1) / h + 0.5f / h;
	tex_secondary_s = tex_secondary_s * (w - 1) / w + 0.5f / w;
	tex_secondary_t = tex_secondary_t * (h - 1) / h + 0.5f / h;

	tex_base_t = 1.0f - tex_base_t;
	tex_primary_t = 1.0f - tex_primary_t;
	tex_secondary_t = 1.0f - tex_secondary_t;

	if (skywind_shader_enabled && GL_MTexCoord2fFunc)
	{
		GL_MTexCoord2fFunc(GL_TEXTURE0_ARB, tex_base_s, tex_base_t);
		GL_MTexCoord2fFunc(GL_TEXTURE1_ARB, tex_primary_s, tex_primary_t);
		GL_MTexCoord2fFunc(GL_TEXTURE2_ARB, tex_secondary_s, tex_secondary_t);
		sample_s = tex_base_s;
		sample_t = tex_base_t;
	}
	else
	{
		sample_s = tex_primary_s;
		sample_t = tex_primary_t;
	}
	glTexCoord2f (sample_s, sample_t);
	glVertex3fv (v);
}

static void Sky_DrawSkyBoxFogOverlay(void)
{
	int i;
	float fog_density;

	fog_density = Fog_GetGlobalDensity();
	if (fog_density <= 0.0f || skyfog <= 0.0f)
		return;

	glEnable (GL_BLEND);
	glDisable (GL_TEXTURE_2D);

	{
		float *c = Fog_GetGlobalColor();
		glColor4f (c[0], c[1], c[2], CLAMP(0.0f, skyfog, 1.0f));
	}

	for (i = 0; i < 6; ++i)
	{
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
			continue;

		glBegin (GL_QUADS);
		Sky_EmitSkyBoxVertex (skymins[0][i], skymins[1][i], i);
		Sky_EmitSkyBoxVertex (skymins[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex (skymaxs[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex (skymaxs[0][i], skymins[1][i], i);
		glEnd ();

		rs_skypasses++;
	} 
	glColor3f (1, 1, 1);
	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
}

static void Sky_DrawSkyBox_Static(void)
{
	int i;

	skywind_apply_offsets = false;
	skywind_shader_enabled = false;

	glColor4f (1, 1, 1, 1);

	for (i = 0; i < 6; ++i)
	{
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
			continue;

		GL_Bind (skybox_textures[skytexorder[i]]);

#if 1 /* FIXME: this is to avoid tjunctions until i can do it the right way */
		skymins[0][i] = -1;
		skymins[1][i] = -1;
		skymaxs[0][i] = 1;
		skymaxs[1][i] = 1;
#endif
		glBegin (GL_QUADS);
		Sky_EmitSkyBoxVertex (skymins[0][i], skymins[1][i], i);
		Sky_EmitSkyBoxVertex (skymins[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex (skymaxs[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex (skymaxs[0][i], skymins[1][i], i);
		glEnd ();

		rs_skypolys++;
		rs_skypasses++;
	} 
	Sky_DrawSkyBoxFogOverlay();
}

/*
==============
Sky_DrawSkyBox

FIXME: eliminate cracks by adding an extra vert on tjuncs
==============
*/
void Sky_DrawSkyBox (void)
{
	vec3_t wind_dir;
	float phase;

	Sky_ExpandSkyboxBoundsForRotation();

	if (Skywind_GetDirectionAndPhase(wind_dir, &phase))
	{
		if (!Skywind_DrawSkyBox_Shader(wind_dir, phase))
		{
			Sky_DrawSkyBox_Static();
			return;
		}

		glColor4f (1, 1, 1, 1);
		//Skywind shader handles fog internally now
		//Sky_DrawSkyBoxFogOverlay();
		return;
	}

	Sky_DrawSkyBox_Static();
}

//==============================================================================
//
//  RENDER CLOUDS
//
//==============================================================================

/*
==============
Sky_SetBoxVert
==============
*/
void Sky_SetBoxVert (float s, float t, int axis, vec3_t v)
{
	vec3_t		b;
	int			j, k;

	b[0] = s * gl_farclip.value / sqrt(3.0);
	b[1] = t * gl_farclip.value / sqrt(3.0);
	b[2] = gl_farclip.value / sqrt(3.0);

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
		v[j] += r_origin[j];
	}
}

/*
=============
Sky_GetTexCoord
=============
*/
void Sky_GetTexCoord (vec3_t v, float speed, float *s, float *t)
{
	vec3_t	dir;
	float	length, scroll;

	float clamped_skyspeed = CLAMP(0, r_skyspeed.value, 100); // woods #skyspeed

	VectorSubtract (v, r_origin, dir);
	dir[2] *= 3;	// flatten the sphere

	length = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
	length = sqrt (length);
	length = 6*63/length;

	scroll = cl.time * speed * clamped_skyspeed; // woods #skyspeed
	scroll -= (int)scroll & ~127;

	*s = (scroll + dir[0] * length) * (1.0/128);
	*t = (scroll + dir[1] * length) * (1.0/128);
}

/*
===============
Sky_DrawFaceQuad
===============
*/
void Sky_DrawFaceQuad (glpoly_t *p)
{
	float	s, t;
	float	*v;
	int		i;

	if (gl_mtexable && r_skyalpha.value >= 1.0)
	{
		GL_Bind (solidskytexture);
		GL_EnableMultitexture();
		GL_Bind (alphaskytexture);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);

		glBegin (GL_QUADS);
		for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
		{
			Sky_GetTexCoord (v, 8, &s, &t);
			GL_MTexCoord2fFunc (GL_TEXTURE0_ARB, s, t);
			Sky_GetTexCoord (v, 16, &s, &t);
			GL_MTexCoord2fFunc (GL_TEXTURE1_ARB, s, t);
			glVertex3fv (v);
		}
		glEnd ();

		GL_DisableMultitexture();

		rs_skypolys++;
		rs_skypasses++;
	}
	else
	{
		GL_Bind (solidskytexture);

		if (r_skyalpha.value < 1.0)
			glColor3f (1, 1, 1);

		glBegin (GL_QUADS);
		for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
		{
			Sky_GetTexCoord (v, 8, &s, &t);
			glTexCoord2f (s, t);
			glVertex3fv (v);
		}
		glEnd ();

		GL_Bind (alphaskytexture);
		glEnable (GL_BLEND);

		if (r_skyalpha.value < 1.0)
			glColor4f (1, 1, 1, r_skyalpha.value);

		glBegin (GL_QUADS);
		for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
		{
			Sky_GetTexCoord (v, 16, &s, &t);
			glTexCoord2f (s, t);
			glVertex3fv (v);
		}
		glEnd ();

		glDisable (GL_BLEND);

		rs_skypolys++;
		rs_skypasses += 2;
	}

	if (Fog_GetGlobalDensity() > 0 && skyfog > 0)
	{
		float *c;

		c = Fog_GetGlobalColor();
		glEnable (GL_BLEND);
		glDisable (GL_TEXTURE_2D);
		glColor4f (c[0],c[1],c[2], CLAMP(0.0f,skyfog,1.0f));

		glBegin (GL_QUADS);
		for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
			glVertex3fv (v);
		glEnd ();

		glColor3f (1, 1, 1);
		glEnable (GL_TEXTURE_2D);
		glDisable (GL_BLEND);

		rs_skypasses++;
	}
}

/*
==============
Sky_DrawFace
==============
*/

void Sky_DrawFace (int axis)
{
	glpoly_t	*p;
	vec3_t		verts[4];
	int			i, j, start;
	float		di,qi,dj,qj;
	vec3_t		up, right, temp, temp2;

	Sky_SetBoxVert(-1.0, -1.0, axis, verts[0]);
	Sky_SetBoxVert(-1.0,  1.0, axis, verts[1]);
	Sky_SetBoxVert(1.0,   1.0, axis, verts[2]);
	Sky_SetBoxVert(1.0,  -1.0, axis, verts[3]);

	start = Hunk_LowMark ();
	p = (glpoly_t *) Hunk_Alloc(sizeof(glpoly_t));

	VectorSubtract(verts[2],verts[3],up);
	VectorSubtract(verts[2],verts[1],right);

	di = q_max((int)r_sky_quality.value, 1);
	qi = 1.0 / di;
	dj = (axis < 4) ? di*2 : di; //subdivide vertically more than horizontally on skybox sides
	qj = 1.0 / dj;

	for (i=0; i<di; i++)
	{
		for (j=0; j<dj; j++)
		{
			if (i*qi < skymins[0][axis]/2+0.5 - qi || i*qi > skymaxs[0][axis]/2+0.5 ||
				j*qj < skymins[1][axis]/2+0.5 - qj || j*qj > skymaxs[1][axis]/2+0.5)
				continue;

			//if (i&1 ^ j&1) continue; //checkerboard test
			VectorScale (right, qi*i, temp);
			VectorScale (up, qj*j, temp2);
			VectorAdd(temp,temp2,temp);
			VectorAdd(verts[0],temp,p->verts[0]);

			VectorScale (up, qj, temp);
			VectorAdd (p->verts[0],temp,p->verts[1]);

			VectorScale (right, qi, temp);
			VectorAdd (p->verts[1],temp,p->verts[2]);

			VectorAdd (p->verts[0],temp,p->verts[3]);

			Sky_DrawFaceQuad (p);
		}
	}
	Hunk_FreeToLowMark (start);
}

/*
==============
Sky_DrawSkyLayers

draws the old-style scrolling cloud layers
==============
*/
void Sky_DrawSkyLayers (void)
{
	int i;
	if (!solidskytexture || !alphaskytexture)
		return;

	if (r_skyalpha.value < 1.0)
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	for (i=0 ; i<6 ; i++)
		if (skymins[0][i] < skymaxs[0][i] && skymins[1][i] < skymaxs[1][i])
			Sky_DrawFace (i);

	if (r_skyalpha.value < 1.0)
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/*
==============
Sky_DrawSky

called once per frame before drawing anything else
==============
*/
void Sky_DrawSky (void)
{
	int i;
	qboolean stencil_skybox = false;

	//in these special render modes, the sky faces are handled in the normal world/brush renderer
	if (r_drawflat_cheatsafe|| r_lightmap_cheatsafe)
		return;

	if (skyroom_drawn)
	{	//Spike: We already drew a skyroom underneath. If we draw an actual sky now then we'll have wasted all that effort.
		//however, if we fiddle with stuff, we can make sure that other surfaces don't draw over it either.

		int			i;
		msurface_t	*s;
		texture_t	*t;

		glColorMask(false,false,false,false);
		glDisable (GL_TEXTURE_2D);
		for (i=0 ; i<cl.worldmodel->numtextures ; i++)
		{
			t = cl.worldmodel->textures[i];

			if (!t || !t->texturechains[chain_world] || !(t->texturechains[chain_world]->flags & SURF_DRAWSKY))
				continue;

			for (s = t->texturechains[chain_world]; s; s = s->texturechain)
			{
				DrawGLPoly(s->polys);
				rs_brushpasses++;
				Sky_ProcessPoly (s->polys);
			}
		}
		glEnable (GL_TEXTURE_2D);
		glColorMask(true,true,true,true);
		return;
	}

	//
	// reset sky bounds
	//
	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = FLT_MAX;
		skymaxs[0][i] = skymaxs[1][i] = -FLT_MAX;
	}

	//
	// process world and bmodels: draw flat-shaded sky surfs, and update skybounds
	//
	Fog_DisableGFog ();
	glDisable (GL_TEXTURE_2D);
	if (Fog_GetGlobalDensity() > 0)
		glColor3fv (Fog_GetGlobalColor());
	else
		glColor3fv (skyflatcolor);

	if (skybox_name[0])
	{
		glEnable (GL_STENCIL_TEST);
		glStencilMask (1);
		glStencilFunc (GL_ALWAYS, 1, 1);
		glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE);
		stencil_skybox = true;
	}
#ifndef SDL_THREADS_DISABLED
	if (skybox_name[0] &&
		((r_fastsky.value == 2) || !r_fastsky.value) &&
		RSceneCache_DrawSkySurfDepth()) // woods -- #fastsky2
	{	//we have no surfaces to process... fill all sides. its probably still faster.
		for (i=0 ; i<6 ; i++)
		{
			skymins[0][i] = skymins[1][i] = -FLT_MAX;
			skymaxs[0][i] = skymaxs[1][i] = FLT_MAX;
		}
	}
	else
#endif
		Sky_ProcessTextureChains ();
	Sky_ProcessEntities ();
	glColor3f (1, 1, 1);
	glEnable (GL_TEXTURE_2D);

	if (stencil_skybox)
	{
		glStencilFunc (GL_EQUAL, 1, 1);
		glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
		glStencilMask (0);
	}

	//
	// render slow sky: cloud layers or skybox
	//
	if ((!r_fastsky.value && !(Fog_GetGlobalDensity() > 0 && skyfog >= 1)) ||
		(r_fastsky.value == 2 && (skybox_name[0] || externalskyloaded) && !(Fog_GetGlobalDensity() > 0 && skyfog >= 1))) // woods -- #fastsky2 | r_fastsky 2 gives skybox precedence over fastsky, but fallback if not skybox
	{
		glDepthFunc(GL_GEQUAL);
		glDepthMask(0);

		if (skybox_name[0])
			Sky_DrawSkyBox ();
		else
			Sky_DrawSkyLayers ();

		glDepthMask(1);
		glDepthFunc(GL_LEQUAL);
	}

	if (stencil_skybox)
	{
		glDisable (GL_STENCIL_TEST);
		glStencilMask (1);
	}

	Fog_EnableGFog ();
}
