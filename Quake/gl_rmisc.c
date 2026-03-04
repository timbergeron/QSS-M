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
// r_misc.c

#include "quakedef.h"

//johnfitz -- new cvars
extern cvar_t r_alphasort; // woods #alphasort
extern cvar_t r_stereo;
extern cvar_t r_stereodepth;
extern cvar_t r_clearcolor;
extern cvar_t r_drawflat;
extern cvar_t r_flatlightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t gl_overbright;
extern cvar_t gl_overbright_models;
extern cvar_t r_model_light_desat; // woods - remove colored lighting from alias models #dedat
extern cvar_t r_model_light_desat_list; // woods #dedat
extern cvar_t r_waterwarp;
extern cvar_t r_oldskyleaf;
extern cvar_t r_drawworld;
extern cvar_t r_showtris;
extern cvar_t r_showbboxes;
extern cvar_t r_showlocs; // woods #locext
extern cvar_t r_showlocs_y; // woods #locext
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_nolerp_list;
extern cvar_t r_noshadow_list;
extern cvar_t r_nooutline_list; // woods #routline
extern cvar_t r_outline; // woods #routline
extern cvar_t r_player_xray; // woods #routline
#ifdef MACBOOK_ARM_HACK // woods #collinear
extern cvar_t r_remove_collinear_vertices;
#endif

//johnfitz
extern cvar_t r_scenecache;
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix
cvar_t r_brokenturbbias = {"r_brokenturbbias", "1", CVAR_ARCHIVE}; //replicates QS's bug where it ignores texture coord offsets for water (breaking curved water volumes). we do NOT ignore scales though.

extern cvar_t trace_any; // woods #tracers
extern cvar_t trace_any_contains; // woods #tracers
extern cvar_t r_drawflame; // woods #drawflame

extern gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz

void TexturePointer_Init (void); // woods #texturepointer
void R_Ambient_OnChange_f(cvar_t* var); // woods #rambient

/*
====================
R_ShowbboxesFilter_f -- woods #iwshowbboxes
====================
*/
static void R_ShowbboxesFilter_f (void)
{
	extern char r_showbboxes_filter_strings[MAXCMDLINE];

	if (Cmd_Argc() >= 2)
	{
		int i, len, ofs;
		for (i = 1, ofs = 0; i < Cmd_Argc(); i++)
		{
			const char* arg = Cmd_Argv(i);
			if (!*arg)
				continue;
			len = strlen(arg) + 1;
			if (ofs + len + 1 > (int) countof(r_showbboxes_filter_strings))
			{
				Con_Warning("overflow at \"%s\"\n", arg);
				break;
			}
			memcpy(&r_showbboxes_filter_strings[ofs], arg, len);
			ofs += len;
		}
		r_showbboxes_filter_strings[ofs++] = '\0';
	}
	else
	{
		const char* p = r_showbboxes_filter_strings;
		Con_SafePrintf("\"r_showbboxes_filter\" is");
		if (!*p)
			Con_SafePrintf(" \"\"");
		else do
		{
			Con_SafePrintf(" \"%s\"", p);
			p += strlen(p) + 1;
		} while (*p);
		Con_SafePrintf("\n");
	}
}

/*
===============
Tracer_Completion_f -- woods
===============
*/
static void Tracer_Completion_f (cvar_t* cvar, const char* partial)
{
	int i;
	edict_t* ed;

	qcvm_t* oldvm;
	oldvm = qcvm;
	PR_SwitchQCVM (NULL);
	PR_SwitchQCVM (&sv.qcvm);
	for (i = 0, ed = NEXT_EDICT (qcvm->edicts); i < qcvm->num_edicts; i++, ed = NEXT_EDICT (ed))
	{
		if (ed->free) continue;

		const char* classname = PR_GetString (ed->v.classname);
		classname = PR_GetString (ed->v.classname);
		if (*classname)
			Con_AddToTabList (classname, partial, "#", NULL); // #demolistsort add arg
	}

	PR_SwitchQCVM (NULL);
	PR_SwitchQCVM (oldvm);
}

/*
====================
GL_Overbright_f -- johnfitz
====================
*/
static void GL_Overbright_f (cvar_t *var)
{
	R_RebuildAllLightmaps ();
}

/*
====================
GL_Fullbrights_f -- johnfitz
====================
*/
static void GL_Fullbrights_f (cvar_t *var)
{
	TexMgr_ReloadNobrightImages ();
}

/*
====================
R_SetClearColor_f -- johnfitz
====================
*/
static void R_SetClearColor_f (cvar_t *var)
{
	byte	*rgb;
	int		s;

	s = (int)r_clearcolor.value & 0xFF;
	rgb = (byte*)(d_8to24table + s);
	glClearColor (rgb[0]/255.0,rgb[1]/255.0,rgb[2]/255.0,0);
}

/*
===============
R_Model_ExtraFlags_List_f -- johnfitz -- called when r_nolerp_list or r_noshadow_list cvar changes
===============
*/
static void R_Model_ExtraFlags_List_f (cvar_t *var)
{
	int i;
	for (i=0; i < MAX_MODELS; i++)
		Mod_SetExtraFlags (cl.model_precache[i]);
}

/*
====================
R_SetWateralpha_f -- ericw
====================
*/
static void R_SetWateralpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent&SURF_DRAWWATER) && var->value < 1)
		Con_Warning("Map does not appear to be water-vised\n");
	map_wateralpha = var->value;
	map_fallbackalpha = var->value;
}

/*
====================
R_SetLavaalpha_f -- ericw
====================
*/
static void R_SetLavaalpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent&SURF_DRAWLAVA) && var->value && var->value < 1)
		Con_Warning("Map does not appear to be lava-vised\n");
	map_lavaalpha = var->value;
}

/*
====================
R_SetTelealpha_f -- ericw
====================
*/
static void R_SetTelealpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent&SURF_DRAWTELE) && var->value && var->value < 1)
		Con_Warning("Map does not appear to be tele-vised\n");
	map_telealpha = var->value;
}

/*
====================
R_SetSlimealpha_f -- ericw
====================
*/
static void R_SetSlimealpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel && !(cl.worldmodel->contentstransparent&SURF_DRAWSLIME) && var->value && var->value < 1)
		Con_Warning("Map does not appear to be slime-vised\n");
	map_slimealpha = var->value;
}

/*
====================
GL_WaterAlphaForSurfface -- ericw
====================
*/
float GL_WaterAlphaForSurface (msurface_t *fa)
{
	if (fa->flags & SURF_DRAWLAVA)
		return map_lavaalpha > 0 ? map_lavaalpha : map_fallbackalpha;
	else if (fa->flags & SURF_DRAWTELE)
		return map_telealpha > 0 ? map_telealpha : map_fallbackalpha;
	else if (fa->flags & SURF_DRAWSLIME)
		return map_slimealpha > 0 ? map_slimealpha : map_fallbackalpha;
	else
		return map_wateralpha;// > 0 ? map_wateralpha : map_fallbackalpha;
}

gltexture_t* underwatertexture; // woods #caustics
gltexture_t* shelltexture; // woods #powershell

static void R_InitOtherTextures () // woods #powershell #caustics
{
	int w, h;
	enum srcformat fmt;
	qboolean malloced;
	byte* data;

	if (COM_FileExists("textures/shellmap.tga", NULL))
	{
		data = Image_LoadImage("textures/shellmap", &w, &h, &fmt, &malloced);
		shelltexture = TexMgr_LoadImage(NULL, "textures/shellmap", w, h, fmt, data, "textures/shellmap", 0,
			TEXPREF_ALPHA |      // Allow alpha channel
			TEXPREF_LINEAR |     // Linear filtering for smoother look
			TEXPREF_MIPMAP |     // Generate mipmaps for better quality at different scales
			TEXPREF_PERSIST |    // Keep the texture loaded
			TEXPREF_NOPICMIP     // Always use full resolution
		);
	}

	if (COM_FileExists("textures/water_caustic.tga", NULL))
	{
		data = Image_LoadImage("textures/water_caustic", &w, &h, &fmt, &malloced);
		underwatertexture = TexMgr_LoadImage(NULL, "textures/water_caustic", w, h, fmt, data, "textures/water_caustic", 0, 
			TEXPREF_ALPHA |      // Allow alpha channel
			TEXPREF_LINEAR |     // Linear filtering for smoother look
			TEXPREF_MIPMAP |     // Generate mipmaps for better quality at different scales
			TEXPREF_PERSIST |    // Keep the texture loaded
			TEXPREF_NOPICMIP     // Always use full resolution
		);
	}
}

/*
====================
ClearParticles_f -- woods #drawflame
====================
*/
static void ClearParticles_f (cvar_t* var)
{
	PScript_ClearParticles ();
}

/*
===============
GL_Skin_Completion_f -- woods #iwtabcomplete
===============
*/
static void GL_Skin_Completion_f (cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("0x66ff00", partial, "bright green", NULL); // #demolistsort add arg
	Con_AddToTabList("0xfff700", partial, "bright yellow", NULL); // #demolistsort add arg
	Con_AddToTabList("0xff00cd", partial, "bright pink", NULL); // #demolistsort add arg

	return;
}

/*
===============
Cl_Damagehue_Completion_f -- woods #iwtabcomplete
===============
*/
static void Cl_Damagehue_Completion_f (cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("0xeb580e", partial, "bright orange", NULL); // #demolistsort add arg
	Con_AddToTabList("0xff0000", partial, "red", NULL); // #demolistsort add arg

	return;
}


/*
===============
R_Player_Xray_Completion_f -- woods #iwtabcomplete
===============
*/
static void R_Player_Xray_Completion_f (cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("0", partial, "disable", NULL);
	Con_AddToTabList("0xFF0000", partial, "base color", NULL);
	Con_AddToTabList("0.5", partial, "alpha", NULL);
	Con_AddToTabList("1.0", partial, "alpha", NULL);
	Con_AddToTabList("1024", partial, "distance", NULL);
	Con_AddToTabList("2048", partial, "distance", NULL);
	Con_AddToTabList("4096", partial, "distance", NULL);

	Con_AddToTabList("both", partial, "targets", NULL);
	Con_AddToTabList("enemy", partial, "targets", NULL);
	Con_AddToTabList("team", partial, "targets", NULL);
	Con_AddToTabList("targets=both", partial, "targets", NULL);
	Con_AddToTabList("targets=enemy", partial, "targets", NULL);
	Con_AddToTabList("targets=team", partial, "targets", NULL);

	Con_AddToTabList("pcolor", partial, "use player colors", NULL);
	Con_AddToTabList("color=pcolor", partial, "color mode", NULL);
	Con_AddToTabList("colormode=pcolor", partial, "color mode", NULL);

	Con_AddToTabList("gametype=1", partial, "1v1 only", NULL);
	Con_AddToTabList("gametype=2", partial, "up to 2v2", NULL);
	Con_AddToTabList("gametype=3", partial, "up to 3v3", NULL);
	Con_AddToTabList("gametype=4", partial, "up to 4v4", NULL);
	Con_AddToTabList("gametype=5", partial, "5v5+ (no cap)", NULL);
	Con_AddToTabList("1v1", partial, "match size", NULL);
	Con_AddToTabList("2v2", partial, "match size", NULL);
	Con_AddToTabList("3v3", partial, "match size", NULL);
	Con_AddToTabList("4v4", partial, "match size", NULL);
	Con_AddToTabList("5v5", partial, "match size (5+)", NULL);

	Con_AddToTabList("enemycolor=0xFF0000", partial, "enemy outline color", NULL);
	Con_AddToTabList("teamcolor=0x00B7FF", partial, "team outline color", NULL);
}

/*
===============
R_Init
===============
*/
void R_Init (void)
{
	extern cvar_t gl_finish;

	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);
	Cmd_AddCommand ("r_showbboxes_filter", R_ShowbboxesFilter_f); // woods #iwshowbboxes

	Cvar_RegisterVariable (&r_norefresh);
	Cvar_RegisterVariable (&r_lightmap);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);
	Cvar_RegisterVariable (&r_shadows);
	Cvar_RegisterVariable (&r_shadows_groundcheck); // woods #shadow
	Cvar_RegisterVariable (&r_shadows_bmodels); // woods #shadow
	Cvar_RegisterVariable (&r_wateralpha);
	Cvar_SetCallback (&r_wateralpha, R_SetWateralpha_f);
	Cvar_RegisterVariable (&r_dynamic);
	Cvar_RegisterVariable (&r_novis);
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_pos);
	Cvar_RegisterVariable (&r_drawflame); // woods #drawflame
	Cvar_SetCallback (&r_drawflame, ClearParticles_f); // woods #drawflame
	Cvar_RegisterVariable (&r_alphasort); // woods #alphasort

	Cvar_RegisterVariable (&gl_finish);
	Cvar_RegisterVariable (&gl_clear);
	Cvar_RegisterVariable (&gl_cull);
	Cvar_RegisterVariable (&gl_smoothmodels);
	Cvar_RegisterVariable (&gl_affinemodels);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_flashblend);
	Cvar_RegisterVariable (&gl_playermip);
	Cvar_RegisterVariable (&gl_nocolors);
	Cvar_RegisterVariable (&gl_enemycolor); // woods #enemycolors
	Cvar_SetCompletion (&gl_enemycolor, &GL_Skin_Completion_f); // woods #iwtabcomplete
	Cvar_RegisterVariable (&gl_teamcolor); // woods #enemycolors
	Cvar_SetCompletion (&gl_teamcolor, &GL_Skin_Completion_f); // woods #iwtabcomplete
	Cvar_RegisterVariable (&gl_laserpoint); // woods #laser
	Cvar_RegisterVariable (&gl_laserpoint_alpha); // woods #laser
	Cvar_RegisterVariable (&trace_any); // woods #tracers
	Cvar_RegisterVariable (&trace_any_contains); // woods #tracers
	Cvar_SetCompletion (&trace_any_contains, &Tracer_Completion_f); // woods #iwtabcomplete


	//johnfitz -- new cvars
	Cvar_RegisterVariable (&r_stereo);
	Cvar_RegisterVariable (&r_stereodepth);
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_SetCallback (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_brokenturbbias);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_drawflat);
	Cvar_RegisterVariable (&r_flatlightstyles);
	Cvar_RegisterVariable (&r_oldskyleaf);
	Cvar_RegisterVariable (&r_drawworld);
	Cvar_RegisterVariable (&r_showtris);
	Cvar_RegisterVariable (&r_showbboxes);
	Cvar_RegisterVariable (&r_showlocs); // woods #locext
	Cvar_RegisterVariable (&r_showlocs_y); // woods #locext
	Cvar_RegisterVariable (&gl_farclip);
	Cvar_RegisterVariable (&gl_fullbrights);
	Cvar_RegisterVariable (&gl_overbright);
	Cvar_RegisterVariable (&gl_powerupshells); // woods #powershell
	Cvar_RegisterVariable (&gl_powerupshells_alpha); // woods #powershell
	Cvar_RegisterVariable (&gl_caustics); // woods #caustics
	Cvar_RegisterVariable (&gl_motion_blur); // woods #motionblur
	Cvar_SetCallback (&gl_fullbrights, GL_Fullbrights_f);
	Cvar_SetCallback (&gl_overbright, GL_Overbright_f);
	Cvar_RegisterVariable (&gl_overbright_models);
	Cvar_RegisterVariable (&r_model_light_desat); // woods #dedat
	Cvar_RegisterVariable (&r_model_light_desat_list); // woods #dedat
	Cvar_RegisterVariable (&r_lerpmodels);
	Cvar_RegisterVariable (&r_lerpmove);
	Cvar_RegisterVariable (&r_nolerp_list);
	Cvar_SetCallback (&r_nolerp_list, R_Model_ExtraFlags_List_f);
	Cvar_RegisterVariable (&r_noshadow_list);
	Cvar_SetCallback (&r_noshadow_list, R_Model_ExtraFlags_List_f);
	Cvar_RegisterVariable(&r_nooutline_list); // woods #routline
	Cvar_RegisterVariable(&r_outline); // woods #routline
	Cvar_RegisterVariable(&r_player_xray); // woods #routline
	Cvar_SetCompletion (&r_player_xray, &R_Player_Xray_Completion_f); // woods #iwtabcomplete
#ifdef MACBOOK_ARM_HACK // woods #collinear
	Cvar_RegisterVariable (&r_remove_collinear_vertices);
#endif
	//johnfitz
	//spike -- new cvars...
	Cvar_RegisterVariable (&r_scenecache);
	//spike

	Cvar_RegisterVariable (&cl_damagehue);   // woods #damage
	Cvar_RegisterVariable (&cl_damagehuecolor);   // woods #damage
	Cvar_SetCompletion (&cl_damagehuecolor, &Cl_Damagehue_Completion_f); // woods #iwtabcomplete
	Cvar_RegisterVariable(&cl_autodemo);   // woods #autodemo

	Cvar_RegisterVariable (&gl_zfix); // QuakeSpasm z-fighting fix
	Cvar_RegisterVariable (&r_lavaalpha);
	Cvar_RegisterVariable (&r_telealpha);
	Cvar_RegisterVariable (&r_slimealpha);
	Cvar_RegisterVariable (&r_scale);
	Cvar_SetCallback (&r_lavaalpha, R_SetLavaalpha_f);
	Cvar_SetCallback (&r_telealpha, R_SetTelealpha_f);
	Cvar_SetCallback (&r_slimealpha, R_SetSlimealpha_f);
	Cvar_RegisterVariable(&r_ambient); // woods #rambient
	Cvar_SetCallback(&r_ambient, R_Ambient_OnChange_f); // woods #rambient

	R_InitParticles ();
#ifdef PSET_SCRIPT
	PScript_InitParticles();
#endif
	R_SetClearColor_f (&r_clearcolor); //johnfitz

	TexturePointer_Init (); // woods #texturepointer

	Sky_Init (); //johnfitz
	Fog_Init (); //johnfitz

	R_InitOtherTextures (); // woods #powershell
}

/*
=============
R_ParseWorldspawn

called at map load
=============
*/
static void R_ParseWorldspawn (void)
{
	char key[128], value[4096];
	const char *data;

	map_fallbackalpha = r_wateralpha.value;
	map_wateralpha = (cl.worldmodel->contentstransparent&SURF_DRAWWATER)?r_wateralpha.value:1;
	map_lavaalpha = (cl.worldmodel->contentstransparent&SURF_DRAWLAVA)?r_lavaalpha.value:1;
	map_telealpha = (cl.worldmodel->contentstransparent&SURF_DRAWTELE)?r_telealpha.value:1;
	map_slimealpha = (cl.worldmodel->contentstransparent&SURF_DRAWSLIME)?r_slimealpha.value:1;

	map_ctf_flag_style = 1; // flag style default #alternateflags

	data = COM_Parse(cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
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
		data = COM_Parse(data);
		if (!data)
			return; // error
		q_strlcpy(value, com_token, sizeof(value));

		if (!strcmp("wateralpha", key))
			map_fallbackalpha = map_wateralpha = atof(value);

		if (!strcmp("lavaalpha", key))
			map_lavaalpha = atof(value);

		if (!strcmp("telealpha", key))
			map_telealpha = atof(value);

		if (!strcmp("slimealpha", key))
			map_slimealpha = atof(value);

		if (!strcmp("ctfstyle", key)) // woods lets set whgat flag style we use [ 1 - default, 2 - rogue, 3 - alternate option1, 4 - alternate option2 ] #alternateflags
			map_ctf_flag_style = atof(value);
	}
}


/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;

	for (i=0 ; i<256 ; i++)
		d_lightstylevalue[i] = 264;		// normal light value

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = NULL;
	R_ClearParticles ();
#ifdef PSET_SCRIPT
	PScript_ClearParticles();
#endif

	GL_BuildLightmaps ();
	GL_BuildBModelVertexBuffer ();
	//ericw -- no longer load alias models into a VBO here, it's done in Mod_LoadAliasModel

	r_framecount = 0; //johnfitz -- paranoid?
	r_visframecount = 0; //johnfitz -- paranoid?

	Sky_NewMap (); //johnfitz -- skybox in worldspawn
	Fog_NewMap (); //johnfitz -- global fog in worldspawn
	R_ParseWorldspawn (); //ericw -- wateralpha, lavaalpha, telealpha, slimealpha in worldspawn
	CShift_ParseWorldspawn (); //infin -- cshiftwater, cshiftslime, cshiftlava in worldspawn // woods tag

	LOC_LoadLocations ();//ProQuake   rook / woods #pqteam

	load_subdivide_size = gl_subdivide_size.value; //johnfitz -- is this the right place to set this?
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int		i;
	float		start, stop, time;

	if (cls.state != ca_connected)
	{
		Con_Printf("Not connected to a server\n");
		return;
	}

	start = Sys_DoubleTime ();
	for (i = 0; i < 128; i++)
	{
		GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
		r_refdef.viewangles[1] = i/128.0*360.0;
		R_RenderView ();
		GL_EndRendering ();
	}

	glFinish ();
	stop = Sys_DoubleTime ();
	time = stop-start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128/time);
}

void D_FlushCaches (void)
{
}

static GLuint gl_programs[16];
static int gl_num_programs;

static qboolean GL_CheckShader (GLuint shader)
{
	GLint status;
	GL_GetShaderivFunc (shader, GL_COMPILE_STATUS, &status);

	if (status != GL_TRUE)
	{
		char infolog[1024];

		memset(infolog, 0, sizeof(infolog));
		GL_GetShaderInfoLogFunc (shader, sizeof(infolog), NULL, infolog);

		Con_Warning ("GLSL program failed to compile: %s", infolog);

		return false;
	}
	return true;
}

static qboolean GL_CheckProgram (GLuint program)
{
	GLint status;
	GL_GetProgramivFunc (program, GL_LINK_STATUS, &status);

	if (status != GL_TRUE)
	{
		char infolog[1024];

		memset(infolog, 0, sizeof(infolog));
		GL_GetProgramInfoLogFunc (program, sizeof(infolog), NULL, infolog);

		Con_Warning ("GLSL program failed to link: %s", infolog);

		return false;
	}
	return true;
}

/*
=============
GL_GetUniformLocation
=============
*/
GLint GL_GetUniformLocation (GLuint *programPtr, const char *name)
{
	GLint location;

	if (!*programPtr)
		return -1;

	location = GL_GetUniformLocationFunc(*programPtr, name);
	if (location == -1)
	{
		Con_Warning("GL_GetUniformLocationFunc %s failed\n", name);
		*programPtr = 0;
	}
	return location;
}

/*
====================
GL_CreateProgram

Compiles and returns GLSL program.
====================
*/
GLuint GL_CreateProgram (const GLchar *vertSource, const GLchar *fragSource, int numbindings, const glsl_attrib_binding_t *bindings)
{
	int i;
	GLuint program, vertShader, fragShader;

	if (!gl_glsl_able)
		return 0;

	vertShader = GL_CreateShaderFunc (GL_VERTEX_SHADER);
	GL_ShaderSourceFunc (vertShader, 1, &vertSource, NULL);
	GL_CompileShaderFunc (vertShader);
	if (!GL_CheckShader (vertShader))
	{
		GL_DeleteShaderFunc (vertShader);
		return 0;
	}

	fragShader = GL_CreateShaderFunc (GL_FRAGMENT_SHADER);
	GL_ShaderSourceFunc (fragShader, 1, &fragSource, NULL);
	GL_CompileShaderFunc (fragShader);
	if (!GL_CheckShader (fragShader))
	{
		GL_DeleteShaderFunc (vertShader);
		GL_DeleteShaderFunc (fragShader);
		return 0;
	}

	program = GL_CreateProgramFunc ();
	GL_AttachShaderFunc (program, vertShader);
	GL_DeleteShaderFunc (vertShader);
	GL_AttachShaderFunc (program, fragShader);
	GL_DeleteShaderFunc (fragShader);

	for (i = 0; i < numbindings; i++)
	{
		GL_BindAttribLocationFunc (program, bindings[i].attrib, bindings[i].name);
	}

	GL_LinkProgramFunc (program);

	if (!GL_CheckProgram (program))
	{
		GL_DeleteProgramFunc (program);
		return 0;
	}
	else
	{
		if (gl_num_programs == (sizeof(gl_programs)/sizeof(GLuint)))
			Host_Error ("gl_programs overflow");

		gl_programs[gl_num_programs] = program;
		gl_num_programs++;

		return program;
	}
}

/*
====================
R_DeleteShaders

Deletes any GLSL programs that have been created.
====================
*/
void R_DeleteShaders (void)
{
	int i;

	PolyBlend_DeleteVignetteTexture (); // vignette polyblend cleanup -- woods #polylblend2
	if (!gl_glsl_able)
		return;

	for (i = 0; i < gl_num_programs; i++)
	{
		GL_DeleteProgramFunc (gl_programs[i]);
		gl_programs[i] = 0;
	}
	gl_num_programs = 0;
}

GLuint current_array_buffer, current_element_array_buffer;

/*
====================
GL_BindBuffer

glBindBuffer wrapper
====================
*/
void GL_BindBuffer (GLenum target, GLuint buffer)
{
	GLuint *cache;

	if (!gl_vbo_able)
		return;

	switch (target)
	{
		case GL_ARRAY_BUFFER:
			cache = &current_array_buffer;
			break;
		case GL_ELEMENT_ARRAY_BUFFER:
			cache = &current_element_array_buffer;
			break;
		default:
			Host_Error("GL_BindBuffer: unsupported target %d", (int)target);
			return;
	}

	if (*cache != buffer)
	{
		*cache = buffer;
		GL_BindBufferFunc (target, *cache);
	}
}

/*
====================
GL_ClearBufferBindings

This must be called if you do anything that could make the cached bindings
invalid (e.g. manually binding, destroying the context).
====================
*/
void GL_ClearBufferBindings ()
{
	if (!gl_vbo_able)
		return;

	current_array_buffer = 0;
	current_element_array_buffer = 0;
	GL_BindBufferFunc (GL_ARRAY_BUFFER, 0);
	GL_BindBufferFunc (GL_ELEMENT_ARRAY_BUFFER, 0);
}

/*
===============================================================================
CLIENT-SIDE MODEL ROTATION -- woods #clmrotate
===============================================================================
*/

extern cvar_t cl_rot;

extern int host_framecount; // ensure we only advance rotation once per host frame

#define MAX_ROTATE_MODELS  64
typedef struct {
	char   name[MAX_QPATH];
	vec3_t avel;              // degrees / second
} rotmdl_t;

static rotmdl_t cl_rotatemodels[MAX_ROTATE_MODELS];
static int      cl_rotatemodels_count = 0;

#ifndef VectorClear
#define VectorClear(v) ((v)[0] = (v)[1] = (v)[2] = 0)
#endif

/* Single archived list cvar.
 * Format: "<ax> <ay> <az> <mdl[,mdl2…]>; <ax> <ay> <az> <mdl>; ..."
 * Empty string => no rotations (off). Non-empty => parsed.
 */
void CL_RotateModel_RebuildFromCvar(void);
static void CL_RotateModel_SyncCvar(void);
void CL_RotateModel_OnChange(cvar_t* var);

static qboolean AnglesNearlyZero(const vec3_t a)
{
	const float eps = 0.01f;
	return (fabsf(a[0]) < eps && fabsf(a[1]) < eps && fabsf(a[2]) < eps);
}

static qboolean AnglesNearlyEqual(const vec3_t a, const vec3_t b)
{
	const float eps = 0.01f;
	return (fabsf(a[0] - b[0]) < eps && fabsf(a[1] - b[1]) < eps && fabsf(a[2] - b[2]) < eps);
}

static int CL_FindRotateSlot(const char* mdl)
{
	for (int i = 0; i < cl_rotatemodels_count; i++)
		if (!q_strcasecmp(cl_rotatemodels[i].name, mdl))
			return i;
	return -1;
}

/* Add/replace models from a CSV under one avel vector (ephemeral, runtime only) */
static void CL_AddRotateSpec(const vec3_t avel, const char* model_csv)
{
	char list[1024];
	q_strlcpy(list, model_csv, sizeof(list));

	// Manual parsing to avoid nested strtok() calls which corrupt outer loop state
	char* p = list;
	while (*p) {
		// Skip leading whitespace/commas
		while (*p && (*p == ',' || *p == ' ' || *p == '\t')) p++;
		if (!*p) break;

		// Find end of token
		char* start = p;
		while (*p && *p != ',') p++;

		// Null-terminate and trim trailing whitespace
		char* end = p;
		if (*p) *p++ = '\0';
		end--;
		while (end > start && (*end == ' ' || *end == '\t')) *end-- = '\0';

		if (start[0] == '\0') continue;

		int slot = CL_FindRotateSlot(start);
		if (slot == -1) {
			if (cl_rotatemodels_count >= MAX_ROTATE_MODELS) {
				Con_Printf("rotatemodel: list full\n");
				continue;
			}
			slot = cl_rotatemodels_count++;
			q_strlcpy(cl_rotatemodels[slot].name, start, sizeof(cl_rotatemodels[slot].name));
		}
		VectorCopy(avel, cl_rotatemodels[slot].avel);
	}
}

void CL_RotateModel_f(void)
{
	/* ---- CLEAR / LIST / RELOAD / SAVE ---- */
	if (Cmd_Argc() == 2) {
		const char* arg = Cmd_Argv(1);
		if (!q_strcasecmp(arg, "clear")) {
			cl_rotatemodels_count = 0; /* ephemeral: does not touch cl_rot */
			return;
		}
		if (!q_strcasecmp(arg, "list")) {
			for (int i = 0; i < cl_rotatemodels_count; i++)
				Con_Printf("%3d  %s  (%.1f %.1f %.1f)\n",
					i, cl_rotatemodels[i].name,
					(double)cl_rotatemodels[i].avel[0],
					(double)cl_rotatemodels[i].avel[1],
					(double)cl_rotatemodels[i].avel[2]);
			Con_Printf("cl_rot: %s\n", cl_rot.string[0] ? cl_rot.string : "<empty>");
			return;
		}
		if (!q_strcasecmp(arg, "reload") || !q_strcasecmp(arg, "revert")) {
			CL_RotateModel_RebuildFromCvar();
			Con_Printf("rotatemodel: reloaded from cl_rot\n");
			return;
		}
		if (!q_strcasecmp(arg, "save")) {
			CL_RotateModel_SyncCvar();
			Con_Printf("rotatemodel: saved to cl_rot\n");
			return;
		}
	}

	if (Cmd_Argc() < 5) {
		Con_Printf(
			"rotatemodel - add/inspect client-side spin for specific models\n"
			"\n"
			"Usage:\n"
			"  rotatemodel <ax> <ay> <az> <mdl1[,mdl2...]>\n"
			"  rotatemodel clear | list | reload | save\n"
			"\n"
			"Notes:\n"
			"  - <ax,ay,az> are degrees/second around X,Y,Z (right, up, forward).\n"
			"  - Multiple models can be comma-separated; names must match model->name.\n"
			"  - Player models are ignored by design (no spinning for players).\n"
			"  - Changes are ephemeral until you \"save\" (persisted in cvar: cl_rot).\n"
			"\n"
			"Examples:\n"
			"  rotatemodel 0 180 0 progs/armor.mdl\n"
			"     Spins the armor around Y at 180 degreess.\n"
			"\n"
			"  rotatemodel 0 90 0 progs/backpack.mdl,progs/g_shot.mdl\n"
			"     Spins backpack and shotgun pickup at 90 degreess around Y.\n"
			"\n"
			"  rotatemodel list\n"
			"     Shows the current runtime list and the cl_rot string.\n"
			"\n"
			"  rotatemodel save\n"
			"     Writes the current runtime list to cl_rot so it persists.\n"
			"\n"
			"cl_rot format (for autoexec.cfg etc):\n"
			"  \"0 180 0 progs/armor.mdl; 0 90 0 progs/backpack.mdl\"\n\n"
		);
		return;
	}

	vec3_t avel = { Q_atof(Cmd_Argv(1)),
					Q_atof(Cmd_Argv(2)),
					Q_atof(Cmd_Argv(3)) };

	/* Ephemeral edit: live list only; does NOT modify cl_rot unless you 'save' */
	CL_AddRotateSpec(avel, Cmd_Argv(4));
}

qboolean CL_ApplyModelRotation(entity_t* ent, vec3_t angles, float dt) // true if we touched *angles (for caller-side bookkeeping), false unchanged
{
	if (!ent) {
		return false;
	}
	if (!ent->model || !ent->model->name[0]) {
		return false;
	}

	/*---------------------------------------------------------------
	*  PER-ENTITY RESET: if the server has swapped in a different
	*  model (or removed the model entirely) we wipe all rotation
	*  bookkeeping so this slot starts fresh.
	*--------------------------------------------------------------*/
	if (ent->model != ent->rot_prev_model)
	{
		ent->rot_prev_model = ent->model;   /* remember new model    */
		ent->rot_started = false;
		ent->rot_nonzero_seen = false;
		ent->rot_frozen = false;
		VectorClear(ent->rot_base);
		VectorClear(ent->rot_last);
		VectorClear(ent->rot_final);
		ent->rot_last_host_framecount = -1;
	}

	/* ---------- RE-USE DETECTOR ---------------------------------- */
	if (ent->rot_frozen &&
		!AnglesNearlyZero(ent->netstate.angles))
	{
		ent->rot_frozen = false;
		ent->rot_started = false;
		ent->rot_nonzero_seen = false;
		VectorClear(ent->rot_base);
		VectorClear(ent->rot_last);
		VectorClear(ent->rot_final);
		ent->rot_last_host_framecount = -1;
	}

	/* ---------- Already frozen? ---------------------------------- */
	if (ent->rot_frozen)
	{
		VectorCopy(ent->rot_final, angles);
		return true;
	}

	if (!AnglesNearlyZero(ent->netstate.angles)) // Track whether we have ever seen non-zero baseline
		ent->rot_nonzero_seen = true;

	if (ent->rot_started && // Freeze when: spun once, baseline used to be non-zero, now zero
		ent->rot_nonzero_seen &&
		AnglesNearlyZero(ent->netstate.angles))
	{
		ent->rot_frozen = true;
		VectorCopy(ent->rot_last, ent->rot_final);
		VectorCopy(ent->rot_final, angles);
		return true;
	}

	/* If we already advanced this entity this frame, just reuse last value */
	if (ent->rot_started && ent->rot_last_host_framecount == host_framecount)
	{
		VectorCopy(ent->rot_last, angles);
		return true;
	}

	if (strstr(ent->model->name, "player.mdl") || strstr(ent->model->name, "/player.mdl")) // Player models never spin
		return false;

	for (int i = 0; i < cl_rotatemodels_count; ++i) // Apply spin for whitelisted models
	{
		if (!q_strcasecmp(cl_rotatemodels[i].name, ent->model->name))
		{
			/* ---------------------------------------------------------
			 *  (re)initialise if we have   - never spun before
			 *                           or - QC just supplied new angles
			 * --------------------------------------------------------- */
			if (!ent->rot_started ||
				!AnglesNearlyEqual(ent->rot_base, ent->netstate.angles))
			{
				VectorCopy(ent->netstate.angles, ent->rot_base);
				VectorCopy(ent->rot_base, ent->rot_last);
				ent->rot_started = true;
				ent->rot_last_host_framecount = host_framecount; // treat as handled this frame
				// first frame > no spin yet
			}
			else
			{
				VectorMA(ent->rot_last, dt, // accumulate per-frame spin
					cl_rotatemodels[i].avel,
					ent->rot_last);
				ent->rot_last_host_framecount = host_framecount; // advance only once per frame
			}

			VectorCopy(ent->rot_last, angles); // publish final orientation to caller
			return true;
		}
	}

	return false; // not on rotate list
}

/* --------------------- Cvar <-> List bridging --------------------- */
void CL_RotateModel_RebuildFromCvar(void)
{
	cl_rotatemodels_count = 0;

	const char* s = cl_rot.string;
	if (!s || !*s) return; // empty list => off

	char buf[2048];
	q_strlcpy(buf, s, sizeof(buf));

	// entries separated by ';'
	for (char* entry = strtok(buf, ";"); entry; entry = strtok(NULL, ";")) {
		while (*entry && isspace((unsigned char)*entry)) entry++;
		if (!*entry) continue;

		// Parse "<ax> <ay> <az> <mdl[,mdl2…]>"
		char* p = entry, * endptr = NULL;
		vec3_t avel = { 0,0,0 };
		avel[0] = (float)strtod(p, &endptr); if (endptr == p) continue; p = endptr;
		avel[1] = (float)strtod(p, &endptr); if (endptr == p) continue; p = endptr;
		avel[2] = (float)strtod(p, &endptr); if (endptr == p) continue; p = endptr;
		while (*p && isspace((unsigned char)*p)) p++;
		if (!*p) continue;

		CL_AddRotateSpec(avel, p);
	}
}

static void CL_RotateModel_SyncCvar(void)
{
	char out[2048] = { 0 };
	for (int i = 0; i < cl_rotatemodels_count; ++i) {
		const rotmdl_t* r = &cl_rotatemodels[i];
		char line[256];
		q_snprintf(line, sizeof(line), "%.3f %.3f %.3f %s; ",
			r->avel[0], r->avel[1], r->avel[2], r->name);
		if (strlen(out) + strlen(line) + 1 >= sizeof(out))
			break;
		q_strlcat(out, line, sizeof(out));
	}
	Cvar_Set("cl_rot", out);
}

/* Re-parse list whenever cl_rot changes (e.g., via config/console) */
void CL_RotateModel_OnChange(cvar_t* var)
{
	CL_RotateModel_RebuildFromCvar();
}

/* Call once during client init (e.g., SCR_Init or CL_Init) */
void CL_RotateModel_CvarsInit(void)
{
	Cvar_RegisterVariable(&cl_rot);
	CL_RotateModel_RebuildFromCvar(); // parse cl_rot once at client boot
}