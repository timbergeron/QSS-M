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

float r_fovx, r_fovy; //johnfitz -- rendering fov may be different becuase of r_waterwarp and r_stereo

extern byte* SV_FatPVS (vec3_t org, qmodel_t* worldmodel); // woods #iwshowbboxes
extern qboolean SV_EdictInPVS (edict_t* test, byte* pvs); // woods #iwshowbboxes
extern qboolean SV_BoxInPVS (vec3_t mins, vec3_t maxs, byte* pvs, mnode_t* node); // woods #iwshowbboxes
extern char	skybox_name[1024]; // woods -- #fastsky2

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

//johnfitz -- new cvars
cvar_t	r_stereo = {"r_stereo","0",CVAR_NONE};
cvar_t	r_stereodepth = {"r_stereodepth","128",CVAR_NONE};
cvar_t	r_clearcolor = {"r_clearcolor","2",CVAR_ARCHIVE};
cvar_t	r_drawflat = {"r_drawflat","0",CVAR_NONE};
cvar_t	r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t	gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE};
cvar_t	gl_farclip = {"gl_farclip", "16384", CVAR_ARCHIVE};
cvar_t	gl_overbright = {"gl_overbright", "1", CVAR_ARCHIVE};
cvar_t	gl_overbright_models = {"gl_overbright_models", "2", CVAR_ARCHIVE};
cvar_t	gl_overbright_models_alpha = {"gl_overbright_models_alpha", "1", CVAR_ARCHIVE}; // woods #obmodelslist
cvar_t	gl_overbright_models_list = {"gl_overbright_models_list", "progs/armor.mdl,progs/backpack.mdl,progs/bolt.mdl,progs/bolt2.mdl,progs/bolt3.mdl,progs/end1.mdl,progs/end2.mdl,progs/end3.mdl,progs/end4.mdl,progs/eyes.mdl,progs/g_light.mdl,progs/g_nail.mdl,progs/g_nail2.mdl,progs/g_rock.mdl,progs/g_rock2.mdl,progs/g_shot.mdl,progs/grenade.mdl,progs/invisibl.mdl,progs/invulner.mdl,progs/missile.mdl,progs/player.mdl,progs/quaddama.mdl,progs/s_spike.mdl,progs/spike.mdl,progs/v_axe.mdl,progs/v_light.mdl,progs/v_nail.mdl,progs/v_nail2.mdl,progs/v_rock.mdl,progs/v_rock2.mdl,progs/v_shot.mdl,progs/v_shot2.mdl,progs/v_spike.mdl,progs/w_spike.mdl,progs/bit.mdl,progs/flag.mdl,progs/flag2.mdl,progs/flag3.mdl,progs/ctfmodel.mdl,progs/star.mdl,progs/v_star.mdl", CVAR_ARCHIVE}; // woods #obmodelslist
cvar_t	r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t	r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t	r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t	r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t	r_showlocs = {"r_showlocs", "0", CVAR_ARCHIVE}; // woods #locext
cvar_t	r_showlocs_y = {"r_showlocs_y", "30", CVAR_ARCHIVE }; // woods #locext
cvar_t	r_lerpmodels = {"r_lerpmodels", "1", CVAR_ARCHIVE};
cvar_t	r_lerpmove = {"r_lerpmove", "1", CVAR_ARCHIVE};
cvar_t	r_nolerp_list = {"r_nolerp_list", "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl", CVAR_ARCHIVE};
cvar_t	r_noshadow_list = {"r_noshadow_list", "progs/flame2.mdl,progs/flame.mdl,progs/bolt1.mdl,progs/bolt2.mdl,progs/bolt3.mdl,progs/laser.mdl", CVAR_ARCHIVE};

extern cvar_t	r_vfog;
//johnfitz

cvar_t	gl_zfix = {"gl_zfix", "0", CVAR_ARCHIVE}; // QuakeSpasm z-fighting fix

cvar_t	r_lavaalpha = {"r_lavaalpha","0",CVAR_ARCHIVE};
cvar_t	r_telealpha = {"r_telealpha","0",CVAR_ARCHIVE};
cvar_t	r_slimealpha = {"r_slimealpha","0",CVAR_ARCHIVE};

cvar_t	trace_any = {"trace_any","0",CVAR_NONE}; // woods #tracers
cvar_t	trace_any_contains = {"trace_any_contains","item_artifact_super_damage",CVAR_NONE}; // woods #tracers
cvar_t	r_drawflame = {"r_drawflame","1",CVAR_ARCHIVE}; // woods #drawflame

float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;
float	map_fallbackalpha;

int	map_ctf_flag_style; // woods #alternateflags
extern int ogflagprecache, swapflagprecache, swapflagprecache2, swapflagprecache3; // woods #alternateflags

qboolean r_drawflat_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe, r_drawworld_cheatsafe; //johnfitz

cvar_t	r_scale = {"r_scale", "1", CVAR_ARCHIVE};

void LaserSight(void);


//==============================================================================
//
// GLSL GAMMA CORRECTION
//
//==============================================================================

static GLuint r_gamma_texture;
static GLuint r_gamma_program;
static int r_gamma_texture_width, r_gamma_texture_height;

// uniforms used in gamma shader
static GLuint gammaLoc;
static GLuint contrastLoc;
static GLuint textureLoc;

/*
=============
GLSLGamma_DeleteTexture
=============
*/
void GLSLGamma_DeleteTexture (void)
{
	glDeleteTextures (1, &r_gamma_texture);
	r_gamma_texture = 0;
	r_gamma_program = 0; // deleted in R_DeleteShaders
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
		"uniform float GammaValue;\n"
		"uniform float ContrastValue;\n"
		"\n"
		"void main(void) {\n"
		"	  vec4 frag = texture2D(GammaTexture, gl_TexCoord[0].xy);\n"
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

	if (!gl_glsl_gamma_able)
		return;

	if (vid_gamma.value == 1 && vid_contrast.value == 1)
		return;

// create render-to-texture texture if needed
	if (!r_gamma_texture)
	{
		glGenTextures (1, &r_gamma_texture);
		r_gamma_texture_width = 0;
		r_gamma_texture_height = 0;
	}
	GL_DisableMultitexture();
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

// copy the framebuffer to the texture
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, glx, gly, glwidth, glheight);

// draw the texture back to the framebuffer with a fragment shader
	GL_UseProgramFunc (r_gamma_program);
	GL_Uniform1fFunc (gammaLoc, gamma_value); // woods #gammaclamp
	GL_Uniform1fFunc (contrastLoc, q_min(2.0f, q_max(1.0f, vid_contrast.value)));
	GL_Uniform1iFunc (textureLoc, 0); // use texture unit 0

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

// clear cached binding
	GL_ClearBindings ();
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
		vec[0] = ((signbits % 2)<1) ? emaxs[0] : emins[0];
		vec[1] = ((signbits % 4)<2) ? emaxs[1] : emins[1];
		vec[2] = ((signbits % 8)<4) ? emaxs[2] : emins[2];
		if (p->normal[0]*vec[0] + p->normal[1]*vec[1] + p->normal[2]*vec[2] < p->dist)
			return true;
	}
	return false;
}

/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;
	vec_t scalefactor, *minbounds, *maxbounds;

	if (e->angles[0] || e->angles[2]) //pitch or roll
	{
		minbounds = e->model->rmins;
		maxbounds = e->model->rmaxs;
	}
	else if (e->angles[1]) //yaw
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
		VectorMA (e->origin, scalefactor, minbounds, mins);
		VectorMA (e->origin, scalefactor, maxbounds, maxs);
	}
	else
	{
		VectorAdd (e->origin, minbounds, mins);
		VectorAdd (e->origin, maxbounds, maxs);
	}

	return R_CullBox (mins, maxs);
}

/*
===============
R_RotateForEntity -- johnfitz -- modified to take origin and angles instead of pointer to entity
===============
*/
void R_RotateForEntity (vec3_t origin, vec3_t angles, unsigned char scale)
{
	glTranslatef (origin[0],  origin[1],  origin[2]);
	glRotatef (angles[1],  0, 0, 1);
	glRotatef (-angles[0],  0, 1, 0);
	glRotatef (angles[2],  1, 0, 0);

	if (scale != ENTSCALE_DEFAULT)
	{
		float scalefactor = ENTSCALE_DECODE(scale);
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

	//johnfitz -- rewrote this section
	if (!r_refdef.drawworld)
		scale = 1;	//don't rescale. we can't handle rescaling transparent parts.
	else
		scale =  CLAMP(1, (int)r_scale.value, 4); // ericw -- see R_ScaleView
	glViewport (glx + r_refdef.vrect.x,
				gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height,
				r_refdef.vrect.width / scale,
				r_refdef.vrect.height / scale);
	//johnfitz

	#if 1	//Spike: these should be equivelent. gpus tend not to use doubles in favour of speed, so no loss there.
	{
		mat4_t mat;
		Matrix4_ProjectionMatrix(r_fovx, r_fovy, NEARCLIP, gl_farclip.value, false, frustum_skew, 0, mat);
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(mat);

		Matrix4_ViewMatrix(r_refdef.viewangles, r_refdef.vieworg, mat);
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(mat);
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
		clearbits |= GL_STENCIL_BUFFER_BIT;
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

	Fog_SetupFrame (); //johnfitz

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

	V_SetContentsColor (viewcontents);
	V_CalcBlend ();

	//johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	if (r_waterwarp.value)
	{
		if (viewcontents == CONTENTS_WATER || viewcontents == CONTENTS_SLIME || viewcontents == CONTENTS_LAVA)
		{
			//variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.  what a mess!
			r_fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) * (0.97 + sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			r_fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) * (1.03 - sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
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
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (qboolean alphapass) //johnfitz -- added parameter
{
	int		i;

	if (!r_drawentities.value)
		return;

	//johnfitz -- sprites are not a special case
	for (i=0 ; i<cl_numvisedicts ; i++)
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

		switch (currententity->model->type)
		{
			case mod_alias:

				if (swapflagprecache && map_ctf_flag_style == 2 && !strcmp(currententity->model->name, "progs/flag.mdl")) // is there an alternate flag prechaced and worldspawn, if so lets swap it #alternateflags
				{
					if (currententity->baseline.modelindex == ogflagprecache) // if the model is the flag, we're gonna swap it
					{
						currententity->syncbase = 0;
						currententity->model->flags = MOD_NOLERP | MOD_NOSHADOW;
						currententity->model = cl.model_precache[swapflagprecache]; // roque
			
					}
				}

				if (swapflagprecache2 && map_ctf_flag_style == 3 && !strcmp(currententity->model->name, "progs/flag.mdl")) // is there an alternate flag prechaced and worldspawn, if so lets swap it #alternateflags
				{
					if (currententity->baseline.modelindex == ogflagprecache) // if the model is the flag, we're gonna swap it
					{
						currententity->syncbase = 0;
						currententity->model->flags = MOD_NOLERP | MOD_NOSHADOW;
						currententity->model = cl.model_precache[swapflagprecache2]; // alt1 (flag2.mdl)

					}
				}

				if (swapflagprecache3 && map_ctf_flag_style == 4 && !strcmp(currententity->model->name, "progs/flag.mdl")) // is there an alternate flag prechaced and worldspawn, if so lets swap it #alternateflags
				{
					if (currententity->baseline.modelindex == ogflagprecache) // if the model is the flag, we're gonna swap it
					{
						currententity->syncbase = 0;
						currententity->model->flags = MOD_NOLERP | MOD_NOSHADOW;
						currententity->model = cl.model_precache[swapflagprecache3]; // alt2 (flag3.mdl)

					}
				}

				R_DrawAliasModel (currententity);
				break;
			case mod_brush:
				R_DrawBrushModel (currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel (currententity);
				break;
			case mod_ext_invalid:
				//nothing. could draw a blob instead.
				break;
		}
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
	edict_t		*ed;
	int			i, mode; // woods #iwshowbboxes
	uint32_t	color; // woods #iwshowbboxes
	qcvm_t 		*oldvm;	//in case we ever draw a scene from within csqc.

	if (!r_showbboxes.value || cl.maxclients > 1 || !r_drawentities.value || !sv.active)
		return;

	glDisable (GL_DEPTH_TEST);
	glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset (OFFSET_SHOWTRIS);
	glDisable (GL_TEXTURE_2D);
	glDisable (GL_CULL_FACE);

	oldvm = qcvm;
	PR_SwitchQCVM(NULL);
	PR_SwitchQCVM(&sv.qcvm);

	mode = abs((int)r_showbboxes.value); // woods #iwshowbboxes
	if (mode >= 2)
	{
		vec3_t org;
		VectorAdd(sv_player->v.origin, sv_player->v.view_ofs, org);
		pvs = SV_FatPVS(org, qcvm->worldmodel);
	}
	else
		pvs = NULL;

	for (i=1, ed=NEXT_EDICT(qcvm->edicts) ; i<qcvm->num_edicts ; i++, ed=NEXT_EDICT(ed))
	{
		if (ed == sv_player || ed->free)
			continue; //don't draw player's own bbox or freed edicts

//		if (r_showbboxes.value != 2)
//			if (!SV_VisibleToClient (sv_player, ed, sv.worldmodel))
//				continue; //don't draw if not in pvs

		if (!R_ShowBoundingBoxesFilter(ed))
			continue;

		if (pvs) // woods #iwshowbboxes
		{
			qboolean inpvs =
				ed->num_leafs ?
				SV_EdictInPVS(ed, pvs) :
				SV_BoxInPVS(ed->v.absmin, ed->v.absmax, pvs, qcvm->worldmodel->nodes)
				;
			if (!inpvs)
				continue;
		}

		if (r_showbboxes.value > 0.f) // woods #iwshowbboxes
		{
			int modelindex = (int)ed->v.modelindex;
			color = 0xff800080;
			if (modelindex >= 0 && modelindex < MAX_MODELS && sv.models[modelindex])
			{
				switch (sv.models[modelindex]->type)
				{
				case mod_brush:  color = 0xffff8080; break;
				case mod_alias:  color = 0xff408080; break;
				case mod_sprite: color = 0xff4040ff; break;
				default:
					break;
				}
			}
			if (ed->v.health > 0)
				color = 0xff0000ff;
		}
		else
			color = 0xffffffff;

		if (ed->v.mins[0] == ed->v.maxs[0] && ed->v.mins[1] == ed->v.maxs[1] && ed->v.mins[2] == ed->v.maxs[2])
		{
			//point entity
			R_EmitWirePoint (ed->v.origin, color); // woods #iwshowbboxes
		}
		else
		{
			//box entity
			if ((ed->v.solid == SOLID_BSP || ed->v.solid == SOLID_EXT_BSPTRIGGER) && (ed->v.angles[0]||ed->v.angles[1]||ed->v.angles[2]) && pr_checkextension.value)
				R_EmitWireBox (ed->v.absmin, ed->v.absmax, color); // woods #iwshowbboxes
			else
			{
				VectorAdd (ed->v.mins, ed->v.origin, mins);
				VectorAdd (ed->v.maxs, ed->v.origin, maxs);
				R_EmitWireBox (mins, maxs, color); // woods #iwshowbboxes
			}
		}
	}
	PR_SwitchQCVM(NULL);
	PR_SwitchQCVM(oldvm);

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

		int		i;
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
		R_RotateForEntity(enty->origin, enty->angles, enty->netstate.scale);
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

	if (!r_shadows.value || !r_drawentities.value || r_drawflat_cheatsafe || r_lightmap_cheatsafe)
		return;

	// Use stencil buffer to prevent self-intersecting shadows, from Baker (MarkV)
	if (gl_stencilbits)
	{
		glClear(GL_STENCIL_BUFFER_BIT);
		glStencilFunc(GL_EQUAL, 0, ~0);
		glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
		glEnable(GL_STENCIL_TEST);
	}

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		if (!currententity->model) // woods
			continue;

		if (currententity->model->type != mod_alias)
			continue;

		GL_DrawAliasShadow (currententity);
	}

	if (gl_stencilbits)
	{
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

	R_DrawEntitiesOnList (true); //johnfitz -- true means this is the pass for alpha entities

	R_RenderDlights (); //triangle fan dlights -- johnfitz -- moved after water

	if (r_refdef.drawworld)
	{
		R_DrawParticles ();
#ifdef PSET_SCRIPT
		PScript_DrawParticles();
#endif
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

static GLuint r_scaleview_texture;
static int r_scaleview_texture_width, r_scaleview_texture_height;

/*
=============
R_ScaleView_DeleteTexture
=============
*/
void R_ScaleView_DeleteTexture (void)
{
	glDeleteTextures (1, &r_scaleview_texture);
	r_scaleview_texture = 0;
}

/*
================
R_ScaleView

The r_scale cvar allows rendering the 3D view at 1/2, 1/3, or 1/4 resolution.
This function scales the reduced resolution 3D view back up to fill 
r_refdef.vrect. This is for emulating a low-resolution pixellated look,
or possibly as a perforance boost on slow graphics cards.
================
*/
void R_ScaleView (void)
{
	float smax, tmax;
	int scale;
	int srcx, srcy, srcw, srch;

	// copied from R_SetupGL()
	scale = CLAMP(1, (int)r_scale.value, 4);
	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width / scale;
	srch = r_refdef.vrect.height / scale;

	if (scale == 1 || !r_refdef.drawworld)
		return;

	// make sure texture unit 0 is selected
	GL_DisableMultitexture ();

	// create (if needed) and bind the render-to-texture texture
	if (!r_scaleview_texture)
	{
		glGenTextures (1, &r_scaleview_texture);

		r_scaleview_texture_width = 0;
		r_scaleview_texture_height = 0;
	}
	glBindTexture (GL_TEXTURE_2D, r_scaleview_texture);

	// resize render-to-texture texture if needed
	if (r_scaleview_texture_width < srcw
		|| r_scaleview_texture_height < srch)
	{
		r_scaleview_texture_width = srcw;
		r_scaleview_texture_height = srch;

		if (!gl_texture_NPOT)
		{
			r_scaleview_texture_width = TexMgr_Pad(r_scaleview_texture_width);
			r_scaleview_texture_height = TexMgr_Pad(r_scaleview_texture_height);
		}

		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, r_scaleview_texture_width, r_scaleview_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	// copy the framebuffer to the texture
	glBindTexture (GL_TEXTURE_2D, r_scaleview_texture);
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
	smax = srcw/(float)r_scaleview_texture_width;
	tmax = srch/(float)r_scaleview_texture_height;

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

	// clear cached binding
	GL_ClearBindings ();
}

static qboolean R_SkyroomWasVisible(void)
{
	qmodel_t *model = cl.worldmodel;
	texture_t *t;
	size_t i;
	extern cvar_t r_fastsky;
	if (!skyroom_enabled || (r_fastsky.value == 1) || (r_fastsky.value == 2 && !skybox_name[0])) // woods -- #fastsky2
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
		if (r_viewleaf->contents == CONTENTS_SOLID || r_drawflat_cheatsafe || r_lightmap_cheatsafe || (r_fastsky.value == 1) || (r_fastsky.value == 2 && !skybox_name[0])) // woods -- #fastsky2
			skyroom_visible = false;	//don't do skyrooms when the view is in the void, for framerate reasons while debugging.
		else
			skyroom_visible = RSceneCache_HasSky() || R_SkyroomWasVisible();
		skyroom_drawn = false;
	}
	//skyroom end

	R_ScaleView ();

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

