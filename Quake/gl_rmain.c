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
// r_main.c

#include "quakedef.h"
#include "glquake.h"
#include <math.h> // woods #beamspoly

vec3_t		modelorg, r_entorigin;

static entity_t r_worldentity;	//so we can make sure currententity is valid
entity_t	*currententity;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

//johnfitz -- rendering statistics
int rs_brushpolys, rs_aliaspolys, rs_skypolys;
int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;
mat4_t	r_world_matrix;
mat4_t	r_projection_matrix;
int		r_viewport[4];
qboolean	r_view_matrices_valid;

float r_fovx, r_fovy; //johnfitz -- rendering fov may be different becuase of r_waterwarp and r_stereo
static qboolean water_warp;

extern byte* SV_FatPVS (vec3_t org, qmodel_t* worldmodel); // woods #iwshowbboxes
extern qboolean SV_EdictInPVS (edict_t* test, byte* pvs); // woods #iwshowbboxes
extern qboolean SV_BoxInPVS (vec3_t mins, vec3_t maxs, byte* pvs, mnode_t* node); // woods #iwshowbboxes
extern char	skybox_name[1024]; // woods -- #fastsky2
extern qboolean externalskyloaded; // woods #fastsky2

//
// screen size info
//
refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

int		d_lightstylevalue[MAX_LIGHTSTYLES];	// 8.8 fraction of base light value

cvar_t	cl_damagehue = {"cl_damagehue", "1",CVAR_ARCHIVE};  // woods #damage
cvar_t	cl_damagehuecolor = {"cl_damagehuecolor", "0xeb580e",CVAR_ARCHIVE};  // woods #damage
cvar_t	cl_autodemo = {"cl_autodemo","0",CVAR_ARCHIVE};	//R00k   // woods #autodemo

cvar_t	r_norefresh = {"r_norefresh","0",CVAR_NONE};
cvar_t	r_drawentities = {"r_drawentities","1",CVAR_NONE};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1",CVAR_ARCHIVE};
cvar_t	r_speeds = {"r_speeds","0",CVAR_NONE};
cvar_t	r_pos = {"r_pos","0",CVAR_NONE};
cvar_t	r_fullbright = {"r_fullbright","0",CVAR_NONE};
cvar_t	r_lightmap = {"r_lightmap","0",CVAR_ARCHIVE};
cvar_t	r_shadows = {"r_shadows","0",CVAR_ARCHIVE};
cvar_t	r_shadows_groundcheck = {"r_shadows_groundcheck","1",CVAR_ARCHIVE}; // woods #shadow
cvar_t	r_shadows_bmodels = {"r_shadows_bmodels","0",CVAR_ARCHIVE}; // woods #shadow
cvar_t	r_wateralpha = {"r_wateralpha","1",CVAR_ARCHIVE};
cvar_t	r_dynamic = {"r_dynamic","1",CVAR_ARCHIVE};
cvar_t	r_novis = {"r_novis","0",CVAR_ARCHIVE};

cvar_t	gl_finish = {"gl_finish","0",CVAR_NONE};
cvar_t	gl_clear = {"gl_clear","1",CVAR_NONE};
cvar_t	gl_cull = {"gl_cull","1",CVAR_NONE};
cvar_t	gl_smoothmodels = {"gl_smoothmodels","1",CVAR_NONE};
cvar_t	gl_affinemodels = {"gl_affinemodels","0",CVAR_NONE};
cvar_t	gl_polyblend = {"gl_polyblend","1",CVAR_ARCHIVE};
cvar_t	gl_flashblend = {"gl_flashblend","0",CVAR_ARCHIVE};
cvar_t	gl_playermip = {"gl_playermip","0",CVAR_NONE};
cvar_t	gl_nocolors = {"gl_nocolors","0",CVAR_NONE};
cvar_t	gl_enemycolor = {"gl_enemycolor","",CVAR_ARCHIVE}; // woods #enemycolors
cvar_t	gl_teamcolor = { "gl_teamcolor","",CVAR_ARCHIVE}; // woods #enemycolors
cvar_t	gl_laserpoint = {"gl_laserpoint","0", CVAR_ARCHIVE }; // woods #laser
cvar_t	gl_laserpoint_alpha = { "gl_laserpoint_alpha",".3", CVAR_ARCHIVE }; // woods #laser
cvar_t	gl_powerupshells = {"gl_powerupshells","1",CVAR_ARCHIVE}; // woods #powershell
cvar_t  gl_powerupshells_alpha = {"gl_powerupshells_alpha", "0.3"}; // woods #powershell
cvar_t	gl_motion_blur = {"gl_motion_blur", "0", CVAR_ARCHIVE}; // woods #motionblur

//johnfitz -- new cvars
cvar_t	r_stereo = {"r_stereo","0",CVAR_NONE};
cvar_t	r_stereodepth = {"r_stereodepth","128",CVAR_NONE};
cvar_t	r_clearcolor = {"r_clearcolor","2",CVAR_ARCHIVE};
cvar_t	r_drawflat = {"r_drawflat","0",CVAR_NONE};
cvar_t	r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t	gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE};
cvar_t	gl_farclip = {"gl_farclip", "65536", CVAR_ARCHIVE};
cvar_t	gl_overbright = {"gl_overbright", "1", CVAR_ARCHIVE};
cvar_t	gl_caustics = {"gl_caustics", ".5", CVAR_ARCHIVE}; // woods #caustics
cvar_t	r_grass = {"r_grass", "0", CVAR_ARCHIVE}; // woods #grass
cvar_t	r_grass_tex = {"r_grass_tex", "ground1_1,ground1_6,wgrnd1_6,wgrass1_1", CVAR_ARCHIVE}; // woods #grass
cvar_t	gl_overbright_models = {"gl_overbright_models", "1", CVAR_ARCHIVE};
cvar_t    r_aliaslightcache = {"r_aliaslightcache", "1", CVAR_ARCHIVE}; // tb -- cache unmoving alias model lightpoint samples
cvar_t	r_model_light_desat = {"r_model_light_desat", "-2", CVAR_ARCHIVE}; // woods #dedat
cvar_t	r_model_light_desat_list = {"r_models_light_desat_list", "progs/armor.mdl,progs/backpack.mdl,progs/bolt.mdl,progs/bolt2.mdl,progs/bolt3.mdl,progs/end1.mdl,progs/end2.mdl,progs/end3.mdl,progs/end4.mdl,progs/eyes.mdl,progs/g_light.mdl,progs/g_nail.mdl,progs/g_nail2.mdl,progs/g_rock.mdl,progs/g_rock2.mdl,progs/g_shot.mdl,progs/grenade.mdl,progs/invisibl.mdl,progs/invulner.mdl,progs/missile.mdl,progs/player.mdl,progs/quaddama.mdl,progs/s_spike.mdl,progs/spike.mdl,progs/w_spike.mdl,progs/bit.mdl,progs/flag.mdl,progs/flag2.mdl,progs/flag3.mdl,progs/ctfmodel.mdl,progs/star.mdl", CVAR_ARCHIVE}; // woods #dedat
cvar_t	r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t	r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t	r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t	r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t	r_showfields = {"r_showfields", "0", CVAR_NONE}; // 0=off; 1=bottom-right; 2=track focused entity
cvar_t	r_showlocs = {"r_showlocs", "0", CVAR_ARCHIVE}; // woods #locext
cvar_t	r_showlocs_y = {"r_showlocs_y", "30", CVAR_ARCHIVE }; // woods #locext
cvar_t	r_lerpmodels = {"r_lerpmodels", "1", CVAR_ARCHIVE};
cvar_t	r_lerpmove = {"r_lerpmove", "1", CVAR_ARCHIVE};
cvar_t	r_nolerp_list = {"r_nolerp_list", "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl", CVAR_ARCHIVE};
cvar_t	r_noshadow_list = {"r_noshadow_list", "progs/flame2.mdl,progs/flame.mdl,progs/bolt1.mdl,progs/bolt2.mdl,progs/bolt3.mdl,progs/laser.mdl", CVAR_ARCHIVE};
cvar_t	r_nooutline_list = {"r_nooutline_list", "progs/bolt1.mdl,progs/bolt2.mdl,progs/bolt3.mdl,progs/bit.mdl, progs/star.mdl", CVAR_ARCHIVE}; // woods #routline
cvar_t	r_outline = {"r_outline", "0", CVAR_ARCHIVE}; // woods #routline
cvar_t	r_outline_minpixels = {"r_outline_minpixels", "1", CVAR_ARCHIVE}; // woods #routline -- skip the per-entity outline pass when its projected width is below this many pixels (sub-pixel outlines are invisible but still cost a full extra draw); 0 disables the cull
cvar_t	r_player_xray = {"r_player_xray", "0xFF0000 1.0 0", CVAR_ARCHIVE}; // woods #routline
#ifdef MACBOOK_ARM_HACK // woods #collinear
cvar_t	r_remove_collinear_vertices = {"r_remove_collinear_vertices", "1", CVAR_ARCHIVE};
#endif

extern cvar_t	r_vfog;
//johnfitz

cvar_t	gl_zfix = {"gl_zfix", "0", CVAR_ARCHIVE}; // QuakeSpasm z-fighting fix

cvar_t	r_lavaalpha = {"r_lavaalpha","0",CVAR_ARCHIVE};
cvar_t	r_telealpha = {"r_telealpha","0",CVAR_ARCHIVE};
cvar_t	r_slimealpha = {"r_slimealpha","0",CVAR_ARCHIVE};

cvar_t	trace_any = {"trace_any","0",CVAR_NONE}; // woods #tracers
cvar_t	trace_any_contains = {"trace_any_contains","item_artifact_super_damage",CVAR_NONE}; // woods #tracers
cvar_t	r_drawflame = {"r_drawflame","1",CVAR_ARCHIVE}; // woods #drawflame
cvar_t	r_alphasort = {"r_alphasort", "1", CVAR_ARCHIVE}; // woods #alphasort

float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;
float	map_fallbackalpha;

int	map_ctf_flag_style; // woods #alternateflags
extern int ogflagprecache, swapflagprecache, swapflagprecache2, swapflagprecache3; // woods #alternateflags

qboolean r_drawflat_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe, r_drawworld_cheatsafe; //johnfitz

cvar_t	r_scale = {"r_scale", "1", CVAR_ARCHIVE};
cvar_t	r_softemu = {"r_softemu", "0", CVAR_ARCHIVE};
cvar_t	r_ambient = {"r_ambient", "0", CVAR_ARCHIVE}; // woods #rambient

edict_t *bbox_focus = NULL;

void LaserSight(void);
static qboolean R_WarpScaleView_EnsureShader (void);
static float R_WaterWarpTime (void);


//==============================================================================
//
// GLSL GAMMA CORRECTION
//
//==============================================================================

static gltexture_t *r_lightningbeam_texture = NULL; // woods #beamspoly
static float r_lightningbeam_scroll = 0.0f; // woods #beamspoly

static GLuint r_gamma_texture;
static GLuint r_gamma_program;
static int r_gamma_texture_width, r_gamma_texture_height;
static GLuint r_gamma_fbo;
static GLuint r_gamma_depth_renderbuffer;
static qboolean r_gamma_fbo_active;	// scene FBO bound for the current frame
static qboolean r_gamma_fbo_failed;	// creation failed; use the copy path until vid_restart
static GLuint r_softemu_lut_texture;
static GLuint r_softemu_palette_texture;
static qboolean r_softemu_lut_built;
static unsigned int r_softemu_lut_palette_hash;
static qboolean r_softemu_palette_texture_allocated;
static qboolean r_softemu_palette_valid;
static unsigned int r_softemu_palette_hash;
static float r_softemu_palette_gamma;
static float r_softemu_palette_contrast;
static vec4_t r_softemu_palette_blend;

#define SOFTEMU_LUT_BITS 6
#define SOFTEMU_LUT_SIZE (1 << SOFTEMU_LUT_BITS)
#define SOFTEMU_LUT_TEXWIDTH 512
#define SOFTEMU_LUT_TEXHEIGHT 512

// uniforms used in gamma shader
static GLint  gammaLoc;
static GLint  contrastLoc;
static GLint  textureLoc;
static GLint  softEmuModeLoc;
static GLint  softEmuLUTLoc;
static GLint  softEmuPaletteLoc;

/*
=============
GLSLGamma_DeleteTexture
=============
*/
void GLSLGamma_DeleteTexture (void)
{
	if (r_gamma_fbo)
	{
		GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
		GL_DeleteFramebuffersFunc (1, &r_gamma_fbo);
		r_gamma_fbo = 0;
	}
	if (r_gamma_depth_renderbuffer)
	{
		GL_DeleteRenderbuffersFunc (1, &r_gamma_depth_renderbuffer);
		r_gamma_depth_renderbuffer = 0;
	}
	r_gamma_fbo_active = false;
	r_gamma_fbo_failed = false;
	glDeleteTextures (1, &r_gamma_texture);
	glDeleteTextures (1, &r_softemu_lut_texture);
	glDeleteTextures (1, &r_softemu_palette_texture);
	r_gamma_texture = 0;
	r_softemu_lut_texture = 0;
	r_softemu_palette_texture = 0;
	GL_ClearBindings ();
	r_softemu_lut_built = false;
	r_softemu_lut_palette_hash = 0;
	r_softemu_palette_texture_allocated = false;
	r_softemu_palette_valid = false;
	r_softemu_palette_hash = 0;
	r_gamma_program = 0; // deleted in R_DeleteShaders
}

static unsigned int GLSLGamma_SoftEmuPaletteHash (void)
{
	const byte *pal = (const byte *)d_8to24table;
	unsigned int hash = 2166136261u;
	int i;

	for (i = 0; i < 256 * 4; i++)
	{
		hash ^= pal[i];
		hash *= 16777619u;
	}

	return hash;
}

static int GLSLGamma_SoftEmuMode (void)
{
	return CLAMP(0, (int)r_softemu.value, 3);
}

static qboolean GLSLGamma_SoftEmuAvailable (void)
{
	return GLSLGamma_SoftEmuMode() > 0 && gl_glsl_gamma_able && gl_mtexable && gl_max_texture_image_units >= 3;
}

static qboolean GLSLGamma_SoftEmuApplyBlend (void)
{
	if (!gl_polyblend.value || v_blend[3] <= 0.0f)
		return false;
	if ((int)gl_polyblend.value == 2)
		return false;
	return v_blend[0] > 0.001f || v_blend[1] > 0.001f || v_blend[2] > 0.001f;
}

static qboolean GLSLGamma_EnsureSoftEmuLUT (void)
{
	byte *lutdata;
	byte pal[256][3];
	unsigned int palette_hash;
	int r, g, b, i;

	palette_hash = GLSLGamma_SoftEmuPaletteHash();
	if (r_softemu_lut_built && r_softemu_lut_texture && r_softemu_lut_palette_hash == palette_hash)
		return true;

	lutdata = (byte *) malloc(SOFTEMU_LUT_TEXWIDTH * SOFTEMU_LUT_TEXHEIGHT);
	if (!lutdata)
	{
		Con_Warning("softemu: couldn't allocate palette LUT\n");
		return false;
	}

	for (i = 0; i < 256; i++)
	{
		byte *src = (byte *)&d_8to24table[i];
		pal[i][0] = src[0];
		pal[i][1] = src[1];
		pal[i][2] = src[2];
	}

	for (b = 0; b < SOFTEMU_LUT_SIZE; b++)
	{
		int bb = (b * 255 + (SOFTEMU_LUT_SIZE - 1) / 2) / (SOFTEMU_LUT_SIZE - 1);
		for (g = 0; g < SOFTEMU_LUT_SIZE; g++)
		{
			int gg = (g * 255 + (SOFTEMU_LUT_SIZE - 1) / 2) / (SOFTEMU_LUT_SIZE - 1);
			for (r = 0; r < SOFTEMU_LUT_SIZE; r++)
			{
				int rr = (r * 255 + (SOFTEMU_LUT_SIZE - 1) / 2) / (SOFTEMU_LUT_SIZE - 1);
				int best = 0;
				int bestdist = INT_MAX;
				int flat = r + g * SOFTEMU_LUT_SIZE + b * SOFTEMU_LUT_SIZE * SOFTEMU_LUT_SIZE;

				for (i = 0; i < 256; i++)
				{
					int dr = rr - pal[i][0];
					int dg = gg - pal[i][1];
					int db = bb - pal[i][2];
					int dist = dr * dr + dg * dg + db * db;

					if (dist < bestdist)
					{
						bestdist = dist;
						best = i;
						if (!dist)
							break;
					}
				}

				lutdata[flat] = (byte)best;
			}
		}
	}

	if (!r_softemu_lut_texture)
		glGenTextures(1, &r_softemu_lut_texture);
	if (!r_softemu_lut_texture)
	{
		free(lutdata);
		return false;
	}

	GL_SelectTexture(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_2D, r_softemu_lut_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, SOFTEMU_LUT_TEXWIDTH, SOFTEMU_LUT_TEXHEIGHT, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lutdata);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GL_SelectTexture(GL_TEXTURE0_ARB);
	GL_ClearBindings ();

	free(lutdata);
	r_softemu_lut_built = true;
	r_softemu_lut_palette_hash = palette_hash;
	return true;
}

static qboolean GLSLGamma_UpdateSoftEmuPalette (float gamma_value, float contrast_value);

void GLSLGamma_SoftEmuPrecache (void)
{
	float gamma_value;
	float contrast_value;

	if (!GLSLGamma_SoftEmuAvailable())
		return;
	gamma_value = q_min(GAMMA_MAX, q_max(GAMMA_MIN-.3, vid_gamma.value));
	contrast_value = q_min(2.0f, q_max(1.0f, vid_contrast.value));
	if (!GLSLGamma_EnsureSoftEmuLUT())
		return;
	GLSLGamma_UpdateSoftEmuPalette(gamma_value, contrast_value);
	GL_ClearBindings ();
}

static qboolean GLSLGamma_UpdateSoftEmuPalette (float gamma_value, float contrast_value)
{
	byte paldata[256 * 4];
	qboolean blend = GLSLGamma_SoftEmuApplyBlend();
	unsigned int palette_hash = GLSLGamma_SoftEmuPaletteHash();
	vec4_t blendvalue = {0, 0, 0, 0};
	qboolean dirty;
	int i;

	if (!r_softemu_palette_texture)
		glGenTextures(1, &r_softemu_palette_texture);
	if (!r_softemu_palette_texture)
		return false;

	if (blend)
		memcpy(blendvalue, v_blend, sizeof(blendvalue));

	dirty = !r_softemu_palette_valid ||
		r_softemu_palette_hash != palette_hash ||
		r_softemu_palette_gamma != gamma_value ||
		r_softemu_palette_contrast != contrast_value ||
		memcmp(r_softemu_palette_blend, blendvalue, sizeof(blendvalue));

	if (!r_softemu_palette_texture_allocated)
	{
		GL_SelectTexture(GL_TEXTURE2_ARB);
		glBindTexture(GL_TEXTURE_2D, r_softemu_palette_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		GL_SelectTexture(GL_TEXTURE0_ARB);
		GL_ClearBindings ();
		r_softemu_palette_texture_allocated = true;
		dirty = true;
	}

	if (!dirty)
		return true;

	for (i = 0; i < 256; i++)
	{
		byte *src = (byte *)&d_8to24table[i];
		float r = src[0] / 255.0f;
		float g = src[1] / 255.0f;
		float b = src[2] / 255.0f;

		if (blend)
		{
			float a = CLAMP(0.0f, v_blend[3], 1.0f);
			r = r * (1.0f - a) + v_blend[0] * a;
			g = g * (1.0f - a) + v_blend[1] * a;
			b = b * (1.0f - a) + v_blend[2] * a;
		}

		r = powf(q_max(0.0f, r * contrast_value), gamma_value);
		g = powf(q_max(0.0f, g * contrast_value), gamma_value);
		b = powf(q_max(0.0f, b * contrast_value), gamma_value);

		paldata[i * 4 + 0] = (byte)(CLAMP(0.0f, r, 1.0f) * 255.0f + 0.5f);
		paldata[i * 4 + 1] = (byte)(CLAMP(0.0f, g, 1.0f) * 255.0f + 0.5f);
		paldata[i * 4 + 2] = (byte)(CLAMP(0.0f, b, 1.0f) * 255.0f + 0.5f);
		paldata[i * 4 + 3] = 255;
	}

	GL_SelectTexture(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_2D, r_softemu_palette_texture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, paldata);
	GL_SelectTexture(GL_TEXTURE0_ARB);
	GL_ClearBindings ();

	r_softemu_palette_valid = true;
	r_softemu_palette_hash = palette_hash;
	r_softemu_palette_gamma = gamma_value;
	r_softemu_palette_contrast = contrast_value;
	memcpy(r_softemu_palette_blend, blendvalue, sizeof(r_softemu_palette_blend));
	return true;
}

qboolean GLSLGamma_SoftEmuCanRemapBlend (void)
{
	unsigned int palette_hash;

	if (!GLSLGamma_SoftEmuAvailable() || !r_softemu_lut_built || !r_softemu_lut_texture ||
		!r_softemu_palette_texture_allocated || !r_softemu_palette_valid)
		return false;

	palette_hash = GLSLGamma_SoftEmuPaletteHash();
	return r_softemu_lut_palette_hash == palette_hash && r_softemu_palette_hash == palette_hash;
}

/*
=============
GLSLGamma_CreateShaders
=============
*/
static void GLSLGamma_CreateShaders (void)
{
	const GLchar *vertSource = \
		"#version 110\n"
		"\n"
		"void main(void) {\n"
		"	gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);\n"
		"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"}\n";

	const GLchar *fragSource = \
		"#version 110\n"
		"\n"
		"uniform sampler2D GammaTexture;\n"
		"uniform sampler2D SoftEmuLUT;\n"
		"uniform sampler2D SoftEmuPalette;\n"
		"uniform float GammaValue;\n"
		"uniform float ContrastValue;\n"
		"uniform int SoftEmuMode;\n"
		"\n"
		"float softemu_bayer8(void) {\n"
		"	  vec2 p = mod(floor(gl_FragCoord.xy), 8.0);\n"
		"	  float x = p.x;\n"
		"	  float y = p.y;\n"
		"	  float v = 0.0;\n"
		"	  if (y < 1.0) {\n"
		"	      if (x < 1.0) v = 0.0; else if (x < 2.0) v = 48.0; else if (x < 3.0) v = 12.0; else if (x < 4.0) v = 60.0; else if (x < 5.0) v = 3.0; else if (x < 6.0) v = 51.0; else if (x < 7.0) v = 15.0; else v = 63.0;\n"
		"	  } else if (y < 2.0) {\n"
		"	      if (x < 1.0) v = 32.0; else if (x < 2.0) v = 16.0; else if (x < 3.0) v = 44.0; else if (x < 4.0) v = 28.0; else if (x < 5.0) v = 35.0; else if (x < 6.0) v = 19.0; else if (x < 7.0) v = 47.0; else v = 31.0;\n"
		"	  } else if (y < 3.0) {\n"
		"	      if (x < 1.0) v = 8.0; else if (x < 2.0) v = 56.0; else if (x < 3.0) v = 4.0; else if (x < 4.0) v = 52.0; else if (x < 5.0) v = 11.0; else if (x < 6.0) v = 59.0; else if (x < 7.0) v = 7.0; else v = 55.0;\n"
		"	  } else if (y < 4.0) {\n"
		"	      if (x < 1.0) v = 40.0; else if (x < 2.0) v = 24.0; else if (x < 3.0) v = 36.0; else if (x < 4.0) v = 20.0; else if (x < 5.0) v = 43.0; else if (x < 6.0) v = 27.0; else if (x < 7.0) v = 39.0; else v = 23.0;\n"
		"	  } else if (y < 5.0) {\n"
		"	      if (x < 1.0) v = 2.0; else if (x < 2.0) v = 50.0; else if (x < 3.0) v = 14.0; else if (x < 4.0) v = 62.0; else if (x < 5.0) v = 1.0; else if (x < 6.0) v = 49.0; else if (x < 7.0) v = 13.0; else v = 61.0;\n"
		"	  } else if (y < 6.0) {\n"
		"	      if (x < 1.0) v = 34.0; else if (x < 2.0) v = 18.0; else if (x < 3.0) v = 46.0; else if (x < 4.0) v = 30.0; else if (x < 5.0) v = 33.0; else if (x < 6.0) v = 17.0; else if (x < 7.0) v = 45.0; else v = 29.0;\n"
		"	  } else if (y < 7.0) {\n"
		"	      if (x < 1.0) v = 10.0; else if (x < 2.0) v = 58.0; else if (x < 3.0) v = 6.0; else if (x < 4.0) v = 54.0; else if (x < 5.0) v = 9.0; else if (x < 6.0) v = 57.0; else if (x < 7.0) v = 5.0; else v = 53.0;\n"
		"	  } else {\n"
		"	      if (x < 1.0) v = 42.0; else if (x < 2.0) v = 26.0; else if (x < 3.0) v = 38.0; else if (x < 4.0) v = 22.0; else if (x < 5.0) v = 41.0; else if (x < 6.0) v = 25.0; else if (x < 7.0) v = 37.0; else v = 21.0;\n"
		"	  }\n"
		"	  return ((v + 0.5) / 64.0) - 0.5;\n"
		"}\n"
		"\n"
		"void main(void) {\n"
		"	  vec4 frag = texture2D(GammaTexture, gl_TexCoord[0].xy);\n"
		"	  if (SoftEmuMode != 0) {\n"
		"	      vec3 c = frag.rgb;\n"
		"	      if (SoftEmuMode != 3)\n"
		"	          c += vec3(softemu_bayer8() * (1.0 / 63.0));\n"
		"	      c = clamp(c, 0.0, 1.0);\n"
		"	      vec3 q = floor(c * 63.0 + 0.5);\n"
		"	      float flat = q.r + q.g * 64.0 + q.b * 4096.0;\n"
		"	      vec2 lutst = (vec2(mod(flat, 512.0), floor(flat / 512.0)) + 0.5) / vec2(512.0, 512.0);\n"
		"	      float palindex = texture2D(SoftEmuLUT, lutst).r;\n"
		"	      vec2 palst = vec2((palindex * 255.0 + 0.5) / 256.0, 0.5);\n"
		"	      gl_FragColor = vec4(texture2D(SoftEmuPalette, palst).rgb, 1.0);\n"
		"	      return;\n"
		"	  }\n"
		"	  frag.rgb = frag.rgb * ContrastValue;\n"
		"	  gl_FragColor = vec4(pow(frag.rgb, vec3(GammaValue)), 1.0);\n"
		"}\n";

	if (!gl_glsl_gamma_able)
		return;

	r_gamma_program = GL_CreateProgram (vertSource, fragSource, 0, NULL);

// get uniform locations
	gammaLoc = GL_GetUniformLocation (&r_gamma_program, "GammaValue");
	contrastLoc = GL_GetUniformLocation (&r_gamma_program, "ContrastValue");
	textureLoc = GL_GetUniformLocation (&r_gamma_program, "GammaTexture");
	softEmuModeLoc = GL_GetUniformLocation (&r_gamma_program, "SoftEmuMode");
	softEmuLUTLoc = GL_GetUniformLocation (&r_gamma_program, "SoftEmuLUT");
	softEmuPaletteLoc = GL_GetUniformLocation (&r_gamma_program, "SoftEmuPalette");
}

/*
=============
GLSLGamma_NeedsPostPass -- mirrors GLSLGamma_GammaCorrect's early-outs
=============
*/
static qboolean GLSLGamma_NeedsPostPass (void)
{
	if (!gl_glsl_gamma_able)
		return false;
	if (GLSLGamma_SoftEmuMode () && gl_mtexable && gl_max_texture_image_units >= 3)
		return true;
	return vid_gamma.value != 1 || vid_contrast.value != 1;
}

/*
=============
GLSLGamma_CreateFBO -- scene framebuffer whose color attachment is r_gamma_texture

Rendering the frame into an FBO lets GLSLGamma_GammaCorrect skip the
glCopyTexSubImage2D from the default framebuffer, which stalls Apple's GL.
=============
*/
static qboolean GLSLGamma_CreateFBO (int width, int height)
{
	GLenum status;

	if (r_gamma_fbo_failed)
		return false;

	if (r_gamma_fbo && (r_gamma_texture_width != width || r_gamma_texture_height != height))
	{
		GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
		GL_DeleteFramebuffersFunc (1, &r_gamma_fbo);
		r_gamma_fbo = 0;
		if (r_gamma_depth_renderbuffer)
		{
			GL_DeleteRenderbuffersFunc (1, &r_gamma_depth_renderbuffer);
			r_gamma_depth_renderbuffer = 0;
		}
	}

	if (r_gamma_fbo)
		return true;

	if (!r_gamma_texture)
		glGenTextures (1, &r_gamma_texture);

	GL_DisableMultitexture ();
	if (gl_mtexable)
		GL_SelectTexture (GL_TEXTURE0_ARB);
	glBindTexture (GL_TEXTURE_2D, r_gamma_texture);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	r_gamma_texture_width = width;
	r_gamma_texture_height = height;
	GL_ClearBindings ();

	GL_GenRenderbuffersFunc (1, &r_gamma_depth_renderbuffer);
	GL_BindRenderbufferFunc (GL_RENDERBUFFER, r_gamma_depth_renderbuffer);
	GL_RenderbufferStorageFunc (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

	GL_GenFramebuffersFunc (1, &r_gamma_fbo);
	GL_BindFramebufferFunc (GL_FRAMEBUFFER, r_gamma_fbo);
	GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r_gamma_texture, 0);
	GL_FramebufferRenderbufferFunc (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r_gamma_depth_renderbuffer);
	GL_FramebufferRenderbufferFunc (GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, r_gamma_depth_renderbuffer);

	status = GL_CheckFramebufferStatusFunc (GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		Con_Warning ("GLSL gamma framebuffer incomplete (status 0x%x), using framebuffer copy\n", status);
		GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
		GL_DeleteFramebuffersFunc (1, &r_gamma_fbo);
		r_gamma_fbo = 0;
		GL_DeleteRenderbuffersFunc (1, &r_gamma_depth_renderbuffer);
		r_gamma_depth_renderbuffer = 0;
		r_gamma_fbo_failed = true;
		return false;
	}

	return true;
}

/*
=============
GLSLGamma_BeginFrame -- called right after GL_BeginRendering

Binds the scene FBO for the whole frame (3D + 2D) when the gamma post-pass
will run, so GammaCorrect can source it directly instead of copying the
default framebuffer.
=============
*/
void GLSLGamma_BeginFrame (void)
{
	if (r_gamma_fbo_active)	// frame aborted mid-render (Host_Error); restore default target
	{
		GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
		r_gamma_fbo_active = false;
	}

	if (!gl_fbo_able || !gl_texture_NPOT || !GLSLGamma_NeedsPostPass ())
		return;

	if (!GLSLGamma_CreateFBO (glwidth, glheight))
		return;

	GL_BindFramebufferFunc (GL_FRAMEBUFFER, r_gamma_fbo);
	glViewport (0, 0, glwidth, glheight);
	{
		byte *rgb = (byte *)(d_8to24table + ((int)r_clearcolor.value & 0xFF));
		glClearColor (rgb[0]/255.0f, rgb[1]/255.0f, rgb[2]/255.0f, 1.0f);
	}
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	r_gamma_fbo_active = true;
}

/*
=============
GLSLGamma_SceneFBO

The render target other post-effects (FXAA, menu previews) must restore
instead of framebuffer 0 while the gamma scene FBO is active.
=============
*/
GLuint GLSLGamma_SceneFBO (void)
{
	return r_gamma_fbo_active ? r_gamma_fbo : 0;
}

/*
=============
GLSLGamma_GammaCorrect
=============
*/
void GLSLGamma_GammaCorrect (void)
{
	int tw=glwidth,th=glheight;
	float smax, tmax;
	float gamma_value; // woods #gammaclamp
	float contrast_value;
	int softemu_mode;
	static qboolean softemu_warned;
	qboolean fbo_active = r_gamma_fbo_active;

	r_gamma_fbo_active = false;

	softemu_mode = GLSLGamma_SoftEmuMode();
	if (!gl_glsl_gamma_able)
	{
		if (softemu_mode && !softemu_warned)
		{
			Con_Warning("r_softemu requires GLSL gamma support\n");
			softemu_warned = true;
		}
		return;
	}

	if (softemu_mode && (!gl_mtexable || gl_max_texture_image_units < 3))
	{
		if (!softemu_warned)
		{
			Con_Warning("r_softemu requires at least 3 shader texture units\n");
			softemu_warned = true;
		}
		softemu_mode = 0;
	}

	// when the scene FBO is active the frame lives in r_gamma_texture and
	// must be drawn to the default framebuffer regardless of the settings
	if (!fbo_active && !softemu_mode && vid_gamma.value == 1 && vid_contrast.value == 1)
		return;

// create render-to-texture texture if needed
	if (!r_gamma_texture)
	{
		glGenTextures (1, &r_gamma_texture);
		r_gamma_texture_width = 0;
		r_gamma_texture_height = 0;
	}
	GL_DisableMultitexture();
	if (gl_mtexable)
		GL_SelectTexture(GL_TEXTURE0_ARB);
	glBindTexture (GL_TEXTURE_2D, r_gamma_texture);

	if (!gl_texture_NPOT)
	{
		tw = TexMgr_Pad(tw);
		th = TexMgr_Pad(th);
	}
	if (r_gamma_texture_width != tw || r_gamma_texture_height != th)
	{
		r_gamma_texture_width = tw;
		r_gamma_texture_height = th;
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, r_gamma_texture_width, r_gamma_texture_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

// create shader if needed
	if (!r_gamma_program)
	{
		GLSLGamma_CreateShaders ();
		if (!r_gamma_program)
		{
			Sys_Error("GLSLGamma_CreateShaders failed");
		}
	}

	gamma_value = q_min(GAMMA_MAX, q_max(GAMMA_MIN-.3, vid_gamma.value)); // woods #gammaclamp
	contrast_value = q_min(2.0f, q_max(1.0f, vid_contrast.value));

	if (softemu_mode)
	{
		if (!GLSLGamma_EnsureSoftEmuLUT() || !GLSLGamma_UpdateSoftEmuPalette(gamma_value, contrast_value))
			softemu_mode = 0;
	}

// get the frame into the texture
	if (gl_mtexable)
		GL_SelectTexture(GL_TEXTURE0_ARB);
	glBindTexture (GL_TEXTURE_2D, r_gamma_texture);
	if (fbo_active)	// scene was rendered into r_gamma_texture via the FBO
		GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
	else
		glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, glx, gly, glwidth, glheight);

// draw the texture back to the framebuffer with a fragment shader
	if (softemu_mode)
	{
		GL_SelectTexture(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_2D, r_softemu_lut_texture);
		GL_SelectTexture(GL_TEXTURE2_ARB);
		glBindTexture(GL_TEXTURE_2D, r_softemu_palette_texture);
		GL_SelectTexture(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_2D, r_gamma_texture);
	}

	GL_UseProgramFunc (r_gamma_program);
	GL_Uniform1fFunc (gammaLoc, gamma_value); // woods #gammaclamp
	GL_Uniform1fFunc (contrastLoc, contrast_value);
	GL_Uniform1iFunc (textureLoc, 0); // use texture unit 0
	GL_Uniform1iFunc (softEmuModeLoc, softemu_mode);
	GL_Uniform1iFunc (softEmuLUTLoc, 1);
	GL_Uniform1iFunc (softEmuPaletteLoc, 2);

	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);

	glViewport (glx, gly, glwidth, glheight);

	smax = glwidth/(float)r_gamma_texture_width;
	tmax = glheight/(float)r_gamma_texture_height;

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);
	glVertex2f (-1, -1);
	glTexCoord2f (smax, 0);
	glVertex2f (1, -1);
	glTexCoord2f (smax, tmax);
	glVertex2f (1, 1);
	glTexCoord2f (0, tmax);
	glVertex2f (-1, 1);
	glEnd ();

	GL_UseProgramFunc (0);
	if (gl_mtexable)
		GL_SelectTexture(GL_TEXTURE0_ARB);

// clear cached binding
	GL_ClearBindings ();
}

/*
=============
R_RenderSceneBlur -- woods - sourced from Qrack #motionblur

Applies motion blur effect by overlaying the previous frame
=============
*/
static GLenum sceneblur_internal_format = GL_RGB8; // or GL_RGBA8 if you add alpha
static GLuint sceneblur_texture = 0; // woods #motionblur
static int sceneblur_texture_width = 0, sceneblur_texture_height = 0; // woods #motionblur

void R_RenderSceneBlur(float alpha)
{
	if (alpha <= 0.0f) return;

	alpha = CLAMP(0.0f, alpha, 0.95f);

	int tgt_w, tgt_h;

	if (gl_texture_NPOT) {
		tgt_w = glwidth;
		tgt_h = glheight;
	}
	else {
		tgt_w = 1; while (tgt_w < glwidth)  tgt_w <<= 1;
		tgt_h = 1; while (tgt_h < glheight) tgt_h <<= 1;
	}

	if (!sceneblur_texture)
		glGenTextures(1, &sceneblur_texture);

	GL_DisableMultitexture();
	glBindTexture(GL_TEXTURE_2D, sceneblur_texture);
	GL_ClearBindings();

	if (sceneblur_texture_width != tgt_w ||
		sceneblur_texture_height != tgt_h)
	{
		// Optional shrink heuristic: if window shrank a lot, reallocate.
		sceneblur_texture_width = tgt_w;
		sceneblur_texture_height = tgt_h;
		glTexImage2D(GL_TEXTURE_2D, 0, sceneblur_internal_format,
			tgt_w, tgt_h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	float s = (float)glwidth / sceneblur_texture_width;
	float t = (float)glheight / sceneblur_texture_height;

	// Save + modify minimal state
	GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
	GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
	GLboolean alphaTestWas = glIsEnabled(GL_ALPHA_TEST);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1.f, 1.f, 1.f, alpha);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, glwidth, 0, glheight, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glBegin(GL_QUADS);
	glTexCoord2f(0.f, 0.f); glVertex2f(0.f, 0.f);
	glTexCoord2f(s, 0.f); glVertex2f((GLfloat)glwidth, 0.f);
	glTexCoord2f(s, t);   glVertex2f((GLfloat)glwidth, (GLfloat)glheight);
	glTexCoord2f(0.f, t);   glVertex2f(0.f, (GLfloat)glheight);
	glEnd();

	// Restore
	glPopMatrix(); // modelview
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);

	glDisable(GL_BLEND);
	if (alphaTestWas) glEnable(GL_ALPHA_TEST);
	if (cullWas)      glEnable(GL_CULL_FACE);
	if (depthWas)     glEnable(GL_DEPTH_TEST);
	glColor4f(1.f, 1.f, 1.f, 1.f);

	// Update history texture
	glBindTexture(GL_TEXTURE_2D, sceneblur_texture);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, glx, gly, glwidth, glheight);
	GL_ClearBindings();
}


/*
=============
R_MotionBlur_DeleteTexture
=============
*/
void R_MotionBlur_DeleteTexture (void) // woods #motionblur
{
	if (sceneblur_texture)
	{
		glDeleteTextures (1, &sceneblur_texture);
		sceneblur_texture = 0;
		sceneblur_texture_width = 0;
		sceneblur_texture_height = 0;
		GL_ClearBindings ();
	}
}

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int i;
	mplane_t *p;
	byte signbits;
	float vec[3];

	for (i = 0;i < 4;i++)
	{
		p = frustum + i;
		signbits = p->signbits;
		vec[0] = ((signbits & 1) ? emins : emaxs)[0];
		vec[1] = ((signbits & 2) ? emins : emaxs)[1];
		vec[2] = ((signbits & 4) ? emins : emaxs)[2];
		if (p->normal[0]*vec[0] + p->normal[1]*vec[1] + p->normal[2]*vec[2] < p->dist)
			return true;
	}
	return false;
}

/*
===============
R_GetEntityBounds -- woods - factor out entity bounds from R_CullModelForEntity #alphasort
===============
*/
static void R_GetEntityBoundsForTransform (const entity_t *e, const vec3_t origin, const vec3_t angles, vec3_t mins, vec3_t maxs)
{
	vec_t scalefactor, *minbounds, *maxbounds;

	if (angles[0] || angles[2]) //pitch or roll
	{
		minbounds = e->model->rmins;
		maxbounds = e->model->rmaxs;
	}
	else if (angles[1]) //yaw
	{
		minbounds = e->model->ymins;
		maxbounds = e->model->ymaxs;
	}
	else //no rotation
	{
		minbounds = e->model->mins;
		maxbounds = e->model->maxs;
	}

	scalefactor = ENTSCALE_DECODE(e->netstate.scale);
	if (scalefactor != 1.0f)
	{
		VectorScale(minbounds, scalefactor, mins);
		VectorScale(maxbounds, scalefactor, maxs);
		VectorAdd(origin, mins, mins);
		VectorAdd(origin, maxs, maxs);
	}
	else
	{
		VectorAdd (origin, minbounds, mins);
		VectorAdd (origin, maxbounds, maxs);
	}
}

void R_GetEntityBounds (const entity_t *e, vec3_t mins, vec3_t maxs)
{
	R_GetEntityBoundsForTransform (e, e->origin, e->angles, mins, maxs);
}

/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation -- woods #alphasort
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;

	R_GetEntityBounds (e, mins, maxs);

	return R_CullBox (mins, maxs);
}

qboolean R_CullModelForEntityTransform (entity_t *e, const vec3_t origin, const vec3_t angles)
{
	vec3_t mins, maxs;

	R_GetEntityBoundsForTransform (e, origin, angles, mins, maxs);

	return R_CullBox (mins, maxs);
}

/*
===============
R_RotateForEntity -- johnfitz -- modified to take origin and angles instead of pointer to entity
===============
*/
void R_RotateForEntity (vec3_t origin, vec3_t angles, entity_t *e)
{
	glTranslatef (origin[0],  origin[1],  origin[2]);
	glRotatef (angles[1],  0, 0, 1);
	glRotatef (-angles[0],  0, 1, 0);
	glRotatef (angles[2],  1, 0, 0);

	if (e->netstate.scale != ENTSCALE_DEFAULT)
	{
		float scalefactor = ENTSCALE_DECODE(e->netstate.scale);

		switch((e->netstate.drawflags>>5)&3)
		{
		case 0/*SCALE_ORIGIN_CENTER...ish*/:
			glTranslatef (0, 0, (e->model->mins[2] + e->model->maxs[2])/2 * (1-scalefactor));
			break;
		case 1/*SCALE_ORIGIN_BOTTOM...ish*/:
			glTranslatef (0, 0, e->model->mins[2] * (1-scalefactor));
			break;
		case 2/*SCALE_ORIGIN_TOP...ish*/:
			glTranslatef (0, 0, e->model->maxs[2] * (1-scalefactor));
			break;
		case 3: //origin no extra translate needed.
			break;
		}
		glScalef(scalefactor, scalefactor, scalefactor);
	}
}

/*
=============
GL_PolygonOffset -- johnfitz

negative offset moves polygon closer to camera
=============
*/
void GL_PolygonOffset (int offset)
{
	if (offset > 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(1, offset);
	}
	else if (offset < 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(-1, offset);
	}
	else
	{
		glDisable (GL_POLYGON_OFFSET_FILL);
		glDisable (GL_POLYGON_OFFSET_LINE);
	}
}

//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float scale_forward, scale_side;

	scale_forward = cos( DEG2RAD( angle ) );
	scale_side = sin( DEG2RAD( angle ) );

	out[0] = scale_forward*forward[0] + scale_side*side[0];
	out[1] = scale_forward*forward[1] + scale_side*side[1];
	out[2] = scale_forward*forward[2] + scale_side*side[2];
}

/*
===============
R_SetFrustum -- johnfitz -- rewritten
===============
*/
void R_SetFrustum (float fovx, float fovy)
{
	int		i;

	if (r_stereo.value)
		fovx += 10; //silly hack so that polygons don't drop out becuase of stereo skew

	TurnVector(frustum[0].normal, vpn, vright, fovx/2 - 90); //left plane
	TurnVector(frustum[1].normal, vpn, vright, 90 - fovx/2); //right plane
	TurnVector(frustum[2].normal, vpn, vup, 90 - fovy/2); //bottom plane
	TurnVector(frustum[3].normal, vpn, vup, fovy/2 - 90); //top plane

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal); //FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
=============
GL_SetFrustum -- johnfitz -- written to replace MYgluPerspective
=============
*/
float frustum_skew = 0.0; //used by r_stereo
/*void GL_SetFrustum(float fovx, float fovy)
{
	float xmax, ymax;
	xmax = NEARCLIP * tan( fovx * M_PI / 360.0 );
	ymax = NEARCLIP * tan( fovy * M_PI / 360.0 );
	glFrustum(-xmax + frustum_skew, xmax + frustum_skew, -ymax, ymax, NEARCLIP, gl_farclip.value);
}*/

/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	int scale;
	int viewx, viewy, vieww, viewh;

	//johnfitz -- rewrote this section
	if (!r_refdef.drawworld)
		scale = 1;	//don't rescale. we can't handle rescaling transparent parts.
	else
		scale =  CLAMP(1, (int)r_scale.value, 4); // ericw -- see R_WarpScaleView
	viewx = glx + r_refdef.vrect.x;
	viewy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	vieww = r_refdef.vrect.width / scale;
	viewh = r_refdef.vrect.height / scale;
	glViewport (viewx, viewy, vieww, viewh);
	r_viewport[0] = viewx;
	r_viewport[1] = viewy;
	r_viewport[2] = vieww;
	r_viewport[3] = viewh;
	//johnfitz

	#if 1	//Spike: these should be equivelent. gpus tend not to use doubles in favour of speed, so no loss there.
	{
		// reduce near clip distance at high FOV's to avoid seeing through walls
		const float w = 1.0f / tanf(r_fovx * (float)M_PI / 360.0f);
		const float h = 1.0f / tanf(r_fovy * (float)M_PI / 360.0f);
		const float d = 12.f * q_min(w, h);
		const float nearclip = CLAMP(0.5f, d, (float)NEARCLIP);

		Matrix4_ProjectionMatrix(r_fovx, r_fovy, nearclip, gl_farclip.value, false, frustum_skew, 0, r_projection_matrix);
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(r_projection_matrix);

		Matrix4_ViewMatrix(r_refdef.viewangles, r_refdef.vieworg, r_world_matrix);
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(r_world_matrix);
    }
	#else
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity ();
	GL_SetFrustum (r_fovx, r_fovy); //johnfitz -- use r_fov* vars

//	glCullFace(GL_BACK); //johnfitz -- glquake used CCW with backwards culling -- let's do it right

	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity ();

    glRotatef (-90,  1, 0, 0);	    // put Z going up
    glRotatef (90,  0, 0, 1);	    // put Z going up
    glRotatef (-r_refdef.viewangles[2],  1, 0, 0);
    glRotatef (-r_refdef.viewangles[0],  0, 1, 0);
    glRotatef (-r_refdef.viewangles[1],  0, 0, 1);
    glTranslatef (-r_refdef.vieworg[0],  -r_refdef.vieworg[1],  -r_refdef.vieworg[2]);
    #endif
	r_view_matrices_valid = true;

	//
	// set drawing parms
	//
	if (gl_cull.value)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_DEPTH_TEST);
}

/*
=============
R_Clear -- johnfitz -- rewritten and gutted
=============
*/
void R_Clear (void)
{
	unsigned int clearbits;

	clearbits = GL_DEPTH_BUFFER_BIT;
	// from mh -- if we get a stencil buffer, we should clear it, even though we don't use it
	if (gl_stencilbits)
	{
		// woods #powershell -- glClear honors the stencil write-mask, and other passes (the
		// powerup shell, model outline, xray) leave it partial (often 0x00). On macOS GL-on-Metal
		// that makes the per-frame stencil clear a no-op, so a freshly-reallocated (garbage)
		// stencil buffer after a fullscreen<->windowed switch never gets wiped -- the buffer
		// saturates to 0xFF and stencil-masked effects break (the viewmodel shell floods the gun).
		// Always restore a full write-mask so glClear actually clears every bit.
		glStencilMask(~0u);
		clearbits |= GL_STENCIL_BUFFER_BIT;
	}
	if (gl_clear.value && !skyroom_drawn && r_refdef.drawworld)
		clearbits |= GL_COLOR_BUFFER_BIT;
	glClear (clearbits);
}

/*
===============
R_SetupScene -- johnfitz -- this is the stuff that needs to be done once per eye in stereo mode
===============
*/
void R_SetupScene (void)
{
	R_SetupGL ();
}

/*
===============
R_SetupView -- johnfitz -- this is the stuff that needs to be done once per frame, even in stereo mode
===============
*/
void R_SetupView (void)
{
	int viewcontents;	//spike -- rewrote this a little
	int i;

	r_framecount++;

	// Need to do those early because we now update dynamic light maps during R_MarkSurfaces
	R_AnimateLight ();

	Skywind_SetupFrame();

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

	if (r_refdef.drawworld)
	{
// current viewleaf
		r_oldviewleaf = r_viewleaf;
		r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);
		viewcontents = r_viewleaf->contents;

		//spike -- FTE_ENT_SKIN_CONTENTS -- added this loop for moving water volumes, to avoid v_cshift etc hacks.
		for (i = 0; i < cl.num_entities && viewcontents == CONTENTS_EMPTY; i++)
		{
			mleaf_t *subleaf;
			vec3_t relpos;
			if (cl.entities[i].model && cl.entities[i].model->type==mod_brush)
			{
				VectorSubtract(r_origin, cl.entities[i].origin, relpos);
				if (cl.entities[i].angles[0] || cl.entities[i].angles[1] || cl.entities[i].angles[2])
				{	//rotate the point, just in case.
					vec3_t axis[3], t;
					AngleVectors(cl.entities[i].angles, axis[0], axis[1], axis[2]);
					VectorCopy(relpos, t);
					relpos[0] = DotProduct(t, axis[0]);
					relpos[0] = -DotProduct(t, axis[1]);
					relpos[0] = DotProduct(t, axis[2]);
				}
				subleaf = Mod_PointInLeaf (relpos, cl.entities[i].model);
				if ((char)cl.entities[i].skinnum < 0)
					viewcontents = ((subleaf->contents == CONTENTS_SOLID)?(char)cl.entities[i].skinnum:CONTENTS_EMPTY);
				else
					viewcontents = subleaf->contents;
			}
		}
	}
	else
		viewcontents = CONTENTS_EMPTY;

	Fog_SetupFrame (viewcontents); //johnfitz
	V_SetContentsColor (viewcontents);
	V_CalcBlend ();

	//johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	water_warp = false;
	if ((int)r_waterwarp.value > 0)
	{
		if (viewcontents == CONTENTS_WATER || viewcontents == CONTENTS_SLIME || viewcontents == CONTENTS_LAVA)
		{
			if ((int)r_waterwarp.value == 1 && R_WarpScaleView_EnsureShader ())
			{
				water_warp = true;
			}
			else
			{
				float warp_time = R_WaterWarpTime ();

				//variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.  what a mess!
				r_fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) * (0.97 + sin(warp_time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
				r_fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) * (1.03 - sin(warp_time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			}
		}
	}
	//johnfitz

	R_SetFrustum (r_fovx, r_fovy); //johnfitz -- use r_fov* vars

	if (r_refdef.drawworld)
	{
		currententity = &r_worldentity;
		R_MarkSurfaces (); //johnfitz -- create texture chains from PVS
		currententity = NULL;
	}

	R_Clear ();

	//johnfitz -- cheat-protect some draw modes
	r_drawflat_cheatsafe = r_fullbright_cheatsafe = r_lightmap_cheatsafe = false;
	r_drawworld_cheatsafe = r_refdef.drawworld;
	if (cl.maxclients == 1 && r_refdef.drawworld)
	{
		if (!r_drawworld.value) r_drawworld_cheatsafe = false;

		if (r_drawflat.value) r_drawflat_cheatsafe = true;
		else if (r_fullbright.value || !cl.worldmodel->lightdata) r_fullbright_cheatsafe = true;
		else if (r_lightmap.value) r_lightmap_cheatsafe = true;
	}
	//johnfitz
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_ShouldDrawEntity -- woods #alphasort
=============
*/
static qboolean R_ShouldDrawEntity(entity_t *ent, qboolean alphapass)
{
	qboolean is_translucent;

	//spike -- this would be more efficient elsewhere, but its more correct here.
	if (ent->eflags & EFLAGS_EXTERIORMODEL)
		return false;
	if (!ent->model || ent->model->needload)
		return false;

	if (!r_drawflame.value) // woods
		if (!strcmp(ent->model->name, "progs/flame.mdl") || !strcmp(ent->model->name, "progs/flame2.mdl"))
			return false;

	// Determine if entity should be treated as translucent for sorting purposes -- woods #alphasort
	// Original behavior: only alpha < 1 triggers alpha pass
	is_translucent = (ENTALPHA_DECODE(ent->alpha) < 1);

	// Extended checks only when r_alphasort is enabled
	if (r_alphasort.value && !is_translucent)
	{
		// Check for additive blend mode
		if (ent->effects & EF_ADDITIVE)
			is_translucent = true;

		// Check for alpha-textured sprites (TEXPREF_ALPHA flag on sprite texture)
		if (!is_translucent && ent->model->type == mod_sprite)
		{
			mspriteframe_t *pframe = R_GetSpriteFrame(ent);
			if (pframe && pframe->gltexture && (pframe->gltexture->flags & TEXPREF_ALPHA))
				is_translucent = true;
		}
	}

	// alpha/nonalpha split
	if (alphapass)
	{
		// Process only translucent entities
		if (!is_translucent)
			return false;
	}
	else
	{
		// Process only opaque entities
		if (is_translucent)
			return false;
	}

	return true;
}

/*
=============
R_CheckFlagSwap -- woods #alphasort
=============
*/
static void R_CheckFlagSwap(entity_t *ent)
{
	if (ent->model->type != mod_alias)
		return;

	if (swapflagprecache && map_ctf_flag_style == 2 && !strcmp(ent->model->name, "progs/flag.mdl")) // is there an alternate flag prechaced and worldspawn, if so lets swap it #alternateflags
	{
		if (ent->baseline.modelindex == ogflagprecache) // if the model is the flag, we're gonna swap it
		{
			ent->syncbase = 0;
			ent->effects |= EF_NOSHADOW;
			ent->lerpflags |= LERP_RESETANIM;
			ent->model = cl.model_precache[swapflagprecache]; // roque
		}
	}
	else if (swapflagprecache2 && map_ctf_flag_style == 3 && !strcmp(ent->model->name, "progs/flag.mdl")) // is there an alternate flag prechaced and worldspawn, if so lets swap it #alternateflags
	{
		if (ent->baseline.modelindex == ogflagprecache) // if the model is the flag, we're gonna swap it
		{
			ent->syncbase = 0;
			ent->effects |= EF_NOSHADOW;
			ent->lerpflags |= LERP_RESETANIM;
			ent->model = cl.model_precache[swapflagprecache2]; // alt1 (flag2.mdl)
		}
	}
	else if (swapflagprecache3 && map_ctf_flag_style == 4 && !strcmp(ent->model->name, "progs/flag.mdl")) // is there an alternate flag prechaced and worldspawn, if so lets swap it #alternateflags
	{
		if (ent->baseline.modelindex == ogflagprecache) // if the model is the flag, we're gonna swap it
		{
			ent->syncbase = 0;
			ent->effects |= EF_NOSHADOW;
			ent->lerpflags |= LERP_RESETANIM;
			ent->model = cl.model_precache[swapflagprecache3]; // alt2 (flag3.mdl)
		}
	}
}

/*
=============
R_DrawEntityModel -- woods #alphasort
Helper function to render an entity based on its model type.
Reduces code duplication between alpha and non-alpha rendering paths.
=============
*/
static void R_DrawEntityModel(entity_t *ent)
{
	switch (ent->model->type)
	{
	case mod_alias:
		R_CheckFlagSwap(ent);
		R_DrawAliasModel(ent);
		break;
	case mod_brush:
		R_DrawBrushModel(ent);
		break;
	case mod_sprite:
		R_DrawSpriteModel(ent);
		break;
	case mod_ext_invalid:
		break;
	}
}

/*
=============
R_CalculateEntityDistance -- woods #alphasort
Calculate distance from viewpoint to entity for depth sorting.
Brush/alias models use nearest point on AABB for accurate large-model sorting.
Sprites use origin for simplicity.
=============
*/
static float R_CalculateEntityDistance(entity_t *ent)
{
	if (ent->model->type == mod_brush || ent->model->type == mod_alias)
	{
		// Use nearest point on AABB to view origin, projected onto view direction
		// This gives accurate sorting for large models that cross the view plane
		vec3_t mins, maxs;
		float dist = 0.f;
		int j;

		R_GetEntityBounds(ent, mins, maxs);
		for (j = 0; j < 3; j++)
			dist += (CLAMP(mins[j], r_refdef.vieworg[j], maxs[j]) - r_refdef.vieworg[j]) * vpn[j];
		return dist;
	}
	else
	{
		// Simple origin-based distance for sprites and other types
		vec3_t delta;
		VectorSubtract(ent->origin, r_refdef.vieworg, delta);
		return DotProduct(delta, vpn);
	}
}

/*
=============
CompareAlphaEntities -- woods #alphasort
=============
*/
typedef struct
{
	entity_t *ent;
	float dist;
} sortable_entity_t;

static int CompareAlphaEntities(const void* a, const void* b)
{
	const sortable_entity_t* entA = (const sortable_entity_t*)a;
	const sortable_entity_t* entB = (const sortable_entity_t*)b;

	if (entA->dist > entB->dist) return -1;
	if (entA->dist < entB->dist) return 1;

	// Stable tie-breaker: use entity pointer address to avoid flickering -- woods #alphasort
	// Cast to uintptr_t to avoid UB when comparing pointers from different allocations
	if ((uintptr_t)entA->ent > (uintptr_t)entB->ent) return -1;
	if ((uintptr_t)entA->ent < (uintptr_t)entB->ent) return 1;
	return 0;
}

/*
=============
R_DrawEntitiesOnList -- woods #alphasort
Renders visible entities with optional depth sorting for alpha transparency.

Performance notes:
- Non-alpha pass: O(n) iteration
- Alpha pass with sorting: O(n) collection + O(n log n) sort + O(n) render
- Brush/alias use AABB distance, sprites use origin-based distance
- Renders back-to-front for correct alpha blending
=============
*/
void R_DrawEntitiesOnList (qboolean alphapass) //johnfitz -- added parameter
{
	int		i;
	static sortable_entity_t sorted_ents[MAX_EDICTS];
	static entity_t *brush_ents[MAX_EDICTS];
	int num_sorted = 0;
	int num_brush = 0;
	int count = cl_numvisedicts;

	//johnfitz -- optimized zero-check
	if (count == 0)
		return;

	if (!r_drawentities.value)
		return;

	//johnfitz -- sprites are not a special case
	
	// Alpha pass with depth sorting enabled
	if (alphapass && r_alphasort.value) // woods #alphasort
	{
		// Static array, no allocation needed
		if (count > MAX_EDICTS)
			count = MAX_EDICTS; // Safety clamp

		// Collect and calculate distances for all translucent entities
		for (i = 0; i < count; i++)
		{
			currententity = cl_visedicts[i];

			if (!R_ShouldDrawEntity(currententity, true))
				continue;

			// Store entity and distance for sorting
			sorted_ents[num_sorted].ent = currententity;
			sorted_ents[num_sorted].dist = R_CalculateEntityDistance(currententity);
			num_sorted++;
		}

		// Sort back-to-front (farthest first) for correct alpha blending
		qsort(sorted_ents, num_sorted, sizeof(sortable_entity_t), CompareAlphaEntities);

		// Render sorted entities
		for (i = 0; i < num_sorted; i++)
		{
			currententity = sorted_ents[i].ent;

			//johnfitz -- chasecam
			if (currententity == &cl.entities[cl.viewentity])
				currententity->angles[0] *= 0.3;
			//johnfitz

			R_DrawEntityModel(currententity);
		}
	}
	else if (!r_alphasort.value)
	{
		//johnfitz -- sprites are not a special case
		for (i = 0; i < cl_numvisedicts; i++)
		{
			currententity = cl_visedicts[i];

			//johnfitz -- if alphapass is true, draw only alpha entites this time
			//if alphapass is false, draw only nonalpha entities this time
			if ((ENTALPHA_DECODE(currententity->alpha) < 1 && !alphapass) ||
				(ENTALPHA_DECODE(currententity->alpha) == 1 && alphapass))
				continue;

			//johnfitz -- chasecam
			if (currententity == &cl.entities[cl.viewentity])
				currententity->angles[0] *= 0.3;
			//johnfitz

			//spike -- this would be more efficient elsewhere, but its more correct here.
			if (currententity->eflags & EFLAGS_EXTERIORMODEL)
				continue;
			if (!currententity->model || currententity->model->needload)
				continue;

			if (!r_drawflame.value) // woods
				if (!strcmp(currententity->model->name, "progs/flame.mdl") || !strcmp(currententity->model->name, "progs/flame2.mdl"))
					continue;

			if (!alphapass && currententity->model->type == mod_brush && num_brush < MAX_EDICTS)
			{
				brush_ents[num_brush++] = currententity;
				continue;
			}

			R_DrawEntityModel(currententity);
		}

		if (!alphapass && num_brush > 0)
			R_DrawBrushModelsInstanced(brush_ents, num_brush);
	}
	else
	{
		// Non-alpha pass when alphasort is enabled
		for (i = 0; i < cl_numvisedicts; i++)
		{
			currententity = cl_visedicts[i];

			if (!R_ShouldDrawEntity(currententity, alphapass))
				continue;

			//johnfitz -- chasecam
			if (currententity == &cl.entities[cl.viewentity])
				currententity->angles[0] *= 0.3; //johnfitz -- damp pitch
			//johnfitz

			if (!alphapass && currententity->model->type == mod_brush && num_brush < MAX_EDICTS)
			{
				brush_ents[num_brush++] = currententity;
				continue;
			}

			R_DrawEntityModel(currententity);
		}

		if (!alphapass && num_brush > 0)
			R_DrawBrushModelsInstanced(brush_ents, num_brush);
	}
}

/*
================
R_EmitWirePoint -- johnfitz -- draws a wireframe cross shape for point entities
================
*/
void R_EmitWirePoint (vec3_t origin, uint32_t color) // woods #iwshowbboxes, add color
{
	const int size = 8;

	// woods #iwshowbboxes
	float r = ((color >> 24) & 0xFF) / 255.0f;
	float g = ((color >> 16) & 0xFF) / 255.0f;
	float b = ((color >> 8) & 0xFF) / 255.0f;
	float a = (color & 0xFF) / 255.0f;
	glColor4f(r, g, b, a);

	glBegin (GL_LINES);
	glVertex3f (origin[0]-size, origin[1], origin[2]);
	glVertex3f (origin[0]+size, origin[1], origin[2]);
	glVertex3f (origin[0], origin[1]-size, origin[2]);
	glVertex3f (origin[0], origin[1]+size, origin[2]);
	glVertex3f (origin[0], origin[1], origin[2]-size);
	glVertex3f (origin[0], origin[1], origin[2]+size);
	glEnd ();
}

/*
================
R_EmitWireBox -- johnfitz -- draws one axis aligned bounding box
================
*/
void R_EmitWireBox (vec3_t mins, vec3_t maxs, uint32_t color) // woods #iwshowbboxes, add color
{
	// woods #iwshowbboxes
	float r = ((color >> 24) & 0xFF) / 255.0f;
	float g = ((color >> 16) & 0xFF) / 255.0f;
	float b = ((color >> 8) & 0xFF) / 255.0f;
	float a = (color & 0xFF) / 255.0f;
	glColor4f(r, g, b, a);
	
	glBegin (GL_QUAD_STRIP);
	glVertex3f (mins[0], mins[1], mins[2]);
	glVertex3f (mins[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], mins[1], mins[2]);
	glVertex3f (maxs[0], mins[1], maxs[2]);
	glVertex3f (maxs[0], maxs[1], mins[2]);
	glVertex3f (maxs[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], maxs[1], mins[2]);
	glVertex3f (mins[0], maxs[1], maxs[2]);
	glVertex3f (mins[0], mins[1], mins[2]);
	glVertex3f (mins[0], mins[1], maxs[2]);
	glEnd ();
}

void R_EmitFullSphere (vec3_t origin, float radius, uint32_t color, int segments) // woods #locext
{
	float r = ((color >> 24) & 0xFF) / 255.0f;
	float g = ((color >> 16) & 0xFF) / 255.0f;
	float b = ((color >> 8) & 0xFF) / 255.0f;
	float a = (color & 0xFF) / 255.0f;

	glPushMatrix();
	glTranslatef(origin[0], origin[1], origin[2]);

	glColor4f(r, g, b, a);

	for (int i = 0; i < segments; i++)
	{
		float lat0 = M_PI * (-0.5 + (float)(i) / segments);
		float z0 = sin(lat0);
		float zr0 = cos(lat0);

		float lat1 = M_PI * (-0.5 + (float)(i + 1) / segments);
		float z1 = sin(lat1);
		float zr1 = cos(lat1);

		glBegin(GL_TRIANGLE_STRIP);
		for (int j = 0; j <= segments; j++)
		{
			float lng = 2 * M_PI * (float)(j) / segments;
			float x = cos(lng);
			float y = sin(lng);

			glVertex3f(x * zr0 * radius, y * zr0 * radius, z0 * radius);
			glVertex3f(x * zr1 * radius, y * zr1 * radius, z1 * radius);
		}
		glEnd();
	}

	glPopMatrix();
}

void R_EmitPin (vec3_t origin, float radius, uint32_t color, int segments) // woods #locext
{
	float r = ((color >> 24) & 0xFF) / 255.0f;
	float g = ((color >> 16) & 0xFF) / 255.0f;
	float b = ((color >> 8) & 0xFF) / 255.0f;
	float a = (color & 0xFF) / 255.0f;

	// Adjust the cone height to make it proportionate to the radius
	float coneHeight = radius * 2.0f;
	float zOffset = coneHeight / 2.0f - 0.1f;

	glPushMatrix();
	glTranslatef(origin[0], origin[1], origin[2]);
	glColor4f(r, g, b, a);

	// Draw the top hemisphere
	for (int i = 0; i <= segments / 2; i++)
	{
		float lat0 = M_PI * (float)(i) / segments;
		float z0 = sin(lat0);
		float zr0 = cos(lat0);
		float lat1 = M_PI * (float)(i + 1) / segments;
		float z1 = sin(lat1);
		float zr1 = cos(lat1);

		glBegin(GL_TRIANGLE_STRIP);
		for (int j = 0; j <= segments; j++)
		{
			float lng = 2 * M_PI * (float)(j) / segments;
			float x = cos(lng);
			float y = sin(lng);

			glVertex3f(x * zr0 * radius, y * zr0 * radius, z0 * radius - zOffset);
			glVertex3f(x * zr1 * radius, y * zr1 * radius, z1 * radius - zOffset);
		}
		glEnd();
	}

	// Draw the cone part (extending down from the bottom of the hemisphere)
	glBegin(GL_TRIANGLE_FAN);
	// Draw the tip of the cone
	glVertex3f(0.0f, 0.0f, -radius - coneHeight);  // Cone tip

	for (int j = 0; j <= segments*2; j++)
	{
		float lng = 2 * M_PI * (float)(j) / segments/2;
		float x = cos(lng);
		float y = sin(lng);

		// Base of the cone at the adjusted bottom of the hemisphere
		glVertex3f(x * radius, y * radius, -radius);
	}
	glEnd();

	glPopMatrix();
}

/*
================
R_ShowBoundingBoxesFilter -- woods #iwshowbboxes

r_showbboxes_filter artifact =trigger_secret
================
*/
char r_showbboxes_filter_strings[MAXCMDLINE];

static qboolean R_ShowBoundingBoxesFilter(edict_t* ed)
{
	if (!r_showbboxes_filter_strings[0])
		return true;

	if (ed->v.classname)
	{
		const char* classname = PR_GetString(ed->v.classname);
		const char* str = r_showbboxes_filter_strings;
		qboolean is_allowed = false;
		while (*str && !is_allowed)
		{
			if (*str == '=')
				is_allowed = !strcmp(classname, str + 1);
			else
				is_allowed = strstr(classname, str) != NULL;
			str += strlen(str) + 1;
		}
		return is_allowed;
	}
	return false;
}

static qboolean R_DebugIsPointEntity(edict_t *ed)
{
	return VectorCompare(ed->v.mins, ed->v.maxs);
}

static void R_GetEdictDebugBounds(edict_t *ed, vec3_t mins, vec3_t maxs)
{
	if ((ed->v.solid == SOLID_BSP || ed->v.solid == SOLID_EXT_BSPTRIGGER)
		&& (ed->v.angles[0] || ed->v.angles[1] || ed->v.angles[2])
		&& pr_checkextension.value)
	{
		VectorCopy(ed->v.absmin, mins);
		VectorCopy(ed->v.absmax, maxs);
		return;
	}

	if (R_DebugIsPointEntity(ed))
	{
		mins[0] = ed->v.origin[0] - 8.0f;
		mins[1] = ed->v.origin[1] - 8.0f;
		mins[2] = ed->v.origin[2] - 8.0f;
		maxs[0] = ed->v.origin[0] + 8.0f;
		maxs[1] = ed->v.origin[1] + 8.0f;
		maxs[2] = ed->v.origin[2] + 8.0f;
		return;
	}

	VectorAdd(ed->v.mins, ed->v.origin, mins);
	VectorAdd(ed->v.maxs, ed->v.origin, maxs);
}

static qboolean R_ShouldShowDebugEdict(edict_t *ed, edict_t *sv_player, byte *pvs, vec3_t mins, vec3_t maxs)
{
	if (ed == sv_player || ed->free)
		return false;

	if (!R_ShowBoundingBoxesFilter(ed))
		return false;

	R_GetEdictDebugBounds(ed, mins, maxs);
	if (R_CullBox(mins, maxs))
		return false;

	if (pvs)
	{
		qboolean inpvs =
			ed->num_leafs ?
			SV_EdictInPVS(ed, pvs) :
			SV_BoxInPVS(mins, maxs, pvs, qcvm->worldmodel->nodes);
		if (!inpvs)
			return false;
	}

	return true;
}

/*
================
R_ShowBoundingBoxes -- johnfitz

draw bounding boxes -- the server-side boxes, not the renderer cullboxes
================
*/
void R_ShowBoundingBoxes (void)
{
	extern		edict_t *sv_player;
	byte		*pvs; // woods #iwshowbboxes
	vec3_t		mins,maxs;
	vec3_t		raydelta;
	edict_t		*ed;
	int			i, mode; // woods #iwshowbboxes
	uint32_t	color; // woods #iwshowbboxes
	qcvm_t 		*oldvm;	//in case we ever draw a scene from within csqc.
	float		dist, bestdist;

	bbox_focus = NULL;

	mode = abs((int)r_showbboxes.value); // woods #iwshowbboxes
	if ((!mode && !r_showfields.value) || cl.maxclients > 1 || !r_drawentities.value || !sv.active || !sv_player)
		return;

	glDisable (GL_DEPTH_TEST);
	glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset (OFFSET_SHOWTRIS);
	glDisable (GL_TEXTURE_2D);
	glDisable (GL_CULL_FACE);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	oldvm = qcvm;
	PR_SwitchQCVM(NULL);
	PR_SwitchQCVM(&sv.qcvm);

	if (mode >= 2 || mode == 0)
	{
		vec3_t org;
		VectorAdd(sv_player->v.origin, sv_player->v.view_ofs, org);
		pvs = SV_FatPVS(org, qcvm->worldmodel);
	}
	else
		pvs = NULL;

	VectorScale(vpn, gl_farclip.value, raydelta);
	bestdist = FLT_MAX;

	for (i=1, ed=NEXT_EDICT(qcvm->edicts) ; i<qcvm->num_edicts ; i++, ed=NEXT_EDICT(ed))
	{
		if (!R_ShouldShowDebugEdict(ed, sv_player, pvs, mins, maxs))
			continue;

		if (RayVsBox(r_origin, raydelta, mins, maxs, &dist) && dist > 0.0f && dist < bestdist)
		{
			bestdist = dist;
			bbox_focus = ed;
		}
	}

	for (i=1, ed=NEXT_EDICT(qcvm->edicts) ; i<qcvm->num_edicts ; i++, ed=NEXT_EDICT(ed))
	{
		if (!R_ShouldShowDebugEdict(ed, sv_player, pvs, mins, maxs))
			continue;

		if (ed == bbox_focus)
		{
			color = 0xffffffff;
		}
		else if (r_showbboxes.value > 0.f) // woods #iwshowbboxes
		{
			int modelindex = (int)ed->v.modelindex;
			color = 0x7f800080;
			if (modelindex >= 0 && modelindex < MAX_MODELS && sv.models[modelindex])
			{
				switch (sv.models[modelindex]->type)
				{
				case mod_brush:  color = 0x7fff8080; break;
				case mod_alias:  color = 0x7f408080; break;
				case mod_sprite: color = 0x7f4040ff; break;
				default:
					break;
				}
			}
			if (ed->v.health > 0)
				color = 0x7f0000ff;
		}
		else if (r_showbboxes.value < 0.f)
		{
			color = 0x7fffffff;
		}
		else
		{
			color = 0x5f7f7f7f;
		}

		if (R_DebugIsPointEntity(ed))
		{
			R_EmitWirePoint (ed->v.origin, color); // woods #iwshowbboxes
		}
		else
		{
			R_EmitWireBox (mins, maxs, color); // woods #iwshowbboxes
		}
	}
	PR_SwitchQCVM(NULL);
	PR_SwitchQCVM(oldvm);

	glDisable (GL_BLEND);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_CULL_FACE);
	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset (OFFSET_NONE);
	glEnable (GL_DEPTH_TEST);

	Sbar_Changed (); //so we don't get dots collecting on the statusbar
}

/*
================
R_ShowTris -- johnfitz
================
*/
void R_ShowTris (void)
{
	extern cvar_t r_particles;
	int i;

	if (r_showtris.value < 1 || r_showtris.value > 2 || cl.maxclients > 1)
		return;

	if (r_showtris.value == 1)
		glDisable (GL_DEPTH_TEST);
	glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset (OFFSET_SHOWTRIS);
	glDisable (GL_TEXTURE_2D);
	glColor3f (1,1,1);
//	glEnable (GL_BLEND);
//	glBlendFunc (GL_ONE, GL_ONE);

	if (r_drawworld.value)
	{
		R_DrawWorld_ShowTris ();
	}

	if (r_drawentities.value)
	{
		for (i=0 ; i<cl_numvisedicts ; i++)
		{
			currententity = cl_visedicts[i];

			if (currententity == &cl.entities[cl.viewentity]) // chasecam
				currententity->angles[0] *= 0.3;

			switch (currententity->model->type)
			{
			case mod_brush:
				R_DrawBrushModel_ShowTris (currententity);
				break;
			case mod_alias:
				R_DrawAliasModel_ShowTris (currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel (currententity);
				break;
			default:
				break;
			}
		}

		// viewmodel
		currententity = &cl.viewent;
		if (r_drawviewmodel.value
			&& !chase_active.value
			&& cl.stats[STAT_HEALTH] > 0
			&& !(cl.items & IT_INVISIBILITY)
			&& currententity->model
			&& currententity->model->type == mod_alias)
		{
			glDepthRange (0, 0.3);
			R_DrawAliasModel_ShowTris (currententity);
			glDepthRange (0, 1);
		}
	}

	if (r_particles.value)
	{
		R_DrawParticles_ShowTris ();
	}

//	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//	glDisable (GL_BLEND);
	glColor3f (1,1,1);
	glEnable (GL_TEXTURE_2D);
	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset (OFFSET_NONE);
	if (r_showtris.value == 1)
		glEnable (GL_DEPTH_TEST);

	Sbar_Changed (); //so we don't get dots collecting on the statusbar
}

/*
================
 tool_texturepointer -- woods -- fitzquake markv r15 (baker) #texturepointer
================
*/

static vec3_t collision_spot;
qboolean texturepointer_on;


typedef struct
{
	char			texturename[16]; // WAD sizeof name is 16, so maxlength of a texture is 15.
	gltexture_t* glt;
	const char* explicit_name;
	const char* short_name;
	int				width;
	int				height;
	entity_t* ent;
	msurface_t* surf;
	float			distance;
} texturepointer_t;

static texturepointer_t texturepointer;

void TexturePointer_Reset (void)
{
	memset(&texturepointer, 0, sizeof(texturepointer_t));
}

static void Texture_Pointer_f (void)
{
	switch (Cmd_Argc())
	{
	case 2:
		texturepointer_on = !!Q_atoi(Cmd_Argv(1));
		break;
	case 1:
		texturepointer_on = !texturepointer_on;
		break;
	}

	TexturePointer_Reset();
	Con_Printf("texture pointer is %s\n", texturepointer_on ? "^mON" : "^mOFF");
}

void TexturePointer_Init (void)
{
	Cmd_AddCommand("tool_texturepointer", Texture_Pointer_f);
}

static qboolean TexturePointer_Active (void)
{
	return texturepointer_on && cls.signon == SIGNONS && cl.worldmodel && texturepointer.surf;
}

static void TexturePointer_CopySound (void)
{
	const char *sound_file = COM_FileExists("sound/qssm/copy.wav", NULL) ? "qssm/copy.wav" : "player/tornoff2.wav";

	S_LocalSound(sound_file);
}

static void TexturePointer_CopyName (char *out, size_t outsize)
{
	const char *name = texturepointer.short_name;

	if (!name || !name[0])
		name = texturepointer.explicit_name;
	if (!name || !name[0])
		name = texturepointer.texturename;

	name = COM_SkipPath(COM_SkipColon(name));
	COM_StripExtension(name, out, outsize);
	if (!out[0])
		q_strlcpy(out, texturepointer.texturename, outsize);
}

#if defined(_WIN32) || defined(__APPLE__)
static void TexturePointer_FlipImage (byte *buffer, int width, int height)
{
	int		rowbytes = width * 4;
	byte	*temp;
	int		i;

	temp = (byte *) malloc(rowbytes);
	if (!temp)
		return;

	for (i = 0; i < height / 2; i++)
	{
		byte *top = buffer + i * rowbytes;
		byte *bottom = buffer + (height - 1 - i) * rowbytes;

		memcpy(temp, top, rowbytes);
		memcpy(top, bottom, rowbytes);
		memcpy(bottom, temp, rowbytes);
	}

	free(temp);
}

static qboolean TexturePointer_CopyImage (const char *copyname)
{
	gltexture_t	*glt = texturepointer.glt;
	byte		*buffer;
	size_t		buffersize;

	if (!glt || !glt->width || !glt->height)
	{
		Con_Printf("no texture image to copy\n");
		return true;
	}

	if (glt->width > (unsigned int)INT_MAX || glt->height > (unsigned int)INT_MAX ||
		(size_t)glt->width > (SIZE_MAX / (size_t)glt->height) / 4)
	{
		Con_Printf("texture image is too large to copy\n");
		return true;
	}

	buffersize = (size_t)glt->width * (size_t)glt->height * 4;
	if (buffersize > (size_t)INT_MAX)
	{
		Con_Printf("texture image is too large to copy\n");
		return true;
	}

	buffer = (byte *) malloc(buffersize);
	if (!buffer)
	{
		Con_Printf("texture image copy failed: out of memory\n");
		return true;
	}

	GL_Bind(glt);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, buffer);
	TexturePointer_FlipImage(buffer, (int)glt->width, (int)glt->height);
	Sys_Image_BGRA_To_Clipboard(buffer, (int)glt->width, (int)glt->height, (int)buffersize);
	free(buffer);

	TexturePointer_CopySound();
	Con_Printf("copied texture image: ^m%s^m (%ux%u)\n", copyname, glt->width, glt->height);
	return true;
}
#endif

qboolean TexturePointer_Copy (qboolean copy_image)
{
	char copyname[MAX_QPATH];

	if (!TexturePointer_Active())
		return false;

	TexturePointer_CopyName(copyname, sizeof(copyname));

	if (copy_image)
	{
#if defined(_WIN32) || defined(__APPLE__)
		return TexturePointer_CopyImage(copyname);
#else
		Con_Printf("texture image clipboard is not available in this build\n");
		return true;
#endif
	}

	if (SDL_SetClipboardText(copyname) < 0)
	{
		Con_Printf("Clipboard copy failed: %s\n", SDL_GetError());
		return true;
	}

	TexturePointer_CopySound();
	Con_Printf("copied texture name: ^m%s^m\n", copyname);
	return true;
}

void TexturePointer_CheckChange (texturepointer_t* test)
{	
	// This next IF checks if there is a surface and if the name is different than before ...
 	if (test->surf && strcmp(test->surf->texinfo->texture->name, texturepointer.texturename))
	{
		// Change of texture
//		Con_Printf ("Texture changed from %s to %s\n", texturepointer.texturename, test->surf->texinfo->texture->name);
		q_strlcpy(texturepointer.texturename, test->surf->texinfo->texture->name, 16 /* WAD sizeof name */);
		texturepointer.glt = test->surf->texinfo->texture->gltexture;

		
		// Is water or lava, redirect to that glt
		//if (!texturepointer.glt && test->surf->texinfo->texture->warpimage)
		//	texturepointer.glt = test->surf->texinfo->texture->warpimage;

		/// Probably sky ...
		if (!texturepointer.glt)
		{
			texturepointer.explicit_name = texturepointer.texturename; //texturepointer.surf->texinfo->texture->name;
			texturepointer.short_name = texturepointer.texturename;
		//	texturepointer.width = texturepointer.surf->texinfo->texture->width;
		//	texturepointer.height = texturepointer.surf->texinfo->texture->height;

		}
		else
		{
			texturepointer.explicit_name = texturepointer.glt->name;
			texturepointer.short_name = COM_SkipColon(texturepointer.explicit_name);
		//	texturepointer.width = texturepointer.glt->source_width;
		//	texturepointer.height = texturepointer.glt->source_height;
		}

	}
	texturepointer.surf = test->surf;
	texturepointer.ent = test->ent;
}

msurface_t* SurfacePoint_NodeCheck_Recursive (mnode_t* node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	vec3_t		mid;
	msurface_t* surf = NULL;

	// RecursiveLightPoint wouldn't exit here, btw.  We do
	// Baker: investigate in future why this can happen ...
	if (!node)
		return NULL; // I think it is because we pass brush models to it
					  // Or maybe because we pass sky and water too?

loc0:
	// didn't hit anything (CONTENTS_EMPTY or CONTENTS_WATER, etc.)
	// Baker: special contents ... I'm not sure this should be a fail here except if contents empty
	// Like do: node->contents == CONTENTS_EMPTY or  CONTENTS_SOLID return;
	// However, seems to work perfect!
	if (node->contents < 0)
		return NULL;		// didn't hit anything

// calculate mid point
	if (node->plane->type < 3)
	{
		front = start[node->plane->type] - node->plane->dist;
		back = end[node->plane->type] - node->plane->dist;
	}
	else
	{
		front = DotProduct(start, node->plane->normal) - node->plane->dist;
		back = DotProduct(end, node->plane->normal) - node->plane->dist;
	}

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
	{
		node = node->children[front < 0];
		goto loc0;
	}

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	// go down front side
	surf = SurfacePoint_NodeCheck_Recursive(node->children[front < 0], start, mid);
	if (surf)
	{
		return surf; // hit something
	}
	else
	{
		// Didn't hit anything so ...

		unsigned int		i;
		surf = cl.worldmodel->surfaces + node->firstsurface;

		// check for impact on this node
		// Baker: Apparently we need this if the for loop below fails
		VectorCopy(mid, collision_spot);

		for (i = 0;i < node->numsurfaces;i++, surf++)
		{
			// light would check if SURF_DRAWTILED (no lightmaps), but we want for texture pointer
			//if (surf->flags & SURF_DRAWTILED)
			//	continue; // no lightmaps

			double dsfrac, dtfrac;

			dsfrac = DoublePrecisionDotProduct(mid, surf->lmvecs[0]) + surf->lmvecs[0][3];
			dtfrac = DoublePrecisionDotProduct(mid, surf->lmvecs[1]) + surf->lmvecs[1][3];
			if (dsfrac < 0 || dtfrac < 0)
				continue;

			if (dsfrac > surf->extents[0] || dtfrac > surf->extents[1])
				continue;

			// At this point we have a collision with this surface.
			// Set return variables
			VectorCopy(mid, collision_spot);
			return surf; // success
		}

		// go down back side
		return SurfacePoint_NodeCheck_Recursive(node->children[front >= 0], mid, end);
	}
}

static texturepointer_t SurfacePoint (vec3_t startpoint, vec3_t endpoint)
{
	float collision_distance;
	texturepointer_t best = { 0 };
	int			i;

	msurface_t* collision_surf = SurfacePoint_NodeCheck_Recursive (cl.worldmodel->nodes, startpoint, endpoint);

	if (collision_surf)
	{
		collision_distance = DistanceBetween2Points (startpoint, collision_spot);

		best.ent = NULL;
		best.surf = collision_surf;
		best.distance = collision_distance;
	}

	// Now check for hit with world submodels
	for (i = 0; i < cl_numvisedicts; i++)	// 0 is player.
	{
		// Note that this ONLY collides with visible entities!
		entity_t* pe = cl_visedicts[i];
		vec3_t		adjusted_startpoint, adjusted_endpoint, adjusted_net;

		if (!pe->model)
			continue;   // no model for ent

		if (!(pe->model->surfaces == cl.worldmodel->surfaces))
			continue;	// model isnt part of world (i.e. no health boxes or what not ...)

		// Baker: We need to adjust the point locations for entity origin

		VectorSubtract(startpoint, pe->origin, adjusted_startpoint);
		VectorSubtract(endpoint, pe->origin, adjusted_endpoint);
		VectorSubtract(startpoint, adjusted_startpoint, adjusted_net);

		// Make further adjustments if entity is rotated
		if (pe->angles[0] || pe->angles[1] || pe->angles[2])
		{
			vec3_t f, r, u, temp;
			AngleVectors(pe->angles, f, r, u);	// split entity angles to forward, right, up

			VectorCopy(adjusted_startpoint, temp);
			adjusted_startpoint[0] = DotProduct(temp, f);
			adjusted_startpoint[1] = -DotProduct(temp, r);
			adjusted_startpoint[2] = DotProduct(temp, u);

			VectorCopy(adjusted_endpoint, temp);
			adjusted_endpoint[0] = DotProduct(temp, f);
			adjusted_endpoint[1] = -DotProduct(temp, r);
			adjusted_endpoint[2] = DotProduct(temp, u);
		}

		collision_surf = SurfacePoint_NodeCheck_Recursive(pe->model->nodes + pe->model->hulls[0].firstclipnode /*pe->model->nodes*/, adjusted_startpoint, adjusted_endpoint);

		if (collision_surf)
		{
			// Baker: We have to add the origin back into the results here!
			VectorAdd(collision_spot, adjusted_net, collision_spot);

			collision_distance = DistanceBetween2Points(startpoint, collision_spot);

			if (!best.surf || collision_distance < best.distance)
			{
				// New best
				best.ent = pe;
				best.surf = collision_surf;
				best.distance = collision_distance;
			}

		}
		// On to next entity ..
	}

	return best;
}

// Determine start and end test and run function to get closest collision surface.
texturepointer_t TexturePointer_SurfacePoint (void)
{
	vec3_t startingpoint, endingpoint, forward, up, right;

	// r_refdef.vieworg/viewangles is the camera position
	VectorCopy(r_refdef.vieworg, startingpoint);

	// Obtain the forward vector
	AngleVectors(r_refdef.viewangles, forward, right, up);

	// Walk it forward by 4096 units
	VectorMA(startingpoint, 4096, forward, endingpoint);

	// There is no assurance anything will be hit (i.e. noclip outside map looking at void)
	return SurfacePoint(startingpoint, endingpoint);
}

extern qboolean	qeintermission; // woods

void TexturePointer_Draw (void)
{
	if (cl.intermission || qeintermission || scr_viewsize.value >= 130)
		return;

	if (texturepointer_on && cls.signon == SIGNONS && cl.worldmodel && texturepointer.surf)
	{
		//const char* drawstring1 = va("\bTexture:\b %s", texturepointer.short_name);
		//const char* drawstring2 = va("\b  %i x %i px", texturepointer.width, texturepointer.height);

		GL_SetCanvas(CANVAS_CROSSHAIR2);

		char texturename[MAX_OSPATH];

		if (strstr(texturepointer.short_name, "textures/"))
			q_snprintf(texturename, sizeof(texturename), "external: %s", texturepointer.short_name);
		else
			q_snprintf(texturename, sizeof(texturename), "%s", texturepointer.short_name);

		Draw_String(0 - (strlen(texturename) * 4), 20, texturename);
	}
}

Point3D R_EmitSurfaceHighlight (entity_t* enty, msurface_t* surf, vec4_t color, int style)
{
	Point3D center;
	float* verts = surf->polys->verts[0];

	vec3_t mins = { 99999,  99999,  99999 };
	vec3_t maxs = { -99999, -99999, -99999 };
	int i;

	if (enty)
	{
		glPushMatrix();
		R_RotateForEntity(enty->origin, enty->angles, enty);
	}

	if (style == OUTLINED_POLYGON)	// Set to lines
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_CULL_FACE);
	glColor4f(color[0], color[1], color[2], color[3]);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);

	glBegin(GL_POLYGON);

	// Draw polygon while collecting information for the center.
	for (i = 0; i < surf->polys->numverts; i++, verts += VERTEXSIZE)
	{
		VectorExtendLimits(verts, mins, maxs);
		glVertex3fv(verts);
	}
	glEnd();

	glEnable(GL_TEXTURE_2D);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glEnable(GL_CULL_FACE);
	glColor4f(1, 1, 1, 1);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	if (style == OUTLINED_POLYGON)	// Set to lines
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	if (enty)
		glPopMatrix();

	// Calculate the center
	VectorAverage(mins, maxs, center.vec3);

	return center;
}

void TexturePointer_Think (void)
{
	texturepointer_t test;

	if (!texturepointer_on || !cl.worldmodel || cls.signon < SIGNONS)
		return;

	if (cl.intermission || qeintermission || scr_viewsize.value >= 130)
		return;

	test = TexturePointer_SurfacePoint();

	if (test.surf)
	{
		//const vec4_t linecolor = {1,1,1,1};
		vec4_t color = { 1, 0, 0, sin(realtime * 3) * 0.125f + 0.25 };
 		TexturePointer_CheckChange(&test);

		R_EmitSurfaceHighlight (texturepointer.ent, texturepointer.surf, color, FILLED_POLYGON);
	}
}

/*
================
R_DrawShadows
================
*/
void R_DrawShadows (void)
{
	int i;
	qboolean use_viewmodel_stencil = false;

	if (!r_shadows.value || !r_drawentities.value || r_drawflat_cheatsafe || r_lightmap_cheatsafe)
		return;

	// Use stencil buffer to prevent self-intersecting shadows, from Baker (MarkV)
	if (gl_stencilbits)
	{
		GLuint stencil_mask = (gl_stencilbits > 0 && gl_stencilbits < 32) ? ((1u << gl_stencilbits) - 1u) : 0xFFu;
		GLuint viewmodel_stencil_bit = GL_VIEWMODEL_STENCIL_BIT();

		// Exclude viewmodel bit from shadow stencil writes when laser is active
		if (viewmodel_stencil_bit && gl_laserpoint.value)
		{
			use_viewmodel_stencil = true;
			stencil_mask &= ~viewmodel_stencil_bit;
		}

		// woods #powershell -- force a full write-mask so glClear actually wipes every stencil
		// bit (it honors the mask); otherwise a partial mask left by an earlier pass turns this
		// clear into a no-op and shadow volumes build on a dirty buffer. Then narrow to the
		// shadow mask for the INCR writes below.
		glStencilMask(~0u);
		glClear(GL_STENCIL_BUFFER_BIT);
		glStencilMask(stencil_mask);
		glStencilFunc(GL_EQUAL, 0, ~0);
		glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
		glEnable(GL_STENCIL_TEST);
	}

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		if (!currententity->model) // woods
			continue;

		switch (currententity->model->type) // woods #shadow
		{
		case mod_alias:
			GL_DrawAliasShadow(currententity);
			break;

		case mod_brush:
			GL_DrawBrushShadow(currententity);
			break;

		default:
			continue;
		}
	}

	if (gl_stencilbits)
	{
		if (use_viewmodel_stencil)
			glStencilMask(~0u);
		glDisable(GL_STENCIL_TEST);
	}
}

/*
================
Item Tracers -- quakespasm-shalrathy #tracers
================
*/

int doShowTracer(int value, int distsquared) {
	if (value == 0) return 0;
	else if (value == 1) return 1;
	else return distsquared <= value * value;
}

void GetEdictCenter(edict_t* ed, vec3_t pos) {
	float* mins = GetEdictFieldValue(ed, ED_FindFieldOffset("mins"))->vector;
	float* size = GetEdictFieldValue(ed, ED_FindFieldOffset("size"))->vector;
	pos[0] = mins[0] + size[0] / 2;
	pos[1] = mins[1] + size[1] / 2;
	pos[2] = mins[2] + size[2] / 2;
	pos[0] += ed->v.origin[0];
	pos[1] += ed->v.origin[1];
	pos[2] += ed->v.origin[2];
}

void R_DrawTracers(void)
{
	if (!trace_any.value) {
		return;
	}

	glDisable(GL_DEPTH_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset(OFFSET_SHOWTRIS);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);

	vec3_t forward, right, up;
	AngleVectors(r_refdef.viewangles, forward, right, up);
	float org[3];
	org[0] = cl.entities[cl.viewentity].origin[0];
	org[1] = cl.entities[cl.viewentity].origin[1];
	org[2] = cl.entities[cl.viewentity].origin[2];

	org[0] += forward[0] * 100;
	org[1] += forward[1] * 100;
	org[2] += forward[2] * 100;

	int i;
	edict_t* ed;

	qcvm_t* oldvm;	//in case we ever draw a scene from within csqc.
	oldvm = qcvm;
	PR_SwitchQCVM(NULL);
	PR_SwitchQCVM(&sv.qcvm);
	for (i = 0, ed = NEXT_EDICT(qcvm->edicts); i < qcvm->num_edicts; i++, ed = NEXT_EDICT(ed))
	{
		if (ed->free) continue;

		float pos[3];
		GetEdictCenter(ed, pos);

		float distsquared = (org[0] - pos[0]) * (org[0] - pos[0])
			+ (org[1] - pos[1]) * (org[1] - pos[1])
			+ (org[2] - pos[2]) * (org[2] - pos[2]);

		const char* classname = PR_GetString(ed->v.classname);
		char do_trace = 0;
	
		if (q_strcasestr(classname, trace_any_contains.string)) {
			if (doShowTracer(trace_any.value > 0, distsquared)) {
				do_trace = 1;
				glEnable(GL_BLEND);
				glColor4f(1, 1, 1, trace_any.value);
			}
		}

		if (do_trace) {
			glBegin(GL_LINES);
			glVertex3f(pos[0], pos[1], pos[2]);
			glVertex3f(org[0], org[1], org[2]);
			glEnd();
		}
	}
	PR_SwitchQCVM(NULL);
	PR_SwitchQCVM(oldvm);

	glColor4f(1, 1, 1, 1);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset(OFFSET_NONE);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
}

enum
{
	LIGHTNING_ALPHA_SLOT_BOLT2 = 0,
	LIGHTNING_ALPHA_SLOT_BOLT1,
	LIGHTNING_ALPHA_SLOT_BOLT3,
	LIGHTNING_ALPHA_SLOT_COUNT
};

static float r_lightning_alphas[LIGHTNING_ALPHA_SLOT_COUNT] = { 1.0f, 1.0f, 1.0f };
static char r_lightning_alpha_cached_string[128] = { 0 };

static int R_LightningAlphaSlotForModel(const qmodel_t *model)
{
	if (!model || !model->name[0])
		return LIGHTNING_ALPHA_SLOT_BOLT2;

	if (!strcmp(model->name, "progs/bolt.mdl"))
		return LIGHTNING_ALPHA_SLOT_BOLT1;
	if (!strcmp(model->name, "progs/bolt3.mdl"))
		return LIGHTNING_ALPHA_SLOT_BOLT3;

	return LIGHTNING_ALPHA_SLOT_BOLT2;
}

static void R_UpdateLightningAlphas(void)
{
	const char *value = gl_lightning_alpha.string ? gl_lightning_alpha.string : "";

	if (!strcmp(value, r_lightning_alpha_cached_string))
		return;

	Q_strncpy(r_lightning_alpha_cached_string, value, sizeof(r_lightning_alpha_cached_string) - 1);
	r_lightning_alpha_cached_string[sizeof(r_lightning_alpha_cached_string) - 1] = '\0';

	float parsed[LIGHTNING_ALPHA_SLOT_COUNT] = { 1.0f, 1.0f, 1.0f };
	sscanf(r_lightning_alpha_cached_string, "%f %f %f",
		&parsed[LIGHTNING_ALPHA_SLOT_BOLT2],
		&parsed[LIGHTNING_ALPHA_SLOT_BOLT1],
		&parsed[LIGHTNING_ALPHA_SLOT_BOLT3]);

	for (int i = 0; i < LIGHTNING_ALPHA_SLOT_COUNT; ++i)
		r_lightning_alphas[i] = CLAMP(0.0f, parsed[i], 1.0f);
}

float R_LightningAlphaForModel(const qmodel_t *model)
{
	R_UpdateLightningAlphas();
	return r_lightning_alphas[R_LightningAlphaSlotForModel(model)];
}

/*
=============
R_LightningBeam_DeleteTexture // woods #beamspoly
=============
*/
void R_LightningBeam_DeleteTexture (void)
{
	r_lightningbeam_texture = NULL;
}

static gltexture_t *R_LightningBeam_BuiltinTexture(void)
{
        if (r_lightningbeam_texture)
                return r_lightningbeam_texture;

        byte data[64 * 64 * 4];
        for (int y = 0; y < 64; y++)
        {
                float vf = (float)y / 63.0f;
                float distance = (vf - 0.5f) * 2.0f;
                float falloff = expf(-distance * distance * 4.0f);

                for (int x = 0; x < 64; x++)
                {
                        float uf = (float)x / 63.0f;
                        float wave = 0.5f + 0.5f * sinf((uf * 6.2831853f) + cosf(vf * 9.4247779f));
                        float intensity = CLAMP(0.0f, falloff * (0.6f + 0.4f * wave), 1.0f);

			float brightness = CLAMP(0.0f, 0.5f + intensity * 0.5f, 1.0f);
			float r = brightness;
			float g = brightness;
			float b = brightness;
                        float a = intensity;

                        int idx = (y * 64 + x) * 4;
                        data[idx + 0] = (byte)(r * 255.0f);
                        data[idx + 1] = (byte)(g * 255.0f);
                        data[idx + 2] = (byte)(b * 255.0f);
                        data[idx + 3] = (byte)(a * 255.0f);
                }
        }

        r_lightningbeam_texture = TexMgr_LoadImage (NULL, "lightning_beam_builtin", 64, 64, SRC_RGBA, data, "", (src_offset_t)data, TEXPREF_PERSIST | TEXPREF_ALPHA | TEXPREF_LINEAR);
        return r_lightningbeam_texture;
}

static void R_Beam_DrawQuad(const vec3_t start, const vec3_t end, const vec3_t offset, float sStart, float sEnd)
{
        float base = floorf(sStart);
        float s0 = sStart - base;
        float s1 = sEnd - base;
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(s0, 0.0f);
        glVertex3f(start[0] + offset[0], start[1] + offset[1], start[2] + offset[2]);
        glTexCoord2f(s0, 1.0f);
        glVertex3f(start[0] - offset[0], start[1] - offset[1], start[2] - offset[2]);
        glTexCoord2f(s1, 0.0f);
        glVertex3f(end[0] + offset[0], end[1] + offset[1], end[2] + offset[2]);
        glTexCoord2f(s1, 1.0f);
        glVertex3f(end[0] - offset[0], end[1] - offset[1], end[2] - offset[2]);
        glEnd();
}

static void R_DrawLightningBeamsPolygons(void)
{
	if (cl_beams_polygons.value <= 0)
		return;
	if (!r_drawentities.value)
		return;

	gltexture_t *texture = R_LightningBeam_BuiltinTexture();
	if (!texture)
		return;

	float thickness = cl_beams_polygons.value * 0.5f;

	float repeat = 0.125f;

	r_lightningbeam_scroll += host_frametime * 1.0f;
	if (r_lightningbeam_scroll > 1000.0f || r_lightningbeam_scroll < -1000.0f)
		r_lightningbeam_scroll = 0.0f;

	float scroll = r_lightningbeam_scroll - floorf(r_lightningbeam_scroll);

	GL_DisableMultitexture();
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	GL_Bind(texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	float cached_alpha = -1.0f;

	for (int i = 0; i < MAX_BEAMS; ++i)
	{
		beam_t *b = &cl_beams[i];
		if (!b->model)
			continue;
		if (b->endtime < cl.time)
			continue;
		if (!b->lightning)
			continue;

		float alpha = R_LightningAlphaForModel(b->model);
		if (alpha != cached_alpha)
		{
			glColor4f(1.0f, 1.0f, 1.0f, alpha);
			cached_alpha = alpha;
		}

		vec3_t start, end, beamdir, up, right, offset;
		CL_Beam_CalculatePositions(b, start, end);
		VectorSubtract(end, start, beamdir);
		float length = VectorNormalize(beamdir);
		if (length <= 0.01f)
			continue;

		VectorSubtract(r_refdef.vieworg, start, up);
		float proj = DotProduct(up, beamdir);
		VectorMA(up, -proj, beamdir, up);
		if (VectorNormalize(up) == 0)
		{
			if (fabsf(beamdir[2]) < 0.99f)
			{
				up[0] = 0;
				up[1] = 0;
				up[2] = 1;
			}
			else
			{
				up[0] = 1;
				up[1] = 0;
				up[2] = 0;
			}
			VectorMA(up, -DotProduct(up, beamdir), beamdir, up);
			VectorNormalize(up);
		}

		CrossProduct(beamdir, up, right);
		if (VectorNormalize(right) == 0)
			continue;

		CrossProduct(right, beamdir, up);
		VectorNormalize(up);

		float sStart = scroll;
		float sEnd = scroll + repeat * length;

		VectorScale(right, thickness, offset);
		R_Beam_DrawQuad(start, end, offset, sStart, sEnd);

		float diag = thickness * 0.70710678f;
		VectorScale(right, diag, offset);
		VectorMA(offset, diag, up, offset);
		R_Beam_DrawQuad(start, end, offset, sStart + 0.33f, sEnd + 0.33f);

		VectorScale(right, diag, offset);
		VectorMA(offset, -diag, up, offset);
		R_Beam_DrawQuad(start, end, offset, sStart + 0.66f, sEnd + 0.66f);
	}

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	glEnable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
	glColor3f(1, 1, 1);
}

/*
================
R_RenderScene
================
*/
void R_RenderScene (void)
{
	currententity = &r_worldentity;
	R_SetupScene (); //johnfitz -- this does everything that should be done once per call to RenderScene

	Fog_EnableGFog (); //johnfitz

	if (r_refdef.drawworld)
	{
		Sky_DrawSky (); //johnfitz
		R_DrawWorld ();
	}
	currententity = NULL;

	S_ExtraUpdate (); // don't let sound get messed up if going slow

	if (r_refdef.drawworld)
		R_DrawShadows (); //johnfitz -- render entity shadows

	R_DrawEntitiesOnList (false); //johnfitz -- false means this is the pass for nonalpha entities

	R_DrawWorld_Water (); //johnfitz -- drawn here since they might have transparency

	if (r_alphasort.value) // woods #alphasort
	{
		R_DrawEntitiesOnList (true); //johnfitz -- true means this is the pass for alpha entities
		R_DrawLightningBeamsPolygons();

		R_RenderDlights (); //triangle fan dlights -- johnfitz -- moved after water

		// Render particles after alpha entities for correct depth sorting -- woods #alphasort
		if (r_refdef.drawworld)
		{
			R_DrawParticles ();
#ifdef PSET_SCRIPT
			PScript_DrawParticles();
#endif
		}
	}
	else
	{
		R_DrawEntitiesOnList (true); //johnfitz -- true means this is the pass for alpha entities
		R_DrawLightningBeamsPolygons();

		R_RenderDlights (); //triangle fan dlights -- johnfitz -- moved after water

		if (r_refdef.drawworld)
		{
			R_DrawParticles ();
#ifdef PSET_SCRIPT
			PScript_DrawParticles();
#endif
		}
	}

	Fog_DisableGFog (); //johnfitz

	if (gl_laserpoint.value)
		LaserSight (); // woods #laser

	R_ShowTris (); //johnfitz

	TexturePointer_Think (); // woods #texturepointer

	R_ShowBoundingBoxes (); //johnfitz

	R_DrawTracers(); // woods #tracers

	TP_DrawLocsWithWirePoints(); // woods #locext
	LOC_ShowLocs(); // woods #locext
}

static GLuint r_warpscale_texture;
static int r_warpscale_texture_width, r_warpscale_texture_height;
static GLuint r_warpscale_program;
static GLint r_warpscale_textureLoc = -1;
static GLint r_warpscale_paramsLoc = -1;
static GLint r_warpscale_aspectLoc = -1;
static qboolean r_warpscale_shader_failed;

/*
=============
R_WarpScaleView_CanTryWarp
=============
*/
static qboolean R_WarpScaleView_CanTryWarp (void)
{
	return gl_glsl_able && GL_UseProgramFunc && !r_warpscale_shader_failed;
}

/*
=============
R_WarpScaleView_DeleteTexture
=============
*/
void R_WarpScaleView_DeleteTexture (void)
{
	glDeleteTextures (1, &r_warpscale_texture);
	r_warpscale_texture = 0;
	r_warpscale_program = 0; // deleted in R_DeleteShaders
	r_warpscale_textureLoc = -1;
	r_warpscale_paramsLoc = -1;
	r_warpscale_aspectLoc = -1;
	r_warpscale_shader_failed = false;
	GL_ClearBindings ();
}

/*
=============
R_WarpScaleView_CreateShaders
=============
*/
static qboolean R_WarpScaleView_CreateShaders (void)
{
	const GLchar *vertSource = \
		"#version 110\n"
		"\n"
		"void main(void) {\n"
		"	gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);\n"
		"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"}\n";

	const GLchar *fragSource = \
		"#version 110\n"
		"\n"
		"uniform sampler2D WarpScaleTexture;\n"
		"uniform vec4 WarpScale; // xy=UV scale z=warp amplitude w=time\n"
		"uniform float WarpAspect;\n"
		"\n"
		"void main(void) {\n"
		"	vec2 uv = gl_TexCoord[0].xy;\n"
		"	if (WarpScale.z > 0.0) {\n"
		"		float aspect = max(WarpAspect, 0.0001);\n"
		"		vec2 warp_amp = vec2(WarpScale.z, WarpScale.z * aspect);\n"
		"		uv = warp_amp + uv * (1.0 - 2.0 * warp_amp);\n"
		"		uv += warp_amp * sin(vec2(uv.y / aspect, uv.x) * (3.14159265 * 8.0) + WarpScale.w);\n"
		"	}\n"
		"	gl_FragColor = texture2D(WarpScaleTexture, uv * WarpScale.xy);\n"
		"}\n";

	GLuint program, tracked;
	GLint textureLoc, paramsLoc, aspectLoc;

	if (!R_WarpScaleView_CanTryWarp ())
		return false;

	program = GL_CreateProgram (vertSource, fragSource, 0, NULL);
	if (!program)
	{
		r_warpscale_shader_failed = true;
		return false;
	}

	tracked = program;
	textureLoc = GL_GetUniformLocation (&program, "WarpScaleTexture");
	paramsLoc = GL_GetUniformLocation (&program, "WarpScale");
	aspectLoc = GL_GetUniformLocation (&program, "WarpAspect");
	if (!program)
	{
		GL_DeleteProgramTracked (&tracked);
		r_warpscale_shader_failed = true;
		return false;
	}

	r_warpscale_program = program;
	r_warpscale_textureLoc = textureLoc;
	r_warpscale_paramsLoc = paramsLoc;
	r_warpscale_aspectLoc = aspectLoc;
	return true;
}

/*
=============
R_WarpScaleView_EnsureShader
=============
*/
static qboolean R_WarpScaleView_EnsureShader (void)
{
	if (!R_WarpScaleView_CanTryWarp ())
		return false;
	if (!r_warpscale_program)
		R_WarpScaleView_CreateShaders ();
	return r_warpscale_program != 0;
}

/*
=============
R_WaterWarpTime
=============
*/
static float R_WaterWarpTime (void)
{
	return (key_dest == key_menu || cl.paused) ? (float)realtime : cl.time;
}

/*
================
R_WarpScaleView

The r_scale cvar allows rendering the 3D view at 1/2, 1/3, or 1/4 resolution.
This function scales the reduced resolution 3D view back up to fill 
r_refdef.vrect. This is for emulating a low-resolution pixellated look,
or possibly as a perforance boost on slow graphics cards.
================
*/
void R_WarpScaleView (void)
{
	float smax, tmax;
	int scale;
	int srcx, srcy, srcw, srch;
	qboolean use_warp_shader;

	// copied from R_SetupGL()
	scale = CLAMP(1, (int)r_scale.value, 4);
	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width / scale;
	srch = r_refdef.vrect.height / scale;

	if (!r_refdef.drawworld || (scale == 1 && !water_warp))
		return;

	use_warp_shader = water_warp && r_warpscale_program;
	if (water_warp && !use_warp_shader && scale == 1)
		return;

	// make sure texture unit 0 is selected
	GL_DisableMultitexture ();

	// create (if needed) and bind the render-to-texture texture
	if (!r_warpscale_texture)
	{
		glGenTextures (1, &r_warpscale_texture);

		r_warpscale_texture_width = 0;
		r_warpscale_texture_height = 0;
	}
	glBindTexture (GL_TEXTURE_2D, r_warpscale_texture);

	// resize render-to-texture texture if needed
	if (r_warpscale_texture_width < srcw
		|| r_warpscale_texture_height < srch)
	{
		r_warpscale_texture_width = srcw;
		r_warpscale_texture_height = srch;

		if (!gl_texture_NPOT)
		{
			r_warpscale_texture_width = TexMgr_Pad(r_warpscale_texture_width);
			r_warpscale_texture_height = TexMgr_Pad(r_warpscale_texture_height);
		}

		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, r_warpscale_texture_width, r_warpscale_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	// copy the framebuffer to the texture
	glBindTexture (GL_TEXTURE_2D, r_warpscale_texture);
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);

	// draw the texture back to the framebuffer
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);

	glViewport (srcx, srcy, r_refdef.vrect.width, r_refdef.vrect.height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity ();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity ();

	// correction factor if we lack NPOT textures, normally these are 1.0f
	smax = srcw/(float)r_warpscale_texture_width;
	tmax = srch/(float)r_warpscale_texture_height;

	if (use_warp_shader)
	{
		float aspect = (r_refdef.vrect.height > 0) ? r_refdef.vrect.width/(float)r_refdef.vrect.height : 1.0f;

		GL_UseProgramFunc (r_warpscale_program);
		GL_Uniform1iFunc (r_warpscale_textureLoc, 0);
		GL_Uniform4fFunc (r_warpscale_paramsLoc, smax, tmax, 1.0f/256.0f, R_WaterWarpTime ());
		GL_Uniform1fFunc (r_warpscale_aspectLoc, aspect);
	}

	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);
	glVertex2f (-1, -1);
	glTexCoord2f (use_warp_shader ? 1.0f : smax, 0);
	glVertex2f (1, -1);
	glTexCoord2f (use_warp_shader ? 1.0f : smax, use_warp_shader ? 1.0f : tmax);
	glVertex2f (1, 1);
	glTexCoord2f (0, use_warp_shader ? 1.0f : tmax);
	glVertex2f (-1, 1);
	glEnd ();

	if (use_warp_shader)
		GL_UseProgramFunc (0);

	// clear cached binding
	GL_ClearBindings ();
}

static qboolean R_SkyroomWasVisible(void)
{
	qmodel_t *model = cl.worldmodel;
	texture_t *t;
	size_t i;
	extern cvar_t r_fastsky;
	if (!skyroom_enabled || r_fastsky.value == 1) // woods -- #fastsky2
		return false;
	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];
		if (t && t->texturechains[chain_world] && t->texturechains[chain_world]->flags & SURF_DRAWSKY)
			return true;
	}
	return false;
}

/*
================
R_RenderView
================
*/
void R_RenderView (void)
{
	static qboolean skyroom_visible;
	double	time1, time2;

	r_view_matrices_valid = false;

	if (r_norefresh.value)
		return;

	if (r_refdef.drawworld && !cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	time1 = 0; /* avoid compiler warning */
	if (r_speeds.value)
	{
		glFinish ();
		time1 = Sys_DoubleTime ();

		//johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys =
		rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}
	else if (gl_finish.value)
		glFinish ();

	if (lightmaps_latecached)
	{
		GL_BuildLightmaps ();
		GL_BuildBModelVertexBuffer ();
		lightmaps_latecached=false;
	}


	//Spike -- quickly draw the world from the skyroom camera's point of view.
	skyroom_drawn = false;
	if (r_refdef.drawworld && skyroom_enabled && skyroom_visible)
	{
		vec3_t vieworg;
		vec3_t viewang;
		VectorCopy(r_refdef.vieworg, vieworg);
		VectorCopy(r_refdef.viewangles, viewang);
		VectorMA(skyroom_origin, skyroom_origin[3],vieworg, r_refdef.vieworg); //allow a little paralax

		if (skyroom_orientation[3])
		{
			vec3_t axis[3];
			float ang = skyroom_orientation[3] * cl.time;
			if (!skyroom_orientation[0]&&!skyroom_orientation[1]&&!skyroom_orientation[2])
			{
				skyroom_orientation[0] = 0;
				skyroom_orientation[1] = 0;
				skyroom_orientation[2] = 1;
			}
			VectorNormalize(skyroom_orientation);
			RotatePointAroundVector(axis[0], skyroom_orientation, vpn, ang);
			RotatePointAroundVector(axis[1], skyroom_orientation, vright, ang);
			RotatePointAroundVector(axis[2], skyroom_orientation, vup, ang);
			VectorAngles(axis[0], axis[2], r_refdef.viewangles);
		}

		skyroom_drawing = true;
		R_SetupView ();
		//note: sky boxes are generally considered an 'infinite' distance away such that you'd not see paralax.
		//that's my excuse for not handling r_stereo here, and I'm sticking to it.
		R_RenderScene ();

		VectorCopy(vieworg, r_refdef.vieworg);
		VectorCopy(viewang, r_refdef.viewangles);
		skyroom_drawn = true;	//disable glClear(GL_COLOR_BUFFER_BIT)
	}
	skyroom_drawing = false;
	//skyroom end

	R_SetupView (); //johnfitz -- this does everything that should be done once per frame

	//johnfitz -- stereo rendering -- full of hacky goodness
	if (r_stereo.value)
	{
		float eyesep = CLAMP(-8.0f, r_stereo.value, 8.0f);
		float fdepth = CLAMP(32.0f, r_stereodepth.value, 1024.0f);

		AngleVectors (r_refdef.viewangles, vpn, vright, vup);

		//render left eye (red)
		glColorMask(1, 0, 0, 1);
		VectorMA (r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = 0.5 * eyesep * NEARCLIP / fdepth;
		srand((int) (cl.time * 1000)); //sync random stuff between eyes

		R_RenderScene ();

		//render right eye (cyan)
		glClear (GL_DEPTH_BUFFER_BIT);
		glColorMask(0, 1, 1, 1);
		VectorMA (r_refdef.vieworg, 1.0f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = -frustum_skew;
		srand((int) (cl.time * 1000)); //sync random stuff between eyes

		R_RenderScene ();

		//restore
		glColorMask(1, 1, 1, 1);
		VectorMA (r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = 0.0f;
	}
	else
	{
		R_RenderScene ();
	}
	//johnfitz

	//Spike: flag whether the skyroom was actually visible, so we don't needlessly draw it when its not (1 frame's lag, hopefully not too noticable)
	if (r_refdef.drawworld)
	{
		extern cvar_t r_fastsky;
		if (r_viewleaf->contents == CONTENTS_SOLID || r_drawflat_cheatsafe || r_lightmap_cheatsafe || r_fastsky.value == 1) // woods -- #fastsky2
			skyroom_visible = false;	//don't do skyrooms when the view is in the void, for framerate reasons while debugging.
		else
			skyroom_visible = RSceneCache_HasSky() || R_SkyroomWasVisible();
		skyroom_drawn = false;
	}
	//skyroom end

	if (gl_motion_blur.value > 0.0f) // woods #motionblur
	{
		R_RenderSceneBlur(gl_motion_blur.value);
	}

	R_WarpScaleView ();

	//johnfitz -- modified r_speeds output
	time2 = Sys_DoubleTime ();
	if (r_pos.value)
		Con_Printf ("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
					(int)cl.entities[cl.viewentity].origin[0],
					(int)cl.entities[cl.viewentity].origin[1],
					(int)cl.entities[cl.viewentity].origin[2],
					(int)cl.viewangles[PITCH],
					(int)cl.viewangles[YAW],
					(int)cl.viewangles[ROLL]);
	else if (r_speeds.value == 2)
		Con_Printf ("%3i ms  %4i/%4i wpoly %4i/%4i epoly %3i lmap %4i/%4i sky %1.1f mtex\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_brushpasses,
					rs_aliaspolys,
					rs_aliaspasses,
					rs_dynamiclightmaps,
					rs_skypolys,
					rs_skypasses,
					TexMgr_FrameUsage ());
	else if (r_speeds.value)
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_aliaspolys,
					rs_dynamiclightmaps);
	//johnfitz
}
