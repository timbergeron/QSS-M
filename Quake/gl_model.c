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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"
#include "f_modified.h"

qmodel_t	*loadmodel;
char	loadname[32];	// for hunk tags
char	diskname[MAX_QPATH];	// for loading related name-based files.

static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer);
static void Mod_LoadBrushModel (qmodel_t *mod, void *buffer);
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer, int pvtype);
void Mod_LoadMD3Model (qmodel_t *mod, void *buffer);
void Mod_LoadMD5MeshModel (qmodel_t *mod, void *buffer);
void Mod_LoadIQMModel (qmodel_t *mod, const void *buffer);
static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash);
static void Q1BSPX_Reset (void);

static void Mod_Print (void);

static cvar_t	external_ents = {"external_ents", "1", CVAR_ARCHIVE};
static cvar_t	external_ents_dir = {"external_ents_dir", "", CVAR_ARCHIVE};
static cvar_t   external_lits_dir = {"external_lits_dir", "", CVAR_ARCHIVE}; // woods #litdir
cvar_t	gl_load24bit = {"gl_load24bit", "1", CVAR_ARCHIVE};
static cvar_t	mod_ignorelmscale = {"mod_ignorelmscale", "0"};
static cvar_t	mod_lightscale_broken = {"mod_lightscale_broken", "1"};	//match vanilla's brokenness bug with dlights and scaled textures. decoupled_lm bypasses this obviously buggy setting because zomgletmefixstuffffs
cvar_t	mod_lightgrid = {"mod_lightgrid", "1"};	//mostly for debugging, I dunno. just leave it set to 1.
cvar_t	r_replacemodels = {"r_replacemodels", "", CVAR_ARCHIVE};
static cvar_t	external_vis = {"external_vis", "1", CVAR_ARCHIVE};

cvar_t	gl_loadlitfiles = {"gl_loadlitfiles", "1", CVAR_ARCHIVE}; // woods #loadlits
cvar_t gl_load24bit_skins = {"gl_load24bit_skins", "0", CVAR_ARCHIVE }; // woods #loadskins
cvar_t gl_load24bit_hud = {"gl_load24bit_hud", "1", CVAR_ARCHIVE}; // woods #24bithud
void Cache_Flush_f (cvar_t* var); // woods #loadskins

cvar_t	scr_concolor = {"scr_concolor", "", CVAR_ARCHIVE}; // woods #concolor
cvar_t	scr_conback = {"scr_conback", "", CVAR_ARCHIVE}; // woods #conback

extern cvar_t	r_fastturb; // woods #fastturb

static byte	*mod_novis;
static int	mod_novis_capacity;

static byte	*mod_decompressed;
static int	mod_decompressed_capacity;

#define	MAX_MOD_KNOWN	8192 /*spike -- new value, was 2048 in qs, 512 in vanilla. Needs to be big for big maps with many many inline models. */
static qmodel_t	mod_known[MAX_MOD_KNOWN];
static int		mod_numknown;

texture_t	*r_notexture_mip; //johnfitz -- moved here from r_main.c
texture_t	*r_notexture_mip2; //johnfitz -- used for non-lightmapped surfs with a missing texture

/*
===============
Console_Color_Completion_f -- woods #iwtabcomplete
===============
*/
static void Console_Color_Completion_f(cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("0x000000", partial, "black", NULL); // #demolistsort add arg
	Con_AddToTabList("0x1e1e1e", partial, "dark grey", NULL); // #demolistsort add arg
	Con_AddToTabList("0x2c190c", partial, "quake brown", NULL); // #demolistsort add arg

	return;
}

/*
===============
Console_Background_Completion_f -- woods #iwtabcomplete
===============
*/
static void Console_Background_Completion_f(cvar_t* cvar, const char* partial)
{
	searchpath_t *search;

	Con_AddToTabList("\"\"", partial, "default", NULL); // #demolistsort add arg

	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
			Con_AddToTabList(COM_SkipPath(search->pack->filename), partial, "package conback.lmp", NULL); // #demolistsort add arg
	}

	return;
}

/*
===============
R_ReplaceModels_Completion_f -- woods #iwtabcomplete
===============
*/
static void R_ReplaceModels_Completion_f(cvar_t* cvar, const char* partial)
{
	Con_AddToTabList("\"\"", partial, "disabled", NULL);
	Con_AddToTabList("\"iqm md5mesh md3\"", partial, "all supported", NULL);
	Con_AddToTabList("iqm", partial, "Inter-Quake Model", NULL);
	Con_AddToTabList("md5mesh", partial, "Doom 3 MD5 mesh", NULL);
	Con_AddToTabList("md3", partial, "Quake 3 MD3", NULL);
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&gl_subdivide_size);
	Cvar_RegisterVariable (&external_vis);
	Cvar_RegisterVariable (&external_ents);
	Cvar_RegisterVariable (&external_ents_dir);
	Cvar_RegisterVariable (&external_lits_dir); // woods #litdir
	Cvar_RegisterVariable (&gl_load24bit);
	Cvar_RegisterVariable (&r_replacemodels);
	Cvar_SetCompletion (&r_replacemodels, &R_ReplaceModels_Completion_f); // woods #iwtabcomplete
	Cvar_RegisterVariable (&mod_ignorelmscale);
	Cvar_RegisterVariable (&gl_loadlitfiles); // woods #loadlits
	Cvar_RegisterVariable (&gl_load24bit_skins); // woods #loadskins
	Cvar_SetCallback (&gl_load24bit_skins, Cache_Flush_f); // woods #loadskins
	Cvar_RegisterVariable (&gl_load24bit_hud); // woods #24bithud
	Cvar_RegisterVariable (&mod_lightscale_broken);
	Cvar_RegisterVariable (&mod_lightgrid);
	Cvar_RegisterVariable (&scr_concolor); // woods #concolor
	Cvar_SetCompletion (&scr_concolor, &Console_Color_Completion_f); // woods #iwtabcomplete
	Cvar_RegisterVariable (&scr_conback); // woods #conback
	Cvar_SetCompletion (&scr_conback, &Console_Background_Completion_f); // woods #iwtabcomplete


	Cmd_AddCommand ("mcache", Mod_Print);

	//johnfitz -- create notexture miptex
	r_notexture_mip = (texture_t *) Hunk_AllocName (sizeof(texture_t), "r_notexture_mip");
	strcpy (r_notexture_mip->name, "notexture");
	r_notexture_mip->height = r_notexture_mip->width = 32;

	r_notexture_mip2 = (texture_t *) Hunk_AllocName (sizeof(texture_t), "r_notexture_mip2");
	strcpy (r_notexture_mip2->name, "notexture2");
	r_notexture_mip2->height = r_notexture_mip2->width = 32;
	//johnfitz
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void *Mod_Extradata (qmodel_t *mod)
{
	void	*r;

	r = Cache_Check (&mod->cache);
	if (r)
		return r;

	Mod_LoadModel (mod, true);

	if (!mod->cache.data)
		Sys_Error ("Mod_Extradata: caching failed");
	return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, qmodel_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;
	int		remaining;

	if (!model || !model->nodes || model->numnodes <= 0 ||
		model->hulls[0].firstclipnode < 0 ||
		model->hulls[0].firstclipnode >= model->numnodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes + model->hulls[0].firstclipnode;
	remaining = model->numnodes + 1;
	while (node && remaining-- > 0)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		if (!plane)
			break;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	if (model->leafs)
		return model->leafs;	// malformed tree: use the solid leaf
	Sys_Error ("Mod_PointInLeaf: bad node tree");
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
static void Mod_DecompressVis_VisError (qmodel_t *model, const char *reason)
{
	if (!developer.value || model->viswarn)
		return;

	model->viswarn = true;
	Con_DPrintf ("Mod_DecompressVis: %s on model \"%s\"\n", reason, model->name);
}

static byte *Mod_DecompressVis (byte *in, qmodel_t *model)
{
	int		c;
	byte	*out, *inend;
	byte	*outend;
	int		row;

	row = (model->numleafs+7)>>3;
	if (mod_decompressed == NULL || row > mod_decompressed_capacity)
	{
		mod_decompressed_capacity = row;
		mod_decompressed = (byte *) realloc (mod_decompressed, mod_decompressed_capacity);
		if (!mod_decompressed)
			Sys_Error ("Mod_DecompressVis: realloc() failed on %d bytes", mod_decompressed_capacity);
	}
	out = mod_decompressed;
	outend = mod_decompressed + row;

	if (in && model->visdata && model->visdata_size > 0)
	{
		inend = model->visdata + model->visdata_size;
		if (in < model->visdata || in >= inend)
		{
			Mod_DecompressVis_VisError (model, "input offset out of range");
			in = NULL;
		}
	}
	else
		inend = NULL;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return mod_decompressed;
	}

	do
	{
		if (inend && in >= inend)
		{
			Mod_DecompressVis_VisError (model, "input underrun");
			memset (out, 0xff, outend - out);
			return mod_decompressed;
		}

		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		if (inend && in + 1 >= inend)
		{
			Mod_DecompressVis_VisError (model, "input underrun");
			memset (out, 0xff, outend - out);
			return mod_decompressed;
		}

		c = in[1];
		in += 2;
		if (!c)
		{
			Mod_DecompressVis_VisError (model, "zero run length");
			memset (out, 0xff, outend - out);
			return mod_decompressed;
		}
		if (c > row - (out - mod_decompressed))
			c = (int)(row - (out - mod_decompressed));	//now that we're dynamically allocating pvs buffers, we have to be more careful to avoid heap overflows with buggy maps.
		while (c)
		{
			if (out == outend)
			{
				Mod_DecompressVis_VisError (model, "output overrun");
				return mod_decompressed;
			}
			*out++ = 0;
			c--;
		}
	} while (out - mod_decompressed < row);

	return mod_decompressed;
}

byte *Mod_LeafPVS (mleaf_t *leaf, qmodel_t *model)
{
	if (leaf == model->leafs)
		return Mod_NoVisPVS (model);
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

byte *Mod_NoVisPVS (qmodel_t *model)
{
	int pvsbytes;
 
	pvsbytes = (model->numleafs+7)>>3;
	pvsbytes = (pvsbytes+3)&~3;
	if (mod_novis == NULL || pvsbytes > mod_novis_capacity)
	{
		mod_novis_capacity = pvsbytes;
		mod_novis = (byte *) realloc (mod_novis, mod_novis_capacity);
		if (!mod_novis)
			Sys_Error ("Mod_NoVisPVS: realloc() failed on %d bytes", mod_novis_capacity);
		
		memset(mod_novis, 0xff, mod_novis_capacity);
	}
	return mod_novis;
}

static byte *Mod_LeafVisData (int visofs)
{
	if (visofs == -1)
		return NULL;

	if (!loadmodel->visdata || loadmodel->visdata_size <= 0 ||
		visofs < 0 || visofs >= loadmodel->visdata_size)
	{
		Mod_DecompressVis_VisError (loadmodel, "leaf VIS offset out of range");
		return NULL;
	}

	return loadmodel->visdata + visofs;
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		i;
	qmodel_t	*mod;

	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (mod->type != mod_alias)
		{
			mod->needload = true;
			TexMgr_FreeTexturesForOwner (mod); //johnfitz
			PScript_ClearSurfaceParticles(mod);
			RSceneCache_Cleanup(mod);
			R_BModelDrawCache_Cleanup(mod);
			R_GrassCache_Cleanup(mod);
		}
	}

	InvalidateTraceLineCache();
}

void Mod_ResetAll (void)
{
	int		i;
	qmodel_t	*mod;

	//ericw -- free alias model VBOs
	GLMesh_DeleteVertexBuffers ();

	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->needload) //otherwise Mod_ClearAll() did it already
		{
			TexMgr_FreeTexturesForOwner (mod);
			PScript_ClearSurfaceParticles(mod);
			RSceneCache_Cleanup(mod);
			R_BModelDrawCache_Cleanup(mod);
			R_GrassCache_Cleanup(mod);
		}
		memset(mod, 0, sizeof(qmodel_t));
	}
	mod_numknown = 0;

	InvalidateTraceLineCache();
}

void	Mod_ForEachModel(void(*callback)(qmodel_t *mod))
{
	int i;
	qmodel_t	*mod;
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		callback(mod);
	}
}

/*
==================
Mod_FindName

==================
*/
static qmodel_t *Mod_FindName (const char *name)
{
	int		i;
	qmodel_t	*mod;

	if (!name[0])
		Sys_Error ("Mod_FindName: NULL name"); //johnfitz -- was "Mod_ForName"

//
// search the currently loaded models
//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!strcmp (mod->name, name) )
			break;
	}

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		q_strlcpy (mod->name, name, MAX_QPATH);
		mod->needload = true;
		mod_numknown++;
		InvalidateTraceLineCache();
	}

	return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (const char *name)
{
	qmodel_t	*mod;

	mod = Mod_FindName (name);

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
			Cache_Check (&mod->cache);
	}
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
double mod_load_total_time, mod_load_read_time;	//tb -- load profiling
unsigned int mod_load_calls;

static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash)
{
	byte	*buf;
	int	mod_type;
	qofs_t model_filelen = 0;
	double	t_start, t_read, t_done;
	qboolean profile;

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
		{
			if (Cache_Check (&mod->cache))
				return mod;
		}
		else
			return mod;		// not cached at all
	}

	InvalidateTraceLineCache();

	profile = developer.value != 0;
	t_start = profile ? Sys_DoubleTime () : 0;

//
// load the file
//
	if (*mod->name == '*')
		buf = NULL;
	else
	{
		const char *exts = r_replacemodels.string;
		char *e;
		char newname[MAX_QPATH];
		unsigned int origpathid;
		buf = NULL;
		q_strlcpy(newname, mod->name, sizeof(newname));
		e = (char*)COM_FileGetExtension(newname);
		if (*e) while ((exts = COM_Parse(exts)))
		{
			q_strlcpy(e, com_token, sizeof(newname)-(e-newname));
			buf = COM_LoadMallocFile (newname, & mod->path_id);
			if (buf)
			{
				model_filelen = com_filesize;
				if (COM_FileExists(mod->name, &origpathid))
					if (origpathid > mod->path_id)
					{
						Con_DPrintf("Ignoring %s from lower priority path\n", newname);
						free (buf);
						buf = NULL;
						continue;
					}
				memcpy(diskname, newname, sizeof(newname));
				break;
			}
		}
		if (!buf)
		{
			memcpy(diskname, mod->name, sizeof(mod->name));
			buf = COM_LoadMallocFile (mod->name, & mod->path_id);
			if (buf)
				model_filelen = com_filesize;
		}
	}
	if (!buf)
	{
		if (crash)
			Host_Error ("Mod_LoadModel: %s not found", mod->name); //johnfitz -- was "Mod_NumForName"
		else if (mod->name[0] == '*' && (mod->name[1] < '0' || mod->name[1] > '9'))
			;	//*foo doesn't warn, unless its *NUM. inline models. gah.
		else if (cl.suppress_precache_miss_warnings)
			cl.suppressed_model_precache_warnings++;
		else
			Con_Warning("Mod_LoadModel: %s not found\n", mod->name);

		//avoid crashes
		mod->needload = false;
		mod->type = mod_ext_invalid;
		mod->flags = 0;

		Mod_SetExtraFlags (mod); //johnfitz. spike -- moved this to be generic, because most of the flags are anyway.
		return mod;
	}
	com_filesize = model_filelen;
	t_read = profile ? Sys_DoubleTime () : 0;
	FMod_CheckModel (mod->name, buf, (model_filelen > 0) ? (size_t)model_filelen : 0);

//
// allocate a new model
//
	COM_FileBase (mod->name, loadname, sizeof(loadname));

	loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
	mod->needload = false;

	if (com_filesize < 4)
	{
		Con_Warning("%s is too small to be a model\n", mod->name);
		free (buf);
		Q1BSPX_Reset();
		mod->type = mod_ext_invalid;
		mod->flags = 0;
		Mod_SetExtraFlags (mod);
		return mod;
	}

	mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
	switch (mod_type)
	{
	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf, PV_QUAKE1);
		break;
	case (('M'<<0)+('D'<<8)+('1'<<16)+('6'<<24)):	//QF 16bit variation
		Mod_LoadAliasModel (mod, buf, PV_QUAKEFORGE);
		break;

	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;

	//Spike -- md3 support
	case (('I'<<0)+('D'<<8)+('P'<<16)+('3'<<24)):	//md3
		Mod_LoadMD3Model(mod, buf);
		break;

	//Spike -- md5 support
	case (('M'<<0)+('D'<<8)+('5'<<16)+('V'<<24)):
		Mod_LoadMD5MeshModel(mod, buf);
		break;

	//Spike -- iqm support
	case (('I'<<0)+('N'<<8)+('T'<<16)+('E'<<24)):	//iqm
		Mod_LoadIQMModel(mod, buf);
		break;

	//Spike -- added checks for a few other model types.
	//this is useful because of the number of models with renamed extensions.
	//that and its hard to test the extension stuff when this was crashing.
	case (('R'<<0)+('A'<<8)+('P'<<16)+('O'<<24)):	//h2mp
		Con_Warning("%s is a hexen2-missionpack model (unsupported)\n", mod->name);
		mod->type = mod_ext_invalid;
		break;
	case (('I'<<0)+('D'<<8)+('P'<<16)+('2'<<24)):	//md2
		Con_Warning("%s is an md2 (unsupported)\n", mod->name);
		mod->type = mod_ext_invalid;
		break;
	case (('D'<<0)+('A'<<8)+('R'<<16)+('K'<<24)):	//dpm
		Con_Warning("%s is an dpm (unsupported)\n", mod->name);
		mod->type = mod_ext_invalid;
		break;
	case (('A'<<0)+('C'<<8)+('T'<<16)+('R'<<24)):	//psk
		Con_Warning("%s is a psk (unsupported)\n", mod->name);
		mod->type = mod_ext_invalid;
		break;
	case (('I'<<0)+('B'<<8)+('S'<<16)+('P'<<24)):	//q2/q3bsp
		Con_Warning("%s is a q2/q3bsp (unsupported)\n", mod->name);
		mod->type = mod_ext_invalid;
		break;

	default:
		Mod_LoadBrushModel (mod, buf);
		break;
	}

	free (buf);
	Q1BSPX_Reset ();

	if (crash && mod->type == mod_ext_invalid)
	{	//any of those formats for a world map will be screwed up.
		Sys_Error ("Mod_LoadModel: couldn't load %s", mod->name); //johnfitz -- was "Mod_NumForName"
		return NULL;
	}

	Mod_SetExtraFlags (mod); //johnfitz. spike -- moved this to be generic, because most of the flags are anyway.

	if (profile)
	{
		t_done = Sys_DoubleTime ();
		mod_load_total_time += t_done - t_start;
		mod_load_read_time += t_read - t_start;
		mod_load_calls++;
		if (t_done - t_start > 0.005)
			Con_DPrintf ("Mod_LoadModel %s: read %.1fms parse %.1fms (%u bytes)\n",
				mod->name, (t_read-t_start)*1000.0, (t_done-t_read)*1000.0, (unsigned int)model_filelen);
	}

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
qmodel_t *Mod_ForName (const char *name, qboolean crash)
{
	qmodel_t	*mod;

	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

static byte	*mod_base;


typedef struct {
    char lumpname[24]; // up to 23 chars, zero-padded
    int fileofs;  // from file start
    int filelen;
} bspx_lump_t;
typedef struct {
    char id[4];  // 'BSPX'
    int numlumps;
	bspx_lump_t lumps[1];
} bspx_header_t;
static char *bspxbase;
static bspx_header_t *bspxheader;
//supported lumps:
//RGBLIGHTING (.lit)
//LMSHIFT (.lit2)
//LMOFFSET (LMSHIFT helper)
//LMSTYLE (LMSHIFT helper)

//unsupported lumps ('documented' elsewhere):
//BRUSHLIST (because hulls suck)
//LIGHTINGDIR (.lux)
//LIGHTING_E5BGR9 (hdr lighting)
//VERTEXNORMALS (smooth shading with dlights/rtlights)
static void *Q1BSPX_FindLump(char *lumpname, int *lumpsize)
{
	int i;
	*lumpsize = 0;
	if (!bspxheader)
		return NULL;

	for (i = 0; i < bspxheader->numlumps; i++)
	{
		if (!strncmp(bspxheader->lumps[i].lumpname, lumpname, 24))
		{
			*lumpsize = bspxheader->lumps[i].filelen;
			return bspxbase + bspxheader->lumps[i].fileofs;
		}
	}
	return NULL;
}
static void Q1BSPX_Setup(qmodel_t *mod, char *filebase, unsigned int filelen, lump_t *lumps, int numlumps)
{
	int i;
	unsigned int offs = 0;
	bspx_header_t *h;
	qboolean misaligned = false;

	bspxbase = filebase;
	bspxheader = NULL;

	for (i = 0; i < numlumps; i++, lumps++)
	{
		if ((lumps->fileofs & 3) && i != LUMP_ENTITIES)
			misaligned = true;
		if (offs < lumps->fileofs + lumps->filelen)
			offs = lumps->fileofs + lumps->filelen;
	}
	if (misaligned)
		Con_DWarning("%s contains misaligned lumps\n", mod->name);
	offs = (offs + 3) & ~3;
	if (offs + sizeof(*bspxheader) > filelen)
		return; /*no space for it*/
	h = (bspx_header_t*)(filebase + offs);

	i = LittleLong(h->numlumps);
	/*verify the header*/
	if (strncmp(h->id, "BSPX", 4) ||
		i < 0 ||
		offs + sizeof(*h) + sizeof(h->lumps[0])*(i-1) > filelen)
		return;
	h->numlumps = i;
	while(i-->0)
	{
		h->lumps[i].fileofs = LittleLong(h->lumps[i].fileofs);
		h->lumps[i].filelen = LittleLong(h->lumps[i].filelen);
		if (h->lumps[i].fileofs & 3)
			Con_DWarning("%s contains misaligned bspx limp %s\n", mod->name, h->lumps[i].lumpname);
		if ((unsigned int)h->lumps[i].fileofs + (unsigned int)h->lumps[i].filelen > filelen)
			return;
	}

	bspxheader = h;
}

static void Q1BSPX_Reset (void)
{
	bspxbase = NULL;
	bspxheader = NULL;
}

/*
=================
Mod_CheckFullbrights -- johnfitz
=================
*/
static qboolean Mod_CheckFullbrights (byte *pixels, int count)
{
	int i;
	for (i = 0; i < count; i++)
	{
		if (*pixels++ > 223)
			return true;
	}
	return false;
}

static texliquid_t Mod_TextureLiquidType (const char *name)
{
	const char *checkname;

	if (!name || !name[0])
		return TEXLIQUID_NONE;

	checkname = name;
	if (checkname[0] != '*' && checkname[0] != '!')
		return TEXLIQUID_NONE;
	checkname++;

	if (!q_strncasecmp(checkname, "lava", 4))
		return TEXLIQUID_LAVA;
	if (!q_strncasecmp(checkname, "slime", 5))
		return TEXLIQUID_SLIME;
	if (!q_strncasecmp(checkname, "tele", 4))
		return TEXLIQUID_NONE;

	return TEXLIQUID_WATER;
}

static qboolean Mod_IsLiquidColorPixel (byte r, byte g, byte b, byte a)
{
	int maxc = q_max((int)r, q_max((int)g, (int)b));

	return a >= 128 && maxc >= 16;
}

typedef struct
{
	unsigned int count;
	unsigned int sum_r;
	unsigned int sum_g;
	unsigned int sum_b;
} liquid_color_bucket_t;

static void Mod_AddLiquidColorBucket (liquid_color_bucket_t *bucket, const byte *rgba)
{
	bucket->count++;
	bucket->sum_r += rgba[0];
	bucket->sum_g += rgba[1];
	bucket->sum_b += rgba[2];
}

static int Mod_BestLiquidColorBucket (const liquid_color_bucket_t *buckets, size_t bucket_count)
{
	double best_score = 0.0;
	int best_bucket = -1;
	size_t i;

	for (i = 0; i < bucket_count; i++)
	{
		unsigned int count = buckets[i].count;
		int r, g, b, value;
		double score;

		if (!count)
			continue;

		r = (int)((buckets[i].sum_r + count / 2) / count);
		g = (int)((buckets[i].sum_g + count / 2) / count);
		b = (int)((buckets[i].sum_b + count / 2) / count);
		value = q_max(r, q_max(g, b));
		score = (double)count * (64.0 + (double)value);

		if (score > best_score)
		{
			best_score = score;
			best_bucket = (int)i;
		}
	}

	return best_bucket;
}

static void Mod_SetLiquidTextureColor (texture_t *tx, double rsum, double gsum, double bsum, size_t count)
{
	float peak, min_peak, scale;

	if (!count)
		return;

	tx->liquid_color[0] = (float)(rsum / count) * (1.0f / 255.0f);
	tx->liquid_color[1] = (float)(gsum / count) * (1.0f / 255.0f);
	tx->liquid_color[2] = (float)(bsum / count) * (1.0f / 255.0f);

	peak = q_max(tx->liquid_color[0], q_max(tx->liquid_color[1], tx->liquid_color[2]));
	switch (tx->liquid_type)
	{
	case TEXLIQUID_LAVA:
		min_peak = 0.78f;
		break;
	case TEXLIQUID_SLIME:
		min_peak = 0.38f;
		break;
	default:
		min_peak = 0.42f;
		break;
	}

	if (peak > 0.001f && peak < min_peak)
	{
		scale = min_peak / peak;
		tx->liquid_color[0] *= scale;
		tx->liquid_color[1] *= scale;
		tx->liquid_color[2] *= scale;
	}

	tx->liquid_color[0] = CLAMP(0.0f, tx->liquid_color[0], 1.0f);
	tx->liquid_color[1] = CLAMP(0.0f, tx->liquid_color[1], 1.0f);
	tx->liquid_color[2] = CLAMP(0.0f, tx->liquid_color[2], 1.0f);
	tx->liquid_color_valid = true;
}

static void Mod_DetectLiquidTextureIndexed (texture_t *tx, const byte *data, unsigned int width, unsigned int height)
{
	size_t i, pixels, step, tested;
	liquid_color_bucket_t buckets[256];
	int best_index;
	qboolean allow_fullbright = (tx->liquid_type == TEXLIQUID_LAVA); // lava lives in the fullbright palette range

	if (!data || !width || !height)
		return;

	pixels = (size_t)width * (size_t)height;
	step = pixels > 65536 ? pixels / 65536 : 1;
	tested = 0;
	best_index = -1;
	memset(buckets, 0, sizeof(buckets));

	for (i = 0; i < pixels; i += step)
	{
		byte index = data[i];
		byte *rgba = (byte *)&d_8to24table[index];

		if ((index > 223 && !allow_fullbright) || !Mod_IsLiquidColorPixel(rgba[0], rgba[1], rgba[2], rgba[3]))
			continue;

		tested++;
		Mod_AddLiquidColorBucket(&buckets[index], rgba);
	}

	if (tested >= 8)
	{
		best_index = Mod_BestLiquidColorBucket(buckets, Q_COUNTOF(buckets));
		if (best_index >= 0)
			Mod_SetLiquidTextureColor(tx, buckets[best_index].sum_r, buckets[best_index].sum_g, buckets[best_index].sum_b, buckets[best_index].count);
	}
}

static void Mod_DetectLiquidTextureRGBA (texture_t *tx, const byte *data, unsigned int width, unsigned int height)
{
	enum { bucket_count = 16 * 16 * 16 };
	size_t i, pixels, step, tested;
	liquid_color_bucket_t *buckets;
	int best_bucket;

	if (!data || !width || !height)
		return;

	buckets = (liquid_color_bucket_t *)calloc(bucket_count, sizeof(*buckets));
	if (!buckets)
		return;

	pixels = (size_t)width * (size_t)height;
	step = pixels > 65536 ? pixels / 65536 : 1;
	tested = 0;
	best_bucket = -1;

	for (i = 0; i < pixels; i += step)
	{
		const byte *rgba = data + i * 4;
		int bucket;

		if (!Mod_IsLiquidColorPixel(rgba[0], rgba[1], rgba[2], rgba[3]))
			continue;

		tested++;
		bucket = ((rgba[0] >> 4) << 8) | ((rgba[1] >> 4) << 4) | (rgba[2] >> 4);
		Mod_AddLiquidColorBucket(&buckets[bucket], rgba);
	}

	if (tested >= 8)
	{
		best_bucket = Mod_BestLiquidColorBucket(buckets, bucket_count);
		if (best_bucket >= 0)
			Mod_SetLiquidTextureColor(tx, buckets[best_bucket].sum_r, buckets[best_bucket].sum_g, buckets[best_bucket].sum_b, buckets[best_bucket].count);
	}

	free(buckets);
}

static void Mod_DetectLiquidTexture (texture_t *tx, enum srcformat fmt, const byte *data, unsigned int width, unsigned int height)
{
	tx->liquid_type = Mod_TextureLiquidType(tx->name);
	tx->liquid_color_valid = false;
	tx->liquid_color[0] = 0.0f;
	tx->liquid_color[1] = 0.0f;
	tx->liquid_color[2] = 0.0f;

	if (tx->liquid_type == TEXLIQUID_NONE)
		return;

	switch (fmt)
	{
	case SRC_INDEXED:
		Mod_DetectLiquidTextureIndexed(tx, data, width, height);
		break;
	case SRC_RGBA:
		Mod_DetectLiquidTextureRGBA(tx, data, width, height);
		break;
	default:
		break;
	}
}

static qboolean Mod_IsGrassGreenPixel (byte r, byte g, byte b, byte a)
{
	if (a < 128 || g < 36)
		return false;
	if ((int)g + 10 < (int)r)
		return false;
	if ((int)g < (int)b + 8)
		return false;
	if ((int)g * 5 < ((int)r + (int)b) * 2)
		return false;
	return true;
}

static void Mod_SetGrassTextureColor (texture_t *tx, double rsum, double gsum, double bsum, size_t green)
{
	tx->grass_color_valid = true;
	tx->grass_color[0] = CLAMP(0.025f, (float)(rsum / green) * (1.0f / 255.0f), 0.42f);
	tx->grass_color[1] = CLAMP(0.08f, (float)(gsum / green) * (1.0f / 255.0f), 0.62f);
	tx->grass_color[2] = CLAMP(0.01f, (float)(bsum / green) * (1.0f / 255.0f), 0.32f);
}

static void Mod_DetectGrassTextureIndexed (texture_t *tx, const byte *data, unsigned int width, unsigned int height)
{
	size_t i, pixels, step, tested, green;
	unsigned int counts[256];
	unsigned int sum_r[256];
	unsigned int sum_g[256];
	unsigned int sum_b[256];
	unsigned int best_count;
	int best_value;
	int best_index;

	if (!data || !width || !height)
		return;

	pixels = (size_t)width * (size_t)height;
	step = pixels > 65536 ? pixels / 65536 : 1;
	tested = green = 0;
	best_count = 0;
	best_value = -1;
	best_index = -1;
	memset(counts, 0, sizeof(counts));
	memset(sum_r, 0, sizeof(sum_r));
	memset(sum_g, 0, sizeof(sum_g));
	memset(sum_b, 0, sizeof(sum_b));

	for (i = 0; i < pixels; i += step)
	{
		byte index = data[i];
		byte *rgba = (byte *)&d_8to24table[index];

		if (rgba[3] < 128)
			continue;
		tested++;
		if (!Mod_IsGrassGreenPixel(rgba[0], rgba[1], rgba[2], rgba[3]))
			continue;

		green++;
		counts[index]++;
		sum_r[index] += rgba[0];
		sum_g[index] += rgba[1];
		sum_b[index] += rgba[2];
	}

	if (green >= 8)
	{
		for (i = 0; i < 256; i++)
		{
			unsigned int count = counts[i];
			int r, g, b, value;

			if (!count)
				continue;

			r = (int)((sum_r[i] + count / 2) / count);
			g = (int)((sum_g[i] + count / 2) / count);
			b = (int)((sum_b[i] + count / 2) / count);
			value = q_max(r, q_max(g, b));

			if (count > best_count || (count == best_count && value > best_value))
			{
				best_count = count;
				best_value = value;
				best_index = (int)i;
			}
		}

		if (best_index >= 0)
			Mod_SetGrassTextureColor(tx, sum_r[best_index], sum_g[best_index], sum_b[best_index], best_count);
		if (tested > 0 && (float)green / (float)tested >= 0.035f)
			tx->grass_detected = true;
	}
}

static void Mod_DetectGrassTextureRGBA (texture_t *tx, const byte *data, unsigned int width, unsigned int height)
{
	enum { bucket_count = 16 * 16 * 16 };
	size_t i, pixels, step, tested, green;
	unsigned int counts[bucket_count];
	unsigned int sum_r[bucket_count];
	unsigned int sum_g[bucket_count];
	unsigned int sum_b[bucket_count];
	unsigned int best_count;
	int best_value;
	int best_bucket;

	if (!data || !width || !height)
		return;

	pixels = (size_t)width * (size_t)height;
	step = pixels > 65536 ? pixels / 65536 : 1;
	tested = green = 0;
	best_count = 0;
	best_value = -1;
	best_bucket = -1;
	memset(counts, 0, sizeof(counts));
	memset(sum_r, 0, sizeof(sum_r));
	memset(sum_g, 0, sizeof(sum_g));
	memset(sum_b, 0, sizeof(sum_b));

	for (i = 0; i < pixels; i += step)
	{
		const byte *rgba = data + i * 4;
		int bucket;

		if (rgba[3] < 128)
			continue;
		tested++;
		if (!Mod_IsGrassGreenPixel(rgba[0], rgba[1], rgba[2], rgba[3]))
			continue;

		bucket = ((rgba[0] >> 4) << 8) | ((rgba[1] >> 4) << 4) | (rgba[2] >> 4);
		green++;
		counts[bucket]++;
		sum_r[bucket] += rgba[0];
		sum_g[bucket] += rgba[1];
		sum_b[bucket] += rgba[2];
	}

	if (green >= 8)
	{
		for (i = 0; i < bucket_count; i++)
		{
			unsigned int count = counts[i];
			int r, g, b, value;

			if (!count)
				continue;

			r = (int)((sum_r[i] + count / 2) / count);
			g = (int)((sum_g[i] + count / 2) / count);
			b = (int)((sum_b[i] + count / 2) / count);
			value = q_max(r, q_max(g, b));

			if (count > best_count || (count == best_count && value > best_value))
			{
				best_count = count;
				best_value = value;
				best_bucket = (int)i;
			}
		}

		if (best_bucket >= 0)
			Mod_SetGrassTextureColor(tx, sum_r[best_bucket], sum_g[best_bucket], sum_b[best_bucket], best_count);
		if (tested > 0 && (float)green / (float)tested >= 0.035f)
			tx->grass_detected = true;
	}
}

static qboolean Mod_TextureNameCanHaveGrass (const char *name)
{
	const char *checkname;

	if (!name || !name[0])
		return false;

	checkname = name;
	if (checkname[0] == '+' && checkname[1] && checkname[2])
		checkname += 2;

	if (checkname[0] == '*' || checkname[0] == '!' || checkname[0] == '{')
		return false;
	if (!q_strncasecmp(checkname, "sky", 3))
		return false;
	return true;
}

static void Mod_DetectGrassTexture (texture_t *tx, enum srcformat fmt, const byte *data, unsigned int width, unsigned int height)
{
	tx->grass_detected = false;
	tx->grass_color_valid = false;
	tx->grass_color[0] = 0.11f;
	tx->grass_color[1] = 0.34f;
	tx->grass_color[2] = 0.045f;

	if (!Mod_TextureNameCanHaveGrass(tx->name))
		return;

	switch (fmt)
	{
	case SRC_INDEXED:
		Mod_DetectGrassTextureIndexed(tx, data, width, height);
		break;
	case SRC_RGBA:
		Mod_DetectGrassTextureRGBA(tx, data, width, height);
		break;
	default:
		break;
	}
}

static texture_t *Mod_LoadMipTex(miptex_t *mt, byte *lumpend, enum srcformat *fmt, unsigned int *width, unsigned int *height, unsigned int *pixelbytes)
{
	//if offsets[0] is 0, then we've no legacy data (offsets[3] signifies the end of the extension data.
	byte *extdata;
	texture_t *tx;
	byte *srcdata = NULL;
	size_t sz;
	int shift = 0;
	unsigned int allocpixelbytes;

	if (loadmodel->bspversion == BSPVERSION_QUAKE64)
		extdata = lumpend;	//don't bother, I'm too lazy to validate offsets.
	else if (!mt->offsets[0])	//the legacy data was omitted. we may still have block-compression though.
		extdata = (byte*)(mt+1);
	else if (mt->offsets[0] == sizeof(miptex_t) &&
			 mt->offsets[1] == mt->offsets[0]+(mt->width>>0)*(mt->height>>0) &&
			 mt->offsets[2] == mt->offsets[1]+(mt->width>>1)*(mt->height>>1) &&
			 mt->offsets[3] == mt->offsets[2]+(mt->width>>2)*(mt->height>>2))
	{	//miptex makes sense and matches the standard 4-mip-levels.
		extdata = (byte*)mt + mt->offsets[3]+(mt->width>>3)*(mt->height>>3);
		//FIXME: halflife - leshort=256, palette[256][3].
		//extdata += 2+256*3;
	}
	else	//the numbers don't match what we expect... something weird is going on here... don't misinterpret it.
		extdata = lumpend;

	if (extdata+4 <= lumpend && extdata[0] == 0 && extdata[1]==0xfb && extdata[2]==0x2b && extdata[3]==0xaf)
	for (extdata+=4; extdata+8 < lumpend; extdata += sz)
	{
		sz = (extdata[0]<<0)|(extdata[1]<<8)|(extdata[2]<<16)|(extdata[3]<<24);
		if (sz < 8 || sz >(size_t)(lumpend-extdata))	break;	//bad! bad! bad!
		else if (sz <= 16)	continue;	//nope, no idea

		*fmt = TexMgr_FormatForCode((char*)extdata+4);
		if (*fmt == SRC_EXTERNAL)
			continue;	//nope, no idea

		*width = (extdata[8]<<0)|(extdata[9]<<8)|(extdata[10]<<16)|(extdata[11]<<24);
		*height = (extdata[12]<<0)|(extdata[13]<<8)|(extdata[14]<<16)|(extdata[15]<<24);

		if (*width > (unsigned int)INT_MAX || *height > (unsigned int)INT_MAX)
			continue;	//nope, can't use that.
		if ((int)*width != TexMgr_SafeTextureSize((int)*width) || (int)*height != TexMgr_SafeTextureSize((int)*height))
			continue;	//nope, can't use that. drivers are too lame (or gl_max_size is too low).

		size_t pixelcount = TexMgr_ImageSize((int)*width, (int)*height, *fmt);
		if (pixelcount > (size_t)UINT_MAX)
			continue;
		*pixelbytes = (unsigned int)pixelcount;
		if (16 + (size_t)*pixelbytes == sz)
			srcdata = extdata+16;
		break;
	}

	if (!srcdata)
	{	//no replacements, load the 8bit data.
		*fmt = SRC_INDEXED;
		*width = mt->width;
		*height = mt->height;
		if (*width > (unsigned int)INT_MAX || *height > (unsigned int)INT_MAX)
			Sys_Error ("Mod_LoadMipTex: texture %s is too large", mt->name);
		{
			size_t pixelcount = TexMgr_ImageSize((int)*width, (int)*height, *fmt);
			if (pixelcount > (size_t)UINT_MAX)
				Sys_Error ("Mod_LoadMipTex: texture %s is too large", mt->name);
			*pixelbytes = (unsigned int)pixelcount;
		}

		if (loadmodel->bspversion == BSPVERSION_QUAKE64)
		{
			miptex64_t *mt64 = (miptex64_t*)mt;
			srcdata = (byte*)(mt64 + 1);	//revert to lameness
			shift = LittleLong (mt64->shift);
		}
		else
		{
			if (LittleLong (mt->offsets[0]))
				srcdata = (byte*)mt+LittleLong(mt->offsets[0]);
		}
	}

	allocpixelbytes = *pixelbytes;
	if ((size_t)allocpixelbytes > (size_t)INT_MAX - sizeof(texture_t))
		Sys_Error ("Mod_LoadMipTex: texture %s is too large", mt->name);
	tx = (texture_t *) Hunk_AllocNameNoFill ((int)(sizeof(texture_t) + (size_t)allocpixelbytes), loadname );
	// only clear the texture struct, not the pixel buffer following it
	memset (tx, 0, sizeof (*tx));
	memcpy (tx->name, mt->name, sizeof(tx->name));
	tx->name[sizeof(tx->name)-1] = 0;	//just in case...
	tx->width = mt->width;
	tx->height = mt->height;
	tx->shift = shift;

	if (srcdata)
	{
		// ericw -- check for pixels extending past the end of the lump.
		// appears in the wild; e.g. jam2_tronyn.bsp (func_mapjam2),
		// kellbase1.bsp (quoth), and can lead to a segfault if we read past
		// the end of the .bsp file buffer
		if ((srcdata + *pixelbytes) > lumpend)
		{
			ptrdiff_t available = lumpend - srcdata;
			Con_DPrintf("Texture %s extends past end of lump\n", mt->name);
			*pixelbytes = (available > 0) ? (unsigned int)available : 0;
		}

		memcpy ( tx+1, srcdata, *pixelbytes);
		if (*pixelbytes < allocpixelbytes)
			memset ((byte *)(tx+1) + *pixelbytes, 0, allocpixelbytes - *pixelbytes);
	}
	else
	{
		size_t x,y;
		for(y=0;y<tx->height;y++)
			for(x=0;x<tx->width;x++)
				((byte*)(tx+1))[y*tx->width+x] = (((x>>2)^(y>>2))&1)?6:2;
	}
	return tx;
}
/*
=================
Mod_CheckAnimTextureArrayQ64

Quake64 bsp
Check if we have any missing textures in the array
=================
*/
static qboolean Mod_CheckAnimTextureArrayQ64(texture_t *anims[], int numTex)
{
	int i;

	for (i = 0; i < numTex; i++)
	{
		if (!anims[i])
			return false;
	}
	return true;
}

/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures (lump_t *l)
{
	int		i, j, num, maxanim, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t	*m;
//johnfitz -- more variables
	char		texturename[64];
	int			nummiptex;
	src_offset_t		offset;
	int			mark, fwidth, fheight;
	char		filename[MAX_OSPATH], mapname[MAX_OSPATH];
	byte		*data;
//johnfitz
	qboolean malloced;	//spike
	enum srcformat fmt;	//spike
	unsigned int imgwidth, imgheight, imgpixels;
	unsigned int mipend;

	//johnfitz -- don't return early if no textures; still need to create dummy texture
	if (!l->filelen)
	{
		Con_Printf ("Mod_LoadTextures: no textures in bsp file\n");
		nummiptex = 0;
		m = NULL; // avoid bogus compiler warning
	}
	else
	{
		m = (dmiptexlump_t *)(mod_base + l->fileofs);
		m->nummiptex = LittleLong (m->nummiptex);
		nummiptex = m->nummiptex;
	}
	//johnfitz

	loadmodel->numtextures = nummiptex + 2; //johnfitz -- need 2 dummy texture chains for missing textures
	loadmodel->textures = (texture_t **) Hunk_AllocName (loadmodel->numtextures * sizeof(*loadmodel->textures) , loadname);

	//spike -- rewrote this loop to run backwards (to make it easier to track the end of the miptex) and added handling for extra texture block compression.
	for (i = nummiptex, mipend=l->filelen; i --> 0; )
	{
		m->dataofs[i] = LittleLong(m->dataofs[i]);
		if (m->dataofs[i] == -1)
			continue;
		if ((unsigned int)m->dataofs[i] >= mipend)
			mipend = l->filelen;	//o.O something weird!
		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		mt->width = LittleLong (mt->width);
		mt->height = LittleLong (mt->height);
		for (j=0 ; j<MIPLEVELS ; j++)
			mt->offsets[j] = LittleLong (mt->offsets[j]);

		if (mt->width == 0 || mt->height == 0)
		{
			Con_Warning ("Zero sized texture %s in %s!\n", mt->name, loadmodel->name);
			continue;
		}

		if ( (mt->width & 15) || (mt->height & 15) )
		{
			if (loadmodel->bspversion != BSPVERSION_QUAKE64)
				Con_Warning ("Texture %s (%d x %d) is not 16 aligned\n", mt->name, mt->width, mt->height);
		}

		tx = Mod_LoadMipTex(mt, (mod_base + l->fileofs + mipend), &fmt, &imgwidth, &imgheight, &imgpixels);
		loadmodel->textures[i] = tx;

		mipend = m->dataofs[i];

		if (!tx->name[0]) // woods (aerowalk.bsp)
		{
			q_snprintf(tx->name, sizeof(tx->name), "unnamed%d", i);
			Con_DPrintf ("unnamed texture in %s, renaming to %s\n", loadmodel->name, tx->name);
		}

		if (tx->name[0] && strchr(tx->name + 1, '*')) // woods (oldcrat.bsp)
		{
			char safename[sizeof(tx->name)];

			q_strlcpy(safename, tx->name, sizeof(safename));

			for (size_t k = 1; safename[k]; ++k)
				if (safename[k] == '*')
					safename[k] = '#';

			Con_DPrintf("texture \"%s\" in %s renamed to \"%s\" to avoid wildcard\n",
				tx->name, loadmodel->name, safename);

			q_strlcpy(tx->name, safename, sizeof(tx->name));
		}

		//johnfitz -- lots of changes
		if (!isDedicated) //no texture uploading for dedicated server
		{
			if (!q_strncasecmp(tx->name,"sky",3)) //sky texture //also note -- was Q_strncmp, changed to match qbsp
			{
				if (!gl_load24bit.value || !Sky_LoadExternalTextures(loadmodel, tx)) // woods #extsky
				{
					if (loadmodel->bspversion == BSPVERSION_QUAKE64)
						Sky_LoadTextureQ64 (loadmodel, tx);
					else
						Sky_LoadTexture (loadmodel, tx, fmt, imgwidth, imgheight);
				}
			}
			else if (tx->name[0] == '*') //warping texture
			{
				enum srcformat rfmt = SRC_RGBA;
				fwidth = fheight = 0;
				malloced = false;
				//external textures -- first look in "textures/mapname/" then look in "textures/"
				mark = Hunk_LowMark();
				COM_StripExtension (loadmodel->name + 5, mapname, sizeof(mapname));
				q_snprintf (filename, sizeof(filename), "textures/%s/#%s", mapname, tx->name+1); //this also replaces the '*' with a '#'
				
				if (gl_load24bit.value == 2) // woods #load24bit2
				{
					data = /*!gl_load24bit.value ? NULL : */Image_LoadImage(filename, &fwidth, &fheight, &rfmt, &malloced); // woods load water
					if (!data)
					{
						q_snprintf (filename, sizeof(filename), "textures/#%s", tx->name+1);
						data = /*!gl_load24bit.value ? NULL : */Image_LoadImage (filename, &fwidth, &fheight, &rfmt, &malloced); // woods load water
					}
				}
				else
				{
					data = !gl_load24bit.value ? NULL : Image_LoadImage(filename, &fwidth, &fheight, &rfmt, &malloced);
					if (!data)
					{
						q_snprintf (filename, sizeof(filename), "textures/#%s", tx->name + 1);
						data = !gl_load24bit.value ? NULL : Image_LoadImage (filename, &fwidth, &fheight, &rfmt, &malloced);
					}
				}

				//now load whatever we found
				if (data && !r_fastturb.value) //load external image // woods #fastturb
				{
					q_strlcpy (texturename, filename, sizeof(texturename));
					Mod_DetectLiquidTexture(tx, rfmt, data, fwidth, fheight); // woods #autocshift
					tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, fwidth, fheight,
						rfmt, data, filename, 0, TEXPREF_MIPMAP); // woods #watermip
				}
				else //use the texture from the bsp file
				{
					q_snprintf (texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
					offset = (src_offset_t)(mt+1) - (src_offset_t)mod_base;
					Mod_DetectLiquidTexture(tx, fmt, (byte *)(tx+1), imgwidth, imgheight); // woods #autocshift
					tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, imgwidth, imgheight,
						fmt, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP); // woods #watermip
				}

				Hunk_FreeToLowMark (mark);
				if (malloced)
					free(data);
			}
			else //regular texture
			{
				// ericw -- fence textures
				int	extraflags;
				enum srcformat rfmt = SRC_RGBA;
				fwidth = fheight = 0;
				malloced = false;

				extraflags = 0;
				if (tx->name[0] == '{')
					extraflags |= TEXPREF_ALPHA;
				// ericw

				//external textures -- first look in "textures/mapname/" then look in "textures/"
				mark = Hunk_LowMark ();
				COM_StripExtension (loadmodel->name + 5, mapname, sizeof(mapname));
				q_snprintf (filename, sizeof(filename), "textures/%s/%s", mapname, tx->name);

				if (gl_load24bit.value == 2) // woods #load24bit2
				{
					qboolean brushbsp = isSpecialMap(mapname);

					if (brushbsp)
						data = Image_LoadImage(filename, &fwidth, &fheight, &rfmt, &malloced);
					else
						data = (!gl_load24bit.value || gl_load24bit.value == 2) ? NULL : Image_LoadImage(filename, &fwidth, &fheight, &rfmt, &malloced);
					if (!data)
					{
						q_snprintf(filename, sizeof(filename), "textures/%s", tx->name);
						if (brushbsp)
							data = Image_LoadImage(filename, &fwidth, &fheight, &rfmt, &malloced);
						else
							data = (!gl_load24bit.value || gl_load24bit.value == 2) ? NULL : Image_LoadImage(filename, &fwidth, &fheight, &rfmt, &malloced);
					}
				}
				else
				{
					data = (!gl_load24bit.value || gl_load24bit.value == 2) ? NULL : Image_LoadImage(filename, &fwidth, &fheight, &rfmt, &malloced);
					if (!data)
					{
						q_snprintf (filename, sizeof(filename), "textures/#%s", tx->name + 1);
						data = (!gl_load24bit.value || gl_load24bit.value == 2) ? NULL : Image_LoadImage (filename, &fwidth, &fheight, &rfmt, &malloced);
					}
				}

				//now load whatever we found
				if (data) //load external image
				{
					char filename2[MAX_OSPATH];
					Mod_DetectGrassTexture(tx, rfmt, data, fwidth, fheight); // woods #grass
					tx->gltexture = TexMgr_LoadImage (loadmodel, filename, fwidth, fheight,
						rfmt, data, filename, 0, TEXPREF_MIPMAP | extraflags );

					//now try to load glow/luma image from the same place
					if (malloced)
						free(data);
					Hunk_FreeToLowMark (mark);
					q_snprintf (filename2, sizeof(filename2), "%s_glow", filename);
					data = (!gl_load24bit.value || gl_load24bit.value == 2)?NULL:Image_LoadImage (filename2, &fwidth, &fheight, &rfmt, &malloced);
					if (!data)
					{
						q_snprintf (filename2, sizeof(filename2), "%s_luma", filename);
						data = (!gl_load24bit.value || gl_load24bit.value == 2)?NULL:Image_LoadImage (filename2, &fwidth, &fheight, &rfmt, &malloced);
					}

					if (data)
						tx->fullbright = TexMgr_LoadImage (loadmodel, filename2, fwidth, fheight,
							rfmt, data, filename2, 0, TEXPREF_MIPMAP | extraflags );
				}
				else //use the texture from the bsp file
				{
					q_snprintf (texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
					offset = (src_offset_t)(mt+1) - (src_offset_t)mod_base;
					Mod_DetectGrassTexture(tx, fmt, (byte *)(tx+1), imgwidth, imgheight); // woods #grass
					if (fmt == SRC_INDEXED && Mod_CheckFullbrights ((byte *)(tx+1), imgpixels))
					{
						tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, imgwidth, imgheight,
							fmt, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_NOBRIGHT | extraflags);
						q_snprintf (texturename, sizeof(texturename), "%s:%s_glow", loadmodel->name, tx->name);
						tx->fullbright = TexMgr_LoadImage (loadmodel, texturename, imgwidth, imgheight,
							fmt, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT | extraflags);
					}
					else
					{
						tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, imgwidth, imgheight,
							fmt, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | extraflags);
					}
				}
				if (malloced)
					free(data);
				Hunk_FreeToLowMark (mark);
			}
		}
		//johnfitz
	}

	//johnfitz -- last 2 slots in array should be filled with dummy textures
	loadmodel->textures[loadmodel->numtextures-2] = r_notexture_mip; //for lightmapped surfs
	loadmodel->textures[loadmodel->numtextures-1] = r_notexture_mip2; //for SURF_DRAWTILED surfs

//
// sequence the animations
//
	for (i=0 ; i<nummiptex ; i++)
	{
		tx = loadmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// already sequenced

	// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		maxanim = tx->name[1];
		altmax = 0;
		if (maxanim >= 'a' && maxanim <= 'z')
			maxanim -= 'a' - 'A';
		if (maxanim >= '0' && maxanim <= '9')
		{
			maxanim -= '0';
			altmax = 0;
			anims[maxanim] = tx;
			maxanim++;
		}
		else if (maxanim >= 'A' && maxanim <= 'J')
		{
			altmax = maxanim - 'A';
			maxanim = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<nummiptex ; j++)
		{
			tx2 = loadmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > maxanim)
					maxanim = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}

		if (loadmodel->bspversion == BSPVERSION_QUAKE64 && !Mod_CheckAnimTextureArrayQ64(anims, maxanim))
			continue; // Just pretend this is a normal texture

#define	ANIM_CYCLE	2
	// link them all together
		for (j=0 ; j<maxanim ; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = maxanim * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%maxanim ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (maxanim)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadLighting -- johnfitz -- replaced with lit support code via lordhavoc
=================
*/
static void Mod_LoadLighting (lump_t *l)
{
	int i, mark;
	byte *in, *out, *data;
	byte d, q64_b0, q64_b1;
	char litfilename[MAX_OSPATH];
	unsigned int path_id;
	int	bspxsize;

	loadmodel->flags &= ~MOD_HDRLIGHTING; //just in case.
	loadmodel->lightdata = NULL;
	// LordHavoc: check for a .lit file
	q_strlcpy(litfilename, loadmodel->name, sizeof(litfilename));
	COM_StripExtension(litfilename, litfilename, sizeof(litfilename));
	q_strlcat(litfilename, ".lit", sizeof(litfilename));
	mark = Hunk_LowMark();
	data = NULL;

	if (gl_loadlitfiles.value >= 1) // woods #loadlits #litdir
	{
		char altlitfilename[MAX_OSPATH];
		qboolean try_external = false;

		// Check if we should try external lits first
		if (gl_loadlitfiles.value >= 2 && external_lits_dir.string[0])
		{
			q_snprintf(altlitfilename, sizeof(altlitfilename), "maps/%s/%s",
				external_lits_dir.string, COM_SkipPath(litfilename));

			if (gl_loadlitfiles.value == 2 ||
				(gl_loadlitfiles.value == 3 && (rand() & 1)))
			{
				try_external = true;
			}

			if (try_external && COM_FileExists(altlitfilename, NULL))
			{
				Con_DPrintf2("trying to load %s\n", altlitfilename);
				data = (byte*)COM_LoadHunkFile(altlitfilename, &path_id);
			}
		}

		// Load standard .lit file if no external data loaded
		if (!data)
			data = (byte*)COM_LoadHunkFile(litfilename, &path_id);
	}
	if (data)
	{
		// use lit file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < loadmodel->path_id)
		{
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("ignored %s from a gamedir with lower priority\n", litfilename);
		}
		else
		if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I' && data[3] == 'T')
		{
			i = LittleLong(((int *)data)[1]);
			if (i == 1)
			{
				if (8+l->filelen*3 == com_filesize)
				{
					Con_DPrintf2("%s loaded (ldr)\n", litfilename);
					loadmodel->lightdata = data + 8;
					loadmodel->lightdatasamples = l->filelen;
					return;
				}
				Hunk_FreeToLowMark(mark);
				Con_Printf("Outdated .lit file (%s should be %u bytes, not %u)\n", litfilename, 8+l->filelen*3, (unsigned)com_filesize);
			}
			else if (i == 0x10001)
			{
				if (8+l->filelen*4 == com_filesize)
				{
					Con_DPrintf2("%s loaded (hdr)\n", litfilename);
					loadmodel->lightdata = data + 8;
					loadmodel->lightdatasamples = l->filelen;
					loadmodel->flags |= MOD_HDRLIGHTING;
					for (i = 0; i < loadmodel->lightdatasamples; i++)
						((int*)loadmodel->lightdata)[i] = LittleLong(((int*)loadmodel->lightdata)[i]);
					return;
				}
				Hunk_FreeToLowMark(mark);
				Con_Printf("Outdated .lit file (%s should be %u bytes, not %u)\n", litfilename, 8+l->filelen*4, (unsigned)com_filesize);
			}
			else
			{
				Hunk_FreeToLowMark(mark);
				Con_Printf("Unknown .lit file version (%d)\n", i);
			}
		}
		else
		{
			Hunk_FreeToLowMark(mark);
			Con_Printf("Corrupt .lit file (old version?), ignoring\n");
		}
	}
	// LordHavoc: no .lit found, expand the white lighting data to color

	// Quake64 bsp lighmap data
	if (loadmodel->bspversion == BSPVERSION_QUAKE64 && l->filelen)
	{
		// RGB lightmap samples are packed in 16bits.
		// RRRRR GGGGG BBBBBB

		loadmodel->lightdata = (byte *) Hunk_AllocNameNoFill ( (l->filelen / 2)*3, litfilename);
		loadmodel->lightdatasamples = (l->filelen / 2);
		in = mod_base + l->fileofs;
		out = loadmodel->lightdata;

		for (unsigned int i = 0;i < (l->filelen / 2) ;i++)
		{
			q64_b0 = *in++;
			q64_b1 = *in++;

			*out++ = q64_b0 & 0xf8;/* 0b11111000 */
			*out++ = ((q64_b0 & 0x07) << 5) + ((q64_b1 & 0xc0) >> 5);/* 0b00000111, 0b11000000 */
			*out++ = (q64_b1 & 0x3f) << 2;/* 0b00111111 */
		}
		return;
	}

	if (gl_loadlitfiles.value > 0) // woods #loadlits
	{
		in = Q1BSPX_FindLump("LIGHTING_E5BGR9", &bspxsize);
		if (in && (!l->filelen || (bspxsize && bspxsize == l->filelen * 4)))
		{
			loadmodel->lightdata = (byte*)Hunk_AllocName(bspxsize, litfilename);
			loadmodel->lightdatasamples = bspxsize / 4;
			memcpy(loadmodel->lightdata, in, bspxsize);
			loadmodel->flags |= MOD_HDRLIGHTING;
			Con_DPrintf("bspx hdr lighting loaded\n");
			for (i = 0; i < loadmodel->lightdatasamples; i++)    // native endian...
				((int*)loadmodel->lightdata)[i] = LittleLong(((int*)loadmodel->lightdata)[i]);
			return;
		}
		in = Q1BSPX_FindLump("RGBLIGHTING", &bspxsize);
		if (in && (!l->filelen || (bspxsize && bspxsize == l->filelen * 3)))
		{
			loadmodel->lightdata = (byte*)Hunk_AllocName(bspxsize, litfilename);
			loadmodel->lightdatasamples = bspxsize / 3;
			memcpy(loadmodel->lightdata, in, bspxsize);
			Con_DPrintf("bspx ldr lighting loaded\n");
			return;
		}
	}
	else {
		Con_DPrintf2("gl_loadlitfiles 0: ignoring BSPX colored lighting lumps\n");
	}

	if (l->filelen)
	{
		loadmodel->lightdata = (byte *) Hunk_AllocNameNoFill ( l->filelen*3, litfilename);
		loadmodel->lightdatasamples = l->filelen;
		in = loadmodel->lightdata + l->filelen*2; // place the file at the end, so it will not be overwritten until the very last write
		out = loadmodel->lightdata;
		memcpy (in, mod_base + l->fileofs, l->filelen);
		for (unsigned int i = 0;i < l->filelen;i++)
		{
			d = *in++;
			*out++ = d;
			*out++ = d;
			*out++ = d;
		}
		return;
	}
}


/*
=================
Mod_LoadVisibility
=================
*/
static void Mod_LoadVisibility (lump_t *l)
{
	loadmodel->viswarn = false;
	if (!l->filelen)
	{
		loadmodel->visdata = NULL;
		loadmodel->visdata_size = 0;
		return;
	}
	loadmodel->visdata_size = l->filelen;
	loadmodel->visdata = (byte *) Hunk_AllocNameNoFill (loadmodel->visdata_size, loadname);
	memcpy (loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities (lump_t *l)
{
	char	basemapname[MAX_QPATH];
	char	entfilename[MAX_QPATH];
	char		*ents;
	int		mark;
	unsigned int	path_id;
	unsigned int	crc = 0;

	if (! external_ents.value)
		goto _load_embedded;

	mark = Hunk_LowMark();
	if (l->filelen > 0) {
		crc = CRC_Block(mod_base + l->fileofs, l->filelen - 1);
	}

	q_strlcpy(basemapname, loadmodel->name, sizeof(basemapname));
	COM_StripExtension(basemapname, basemapname, sizeof(basemapname));

	
	q_snprintf(entfilename, sizeof(entfilename), "maps/%s/%s@%04x.ent", external_ents_dir.string, COM_SkipPath(basemapname), crc);
	if (external_ents_dir.string && COM_FileExists(entfilename, NULL))
	{
		Con_DPrintf2("trying to load %s\n", entfilename);
		ents = (char*)COM_LoadHunkFile(entfilename, &path_id);
	}
	else
	{
		q_snprintf(entfilename, sizeof(entfilename), "%s@%04x.ent", basemapname, crc);
		Con_DPrintf2("trying to load %s\n", entfilename);
		ents = (char *) COM_LoadHunkFile (entfilename, &path_id);
	}

	if (!ents)
	{
		q_snprintf(entfilename, sizeof(entfilename), "maps/%s/%s.ent", external_ents_dir.string, COM_SkipPath(basemapname));
		if (external_ents_dir.string && COM_FileExists(entfilename, NULL))
		{
			Con_DPrintf2("trying to load %s\n", entfilename);
			ents = (char*)COM_LoadHunkFile(entfilename, &path_id);
		}
		else
		{
			q_snprintf(entfilename, sizeof(entfilename), "%s.ent", basemapname);
			Con_DPrintf2("trying to load %s\n", entfilename);
			ents = (char *) COM_LoadHunkFile (entfilename, &path_id);
		}
	}

	if (ents)
	{
		// use ent file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < loadmodel->path_id)
		{
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("ignored %s from a gamedir with lower priority\n", entfilename);
		}
		else
		{
			loadmodel->entities = ents;
			Con_DPrintf("Loaded external entity file %s\n", entfilename);
			return;
		}
	}

_load_embedded:
	if (!l->filelen)
	{
		loadmodel->entities = NULL;
		return;
	}
	// Note: some BSPs don't contain a NUL terminator, e.g.
	// https://www.quakeone.com/qrack/maps/Mcmdm04.bsp
	// https://www.quakeone.com/qrack/maps/Jvoxdm3.bsp
	loadmodel->entities = (char *) Hunk_AllocNameNoFill (l->filelen + 1, loadname);
	memcpy (loadmodel->entities, mod_base + l->fileofs, l->filelen);
	loadmodel->entities[l->filelen] = '\0';
}


/*
=================
Mod_ParseWorldspawnKey
=================
(Blame Spike)
This just quickly scans the worldspawn entity for a single key. Returning both _prefixed and non prefixed keys.
(wantkey argument should not have a _prefix.)
*/
const char *Mod_ParseWorldspawnKey(qmodel_t *mod, const char *wantkey, char *buffer, size_t sizeofbuffer)
{
	char foundkey[128];
	const char *data = COM_Parse(mod->entities);

	if (data && com_token[0] == '{')
	{
		while (1)
		{
			data = COM_Parse(data);
			if (!data)
				break; // error
			if (com_token[0] == '}')
				break; // end of worldspawn
			if (com_token[0] == '_')
				q_strlcpy(foundkey, com_token + 1, sizeof(foundkey));
			else
				q_strlcpy(foundkey, com_token, sizeof(foundkey));
			data = COM_Parse(data);
			if (!data)
				break; // error
			if (!strcmp(wantkey, foundkey))
			{
				q_strlcpy(buffer, com_token, sizeofbuffer);
				return buffer;
			}
		}
	}
	return NULL;
}


/*
=================
Mod_LoadVertexes
=================
*/
static void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (dvertex_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mvertex_t *) Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
static void Mod_LoadEdges (lump_t *l, int bsp2)
{
	medge_t *out;
	int 	i, count;

	if (bsp2)
	{
		dledge_t *in = (dledge_t *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (medge_t *) Hunk_AllocNameNoFill ( (count + 1) * sizeof(*out), loadname);

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for (i=0 ; i<count ; i++, in++, out++)
		{
			out->v[0] = LittleLong(in->v[0]);
			out->v[1] = LittleLong(in->v[1]);
		}
		out->v[0] = out->v[1] = 0;
	}
	else
	{
		dsedge_t *in = (dsedge_t *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (medge_t *) Hunk_AllocNameNoFill ( (count + 1) * sizeof(*out), loadname);

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for (i=0 ; i<count ; i++, in++, out++)
		{
			out->v[0] = (unsigned short)LittleShort(in->v[0]);
			out->v[1] = (unsigned short)LittleShort(in->v[1]);
		}
		out->v[0] = out->v[1] = 0;
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
static void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int	i, j, count, miptex;
	int missing = 0; //johnfitz

	in = (texinfo_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mtexinfo_t *) Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<4 ; j++)
		{
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
			out->vecs[1][j] = LittleFloat (in->vecs[1][j]);
		}

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);

		//johnfitz -- rewrote this section
		if (miptex >= loadmodel->numtextures-1 || !loadmodel->textures[miptex])
		{
			if (out->flags & TEX_SPECIAL)
				miptex = loadmodel->numtextures-1;
			else
				miptex = loadmodel->numtextures-2;
			out->flags |= TEX_MISSING;
			missing++;
		}
		out->texture = loadmodel->textures[miptex];
		out->materialidx = miptex;
		//johnfitz
	}

	//johnfitz: report missing textures
	if (missing && loadmodel->numtextures > 1)
		Con_Printf ("Mod_LoadTexinfo: %d texture(s) missing from BSP file\n", missing);
	//johnfitz
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
static void CalcSurfaceExtents (msurface_t *s, int lmshift)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];
	int lmscale;

	mins[0] = mins[1] = FLT_MAX;
	maxs[0] = maxs[1] = -FLT_MAX;

	tex = s->texinfo;

	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		for (j=0 ; j<2 ; j++)
		{
			/* The following calculation is sensitive to floating-point
			 * precision.  It needs to produce the same result that the
			 * light compiler does, because R_BuildLightMap uses surf->
			 * extents to know the width/height of a surface's lightmap,
			 * and incorrect rounding here manifests itself as patches
			 * of "corrupted" looking lightmaps.
			 * Most light compilers are win32 executables, so they use
			 * x87 floating point.  This means the multiplies and adds
			 * are done at 80-bit precision, and the result is rounded
			 * down to 32-bits and stored in val.
			 * Adding the casts to double seems to be good enough to fix
			 * lighting glitches when Quakespasm is compiled as x86_64
			 * and using SSE2 floating-point.  A potential trouble spot
			 * is the hallway at the beginning of mfxsp17.  -- ericw
			 */
			val =	((double)v->position[0] * (double)tex->vecs[j][0]) +
				((double)v->position[1] * (double)tex->vecs[j][1]) +
				((double)v->position[2] * (double)tex->vecs[j][2]) +
				(double)tex->vecs[j][3];

			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	lmscale = 1<<lmshift;

	for (i=0 ; i<2 ; i++)
	{
		bmins[i] = floor(mins[i]/lmscale);
		bmaxs[i] = ceil(maxs[i]/lmscale);

		s->lmvecs[i][0] = s->texinfo->vecs[i][0] / lmscale;
		s->lmvecs[i][1] = s->texinfo->vecs[i][1] / lmscale;
		s->lmvecs[i][2] = s->texinfo->vecs[i][2] / lmscale;
		s->lmvecs[i][3] = s->texinfo->vecs[i][3] / lmscale - bmins[i];
		if (mod_lightscale_broken.value)
			s->lmvecscale[i] = 16;	//luxels->qu... except buggy so dlights have the wrong spread on large surfaces (blame shib7)
		else
			s->lmvecscale[i] = 1.0f/VectorLength(s->lmvecs[i]);	//luxels->qu
		s->extents[i] = bmaxs[i] - bmins[i];

		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] >= (i?LMBLOCK_HEIGHT:LMBLOCK_WIDTH)) //johnfitz -- was 512 in glquake, 256 in winquake
		{
			s->extents[i] = 1;
//			Sys_Error ("Bad surface extents");
		}
	}
}

#define MIN_FLOOR_NORMAL	0.7f	// matches MIN_STEP_NORMAL in pmove.c:37

static void Mod_ResetMapSurfaceAreas (qmodel_t *mod)
{
	if (!mod)
		return;

	mod->total_surface_area = 0.0f;
	mod->floor_surface_area = 0.0f;
	mod->wall_surface_area = 0.0f;
	mod->ceiling_surface_area = 0.0f;
	mod->counted_faces = 0;
}

static qboolean Mod_IsToolTexture (const char *name)
{
	static const char *toolnames[] = {
		"trigger", "clip", "skip", "hint", "null", "nodraw", "playerclip", "monsterclip"
	};
	size_t i;

	if (!name || !*name)
		return false;

	for (i = 0; i < Q_COUNTOF(toolnames); ++i)
	{
		const size_t len = strlen(toolnames[i]);

		if (!q_strncasecmp(name, toolnames[i], len))
			return true;
	}

	return false;
}

/*
=================
Mod_CalcSurfaceBoundsAndArea -- johnfitz -- calculate bounding box for per-surface frustum culling
=================
*/
static float Mod_CalcSurfaceBoundsAndArea (msurface_t *s, qboolean calcarea)
{
	double		sum[3] = {0.0, 0.0, 0.0};
	double		v0[3] = {0.0, 0.0, 0.0};
	int			i, e;
	mvertex_t	*v, *prev = NULL;

	s->mins[0] = s->mins[1] = s->mins[2] = FLT_MAX;
	s->maxs[0] = s->maxs[1] = s->maxs[2] = -FLT_MAX;

	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		if (s->mins[0] > v->position[0])
			s->mins[0] = v->position[0];
		if (s->mins[1] > v->position[1])
			s->mins[1] = v->position[1];
		if (s->mins[2] > v->position[2])
			s->mins[2] = v->position[2];

		if (s->maxs[0] < v->position[0])
			s->maxs[0] = v->position[0];
		if (s->maxs[1] < v->position[1])
			s->maxs[1] = v->position[1];
		if (s->maxs[2] < v->position[2])
			s->maxs[2] = v->position[2];

		if (calcarea)
		{
			if (i == 0)
			{
				v0[0] = v->position[0];
				v0[1] = v->position[1];
				v0[2] = v->position[2];
			}
			else if (i == 1)
				prev = v;
			else
			{
				double a[3], b[3];

				a[0] = (double)prev->position[0] - v0[0];
				a[1] = (double)prev->position[1] - v0[1];
				a[2] = (double)prev->position[2] - v0[2];
				b[0] = (double)v->position[0] - v0[0];
				b[1] = (double)v->position[1] - v0[1];
				b[2] = (double)v->position[2] - v0[2];

				sum[0] += a[1] * b[2] - a[2] * b[1];
				sum[1] += a[2] * b[0] - a[0] * b[2];
				sum[2] += a[0] * b[1] - a[1] * b[0];
				prev = v;
			}
		}
	}

	if (!calcarea || s->numedges < 3)
		return 0.0f;

	return (float)(0.5 * sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]));
}

static void Mod_AddMapSurfaceArea (msurface_t *surf, float area, double *total_area, double *floor_area, double *wall_area, double *ceiling_area, int *counted_faces)
{
	float nz;

	if (area <= 0.0f)
		return;
	if (!surf->plane || (surf->flags & SURF_DRAWSKY))
		return;
	if (!surf->texinfo || !surf->texinfo->texture)
		return;
	if (Mod_IsToolTexture(surf->texinfo->texture->name))
		return;

	nz = surf->plane->normal[2];
	if (surf->flags & SURF_PLANEBACK)
		nz = -nz;

	*total_area += area;
	(*counted_faces)++;

	if (nz > MIN_FLOOR_NORMAL)
		*floor_area += area;
	else if (nz < -MIN_FLOOR_NORMAL)
		*ceiling_area += area;
	else
		*wall_area += area;
}

/*
=================
Mod_LoadFaces
=================
*/
static void Mod_LoadFaces (lump_t *l, qboolean bsp2)
{
	dsface_t	*ins;
	dlface_t	*inl;
	msurface_t 	*out;
	int			i, count, surfnum, lofs, shift;
	int			planenum, side, texinfon;
	int			world_first = 0, world_count = 0, world_end = 0;
	double		total_area = 0.0, floor_area = 0.0, wall_area = 0.0, ceiling_area = 0.0;
	int			counted_faces = 0;
	qboolean	is_world_surface;
	qboolean	calc_surface_area;
	float		surface_area;

	unsigned char *lmshift = NULL, defaultshift = 4;
	unsigned int *lmoffset = NULL;
	unsigned char *lmstyle8 = NULL, stylesperface = 4;
	unsigned short *lmstyle16 = NULL;
	int lumpsize;
	char scalebuf[16];
	int facestyles;
	struct decoupled_lm_info_s *decoupledlm = NULL;

	if (bsp2)
	{
		ins = NULL;
		inl = (dlface_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*inl))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
		count = l->filelen / sizeof(*inl);
	}
	else
	{
		ins = (dsface_t *)(mod_base + l->fileofs);
		inl = NULL;
		if (l->filelen % sizeof(*ins))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
		count = l->filelen / sizeof(*ins);
	}
	out = (msurface_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz -- warn mappers about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i faces exceeds standard limit of 32767.\n", count);
	//johnfitz

	if (!mod_ignorelmscale.value && count > 0)
	{
		decoupledlm = Q1BSPX_FindLump("DECOUPLED_LM", &lumpsize); //RGB packed data
		if (decoupledlm && lumpsize == count*sizeof(*decoupledlm))
		{	//basically stomps over the lmshift+lmoffset stuff above. lmstyle/lmstyle16+lit/hdr+lux info is still needed
			lmshift = NULL;
			lmoffset = NULL;
		}
		else
		{
			decoupledlm = NULL;

			lmshift = Q1BSPX_FindLump("LMSHIFT", &lumpsize);
			if (lumpsize != sizeof(*lmshift)*count)
				lmshift = NULL;
			lmoffset = Q1BSPX_FindLump("LMOFFSET", &lumpsize);
			if (lumpsize != sizeof(*lmoffset)*count)
				lmoffset = NULL;

			if (Mod_ParseWorldspawnKey(loadmodel, "lightmap_scale", scalebuf, sizeof(scalebuf)))
			{
				char *e;
				i = strtol(scalebuf, &e, 10);
				if (i < 0 || *e)
					Con_Warning("Incorrect value for lightmap_scale field - %s - should be texels-per-luxel (and power-of-two), use 16 (or omit) to match vanilla quake.\n", scalebuf);
				else if (i == 0)
					;	//silently use default when its explicitly set to 0 or empty. a bogus value but oh well.
				else
				{
					for(defaultshift = 0; i > 1; defaultshift++)
						i >>= 1;
				}
			}
		}
		lmstyle16 = Q1BSPX_FindLump("LMSTYLE16", &lumpsize);
		stylesperface = lumpsize/(sizeof(*lmstyle16)*count);
		if (lumpsize != sizeof(*lmstyle16)*stylesperface*count)
			lmstyle16 = NULL;
		if (!lmstyle16)
		{
			lmstyle8 = Q1BSPX_FindLump("LMSTYLE", &lumpsize);
			stylesperface = lumpsize/(sizeof(*lmstyle8)*count);
			if (lumpsize != sizeof(*lmstyle8)*stylesperface*count)
				lmstyle8 = NULL;
		}
	}

	{
		void *lglump = Q1BSPX_FindLump("LIGHTGRID_OCTREE", &lumpsize);
		BSPX_LightGridLoad(loadmodel, lglump, lumpsize);
	}

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;
	Mod_ResetMapSurfaceAreas(loadmodel);

	if (loadmodel->submodels && loadmodel->numsubmodels > 0)
	{
		world_first = loadmodel->submodels[0].firstface;
		world_count = loadmodel->submodels[0].numfaces;
		if (world_count <= 0 || world_first < 0 || world_first > count || world_count > count - world_first)
		{
			world_first = 0;
			world_count = 0;
		}
		world_end = world_first + world_count;
	}

	for (surfnum=0 ; surfnum<count ; surfnum++, out++)
	{
		if (bsp2)
		{	//32bit datatypes
			out->firstedge = LittleLong(inl->firstedge);
			out->numedges = LittleLong(inl->numedges);
			planenum = LittleLong(inl->planenum);
			side = LittleLong(inl->side);
			texinfon = LittleLong (inl->texinfo);
			for (i=0 ; i<4 ; i++)
				out->styles[i] = ((inl->styles[i]==INVALID_LIGHTSTYLE_OLD)?INVALID_LIGHTSTYLE:inl->styles[i]);
			lofs = LittleLong(inl->lightofs);
			inl++;
		}
		else
		{	//16bit datatypes
			out->firstedge = LittleLong(ins->firstedge);
			out->numedges = LittleShort(ins->numedges);
			planenum = LittleShort(ins->planenum);
			side = LittleShort(ins->side);
			texinfon = LittleShort (ins->texinfo);
			for (i=0 ; i<4 ; i++)
				out->styles[i] = ((ins->styles[i]==INVALID_LIGHTSTYLE_OLD)?INVALID_LIGHTSTYLE:ins->styles[i]);
			lofs = LittleLong(ins->lightofs);
			ins++;
		}
		shift = defaultshift;
		//bspx overrides (for lmscale)
		if (lmshift)
			shift = lmshift[surfnum];
		if (lmoffset)
			lofs = LittleLong(lmoffset[surfnum]);
		if (lmstyle16)
			for (i=0 ; i<stylesperface ; i++)
				out->styles[i] = lmstyle16[surfnum*stylesperface+i];
		else if (lmstyle8)
			for (i=0 ; i<stylesperface ; i++)
			{
				out->styles[i] = lmstyle8[surfnum*stylesperface+i];
				if (out->styles[i] == INVALID_LIGHTSTYLE_OLD)
					out->styles[i] = INVALID_LIGHTSTYLE;
			}
		for ( ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = INVALID_LIGHTSTYLE;

		out->flags = 0;

		if (side)
			out->flags |= SURF_PLANEBACK;

		out->plane = loadmodel->planes + planenum;
		out->texinfo = loadmodel->texinfo + texinfon;

		if (decoupledlm)
		{
			lofs = LittleLong(decoupledlm->lmoffset);
			out->extents[0] = (unsigned short)LittleShort(decoupledlm->lmsize[0]) - 1;
			out->extents[1] = (unsigned short)LittleShort(decoupledlm->lmsize[1]) - 1;
			out->lmvecs[0][0] = LittleFloat(decoupledlm->lmvecs[0][0]);
			out->lmvecs[0][1] = LittleFloat(decoupledlm->lmvecs[0][1]);
			out->lmvecs[0][2] = LittleFloat(decoupledlm->lmvecs[0][2]);
			out->lmvecs[0][3] = LittleFloat(decoupledlm->lmvecs[0][3]);
			out->lmvecs[1][0] = LittleFloat(decoupledlm->lmvecs[1][0]);
			out->lmvecs[1][1] = LittleFloat(decoupledlm->lmvecs[1][1]);
			out->lmvecs[1][2] = LittleFloat(decoupledlm->lmvecs[1][2]);
			out->lmvecs[1][3] = LittleFloat(decoupledlm->lmvecs[1][3]);
			out->lmvecscale[0] = 1.0f/VectorLength(out->lmvecs[0]);	//luxels->qu
			out->lmvecscale[1] = 1.0f/VectorLength(out->lmvecs[1]);
			decoupledlm++;

			//make sure we don't segfault even if the texture coords get crappified.
			if (out->extents[0] >= LMBLOCK_WIDTH || out->extents[1] >= LMBLOCK_HEIGHT)
			{
				Con_Warning("%s: Bad surface extents (%i*%i, max %i*%u).\n", scalebuf, out->extents[0], out->extents[1], LMBLOCK_WIDTH, LMBLOCK_HEIGHT);
				out->extents[0] = out->extents[1] = 1;
			}
		}
		else
			CalcSurfaceExtents (out, shift);

		is_world_surface = world_count > 0 && surfnum >= world_first && surfnum < world_end;
		calc_surface_area = is_world_surface && out->plane && out->texinfo && out->texinfo->texture;
		if (calc_surface_area)
		{
			if (Mod_IsToolTexture(out->texinfo->texture->name))
				calc_surface_area = false;
		}
		surface_area = Mod_CalcSurfaceBoundsAndArea (out, calc_surface_area); //johnfitz -- for per-surface frustum culling

	// lighting info
		if (loadmodel->bspversion == BSPVERSION_QUAKE64)
			lofs /= 2; // Q64 samples are 16bits instead 8 in normal Quake 

		for (facestyles = 0 ; facestyles<MAXLIGHTMAPS && out->styles[facestyles] != INVALID_LIGHTSTYLE ; facestyles++)
			;	//count the styles so we can bound-check properly.
		if (lofs == -1)
			out->samples = NULL;
		else if (lofs+facestyles*((out->extents[0])+1)*((out->extents[1])+1) > loadmodel->lightdatasamples)
			out->samples = NULL; //corrupt...
		else if (loadmodel->flags & MOD_HDRLIGHTING)
			out->samples = loadmodel->lightdata + (lofs * 4); //spike -- hdr lighting data is 4-aligned
		else
			out->samples = loadmodel->lightdata + (lofs * 3); //johnfitz -- lit support via lordhavoc (was "+ i")

		//johnfitz -- this section rewritten
		if (!q_strncasecmp(out->texinfo->texture->name,"sky",3)) // sky surface //also note -- was Q_strncmp, changed to match qbsp
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
		}
		else if (out->texinfo->texture->name[0] == '*') // warp surface
		{
			out->flags |= SURF_DRAWTURB;
			if (out->texinfo->flags & TEX_SPECIAL)
				out->flags |= SURF_DRAWTILED;	//unlit water
			out->lightmaptexturenum = -1;

			// detect special liquid types
			if (!strncmp (out->texinfo->texture->name, "*lava", 5) || !strncmp (out->texinfo->texture->name, "!lava", 5))
				out->flags |= SURF_DRAWLAVA;
			else if (!strncmp (out->texinfo->texture->name, "*slime", 6) || !strncmp (out->texinfo->texture->name, "!slime", 6))
				out->flags |= SURF_DRAWSLIME;
			else if (!strncmp (out->texinfo->texture->name, "*tele", 5) || !strncmp (out->texinfo->texture->name, "!tele", 5))
				out->flags |= SURF_DRAWTELE;
			else out->flags |= SURF_DRAWWATER;
		}
		else if (out->texinfo->texture->name[0] == '{') // ericw -- fence textures
		{
			out->flags |= SURF_DRAWFENCE;
		}
		else if (out->texinfo->flags & TEX_MISSING) // texture is missing from bsp
		{
			if (out->samples) //lightmapped
			{
				out->flags |= SURF_NOTEXTURE;
			}
			else // not lightmapped
			{
				out->flags |= (SURF_NOTEXTURE | SURF_DRAWTILED);
			}
		}
		//johnfitz

		if (calc_surface_area)
			Mod_AddMapSurfaceArea (out, surface_area, &total_area, &floor_area, &wall_area, &ceiling_area, &counted_faces);
	}

	loadmodel->total_surface_area = (float)total_area;
	loadmodel->floor_surface_area = (float)floor_area;
	loadmodel->wall_surface_area = (float)wall_area;
	loadmodel->ceiling_surface_area = (float)ceiling_area;
	loadmodel->counted_faces = counted_faces;
}

/*
=================
Mod_LoadNodes
=================
*/
static void Mod_LoadNodes_S (lump_t *l)
{
	int			i, j, count, p;
	dsnode_t	*in;
	mnode_t		*out;

	in = (dsnode_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mnode_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz -- warn mappers about exceeding old limits
	if (count > 32767)
		Con_DWarning ("%i nodes exceeds standard limit of 32767.\n", count);
	//johnfitz

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = (unsigned short)LittleShort (in->firstface); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = (unsigned short)LittleShort (in->numfaces); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = (unsigned short)LittleShort(in->children[j]);
			if (p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 65535 - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

static void Mod_LoadNodes_L1 (lump_t *l)
{
	int			i, j, count, p;
	dl1node_t	*in;
	mnode_t		*out;

	in = (dl1node_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = (mnode_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleLong (in->firstface); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = LittleLong (in->numfaces); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = LittleLong(in->children[j]);
			if (p >= 0 && p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 0xffffffff - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

static void Mod_LoadNodes_L2 (lump_t *l)
{
	int			i, j, count, p;
	dl2node_t	*in;
	mnode_t		*out;

	in = (dl2node_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = (mnode_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleFloat (in->mins[j]);
			out->minmaxs[3+j] = LittleFloat (in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleLong (in->firstface); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = LittleLong (in->numfaces); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = LittleLong(in->children[j]);
			if (p > 0 && p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 0xffffffff - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

static void Mod_LoadNodes (lump_t *l, int bsp2)
{
	if (bsp2 == 2)
		Mod_LoadNodes_L2(l);
	else if (bsp2)
		Mod_LoadNodes_L1(l);
	else
		Mod_LoadNodes_S(l);
}

static void Mod_ProcessLeafs_S (dsleaf_t *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(*in))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);
	count = filelen / sizeof(*in);
	out = (mleaf_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz
	if (count > 32767)
		Host_Error ("Mod_LoadLeafs: %i leafs exceeds limit of 32767.", count);
	//johnfitz

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + (unsigned short)LittleShort(in->firstmarksurface); //johnfitz -- unsigned short
		out->nummarksurfaces = (unsigned short)LittleShort(in->nummarksurfaces); //johnfitz -- unsigned short

		p = LittleLong(in->visofs);
		out->compressed_vis = Mod_LeafVisData (p);
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		if (out->contents == CONTENTS_WATER || out->contents == CONTENTS_SLIME || out->contents == CONTENTS_LAVA) // woods #caustics
		{
			for (j = 0; j < out->nummarksurfaces; j++)
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
	}
}
}

static void Mod_ProcessLeafs_L1 (dl1leaf_t *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(*in))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);

	count = filelen / sizeof(*in);

	out = (mleaf_t *) Hunk_AllocName (count * sizeof(*out), loadname);

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + LittleLong(in->firstmarksurface); //johnfitz -- unsigned short
		out->nummarksurfaces = LittleLong(in->nummarksurfaces); //johnfitz -- unsigned short

		p = LittleLong(in->visofs);
		out->compressed_vis = Mod_LeafVisData (p);
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		if (out->contents == CONTENTS_WATER || out->contents == CONTENTS_SLIME || out->contents == CONTENTS_LAVA) // woods #caustics
		{
			for (j = 0; j < out->nummarksurfaces; j++)
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
		}
	}
}

static void Mod_ProcessLeafs_L2 (dl2leaf_t *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(*in))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);

	count = filelen / sizeof(*in);

	out = (mleaf_t *) Hunk_AllocName (count * sizeof(*out), loadname);

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleFloat (in->mins[j]);
			out->minmaxs[3+j] = LittleFloat (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + LittleLong(in->firstmarksurface); //johnfitz -- unsigned short
		out->nummarksurfaces = LittleLong(in->nummarksurfaces); //johnfitz -- unsigned short

		p = LittleLong(in->visofs);
		out->compressed_vis = Mod_LeafVisData (p);
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		if (out->contents == CONTENTS_WATER || out->contents == CONTENTS_SLIME || out->contents == CONTENTS_LAVA) // woods #caustics
		{
			for (j = 0; j < out->nummarksurfaces; j++)
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
		}
	}
}

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs (lump_t *l, int bsp2)
{
	void *in = (void *)(mod_base + l->fileofs);

	if (bsp2 == 2)
		Mod_ProcessLeafs_L2 ((dl2leaf_t *)in, l->filelen);
	else if (bsp2)
		Mod_ProcessLeafs_L1 ((dl1leaf_t *)in, l->filelen);
	else
		Mod_ProcessLeafs_S  ((dsleaf_t *) in, l->filelen);
}

void Mod_CheckWaterVis(void)
{
	mleaf_t		*leaf, *other;
	int i, j, k;
	int numclusters = loadmodel->submodels[0].visleafs;
	int contentfound = 0;
	int contenttransparent = 0;
	int contenttype;
	unsigned hascontents = 0;

	if (r_novis.value)
	{	//all can be
		loadmodel->contentstransparent = (SURF_DRAWWATER|SURF_DRAWTELE|SURF_DRAWSLIME|SURF_DRAWLAVA);
		return;
	}

	//pvs is 1-based. leaf 0 sees all (the solid leaf).
	//leaf 0 has no pvs, and does not appear in other leafs either, so watch out for the biases.
	for (i=0,leaf=loadmodel->leafs+1 ; i<numclusters ; i++, leaf++)
	{
		byte *vis;
		if (leaf->contents < 0)	//err... wtf?
			hascontents |= 1u<<-leaf->contents;
		if (leaf->contents == CONTENTS_WATER)
		{
			if ((contenttransparent & (SURF_DRAWWATER|SURF_DRAWTELE))==(SURF_DRAWWATER|SURF_DRAWTELE))
				continue;
			//this check is somewhat risky, but we should be able to get away with it.
			for (contenttype = 0, j = 0; j < leaf->nummarksurfaces; j++)
				if (leaf->firstmarksurface[j]->flags & (SURF_DRAWWATER|SURF_DRAWTELE))
				{
					contenttype = leaf->firstmarksurface[j]->flags & (SURF_DRAWWATER|SURF_DRAWTELE);
					break;
				}
			//its possible that this leaf has absolutely no surfaces in it, turb or otherwise.
			if (contenttype == 0)
				continue;
		}
		else if (leaf->contents == CONTENTS_SLIME)
			contenttype = SURF_DRAWSLIME;
		else if (leaf->contents == CONTENTS_LAVA)
			contenttype = SURF_DRAWLAVA;
		//fixme: tele
		else
			continue;
		if (contenttransparent & contenttype)
		{
			nextleaf:
			continue;	//found one of this type already
		}
		contentfound |= contenttype;
		vis = Mod_DecompressVis(leaf->compressed_vis, loadmodel);
		for (j = 0; j < (numclusters+7)/8; j++)
		{
			if (vis[j])
			{
				for (k = 0; k < 8; k++)
				{
					if (vis[j] & (1u<<k))
					{
						other = &loadmodel->leafs[(j<<3)+k+1];
						if (leaf->contents != other->contents)
						{
//							Con_Printf("%p:%i sees %p:%i\n", leaf, leaf->contents, other, other->contents);
							contenttransparent |= contenttype;
							goto nextleaf;
						}
					}
				}
			}
		}
	}

	if (!contenttransparent)
	{	//no water leaf saw a non-water leaf
		//but only warn when there's actually water somewhere there...
		if (hascontents & ((1<<-CONTENTS_WATER)
						|  (1<<-CONTENTS_SLIME)
						|  (1<<-CONTENTS_LAVA)))
			Con_DPrintf("%s is not watervised\n", loadmodel->name);
	}
	else
	{
		Con_DPrintf2("%s is vised for transparent", loadmodel->name);
		if (contenttransparent & SURF_DRAWWATER)
			Con_DPrintf2(" water");
		if (contenttransparent & SURF_DRAWTELE)
			Con_DPrintf2(" tele");
		if (contenttransparent & SURF_DRAWLAVA)
			Con_DPrintf2(" lava");
		if (contenttransparent & SURF_DRAWSLIME)
			Con_DPrintf2(" slime");
		Con_DPrintf2("\n");
	}
	//any types that we didn't find are assumed to be transparent.
	//this allows submodels to work okay (eg: ad uses func_illusionary teleporters for some reason).
	loadmodel->contentstransparent = contenttransparent | (~contentfound & (SURF_DRAWWATER|SURF_DRAWTELE|SURF_DRAWSLIME|SURF_DRAWLAVA));
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes (lump_t *l, qboolean bsp2)
{
	dsclipnode_t *ins;
	dlclipnode_t *inl;

	mclipnode_t *out; //johnfitz -- was dclipnode_t
	int			i, count;
	hull_t		*hull;

	if (bsp2)
	{
		ins = NULL;
		inl = (dlclipnode_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*inl))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*inl);
	}
	else
	{
		ins = (dsclipnode_t *)(mod_base + l->fileofs);
		inl = NULL;
		if (l->filelen % sizeof(*ins))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*ins);
	}
	if (count)
		out = (mclipnode_t *) Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);
	else
		out = NULL;	//will use rnodes.

	//johnfitz -- warn about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i clipnodes exceeds standard limit of 32767.\n", count);
	//johnfitz

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &loadmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	if (bsp2)
	{
		for (i=0 ; i<count ; i++, out++, inl++)
		{
			out->planenum = LittleLong(inl->planenum);

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			out->children[0] = LittleLong(inl->children[0]);
			out->children[1] = LittleLong(inl->children[1]);
			//Spike: FIXME: bounds check
		}
	}
	else
	{
		for (i=0 ; i<count ; i++, out++, ins++)
		{
			out->planenum = LittleLong(ins->planenum);

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			//johnfitz -- support clipnodes > 32k
			out->children[0] = (unsigned short)LittleShort(ins->children[0]);
			out->children[1] = (unsigned short)LittleShort(ins->children[1]);

			if (out->children[0] >= count)
				out->children[0] -= 65536;
			if (out->children[1] >= count)
				out->children[1] -= 65536;
			//johnfitz
		}
	}
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	mclipnode_t *out; //johnfitz -- was dclipnode_t
	int			i, j, count;
	hull_t		*hull;

	hull = &loadmodel->hulls[0];

	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = (mclipnode_t *) Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;
		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - loadmodel->nodes;
		}
	}

	//if qbsp was run with -noclip, make sure the extra hulls use the rnodes instead of the missing clipnodes
	//this won't 'fix' it, but it will stop it from crashing if it was just quickly built for debugging or whatever.
	if (!loadmodel->hulls[1].clipnodes)
	{	//hulls will be point-sized.
		//bias that point so that its mid,mid,bottom instead of at the absmin or origin. this will retain view offsets.
		loadmodel->hulls[1].clip_maxs[2] -= loadmodel->hulls[1].clip_mins[2];
		loadmodel->hulls[1].clip_mins[2] = 0;
		loadmodel->hulls[1].clipnodes = hull->clipnodes;
		loadmodel->hulls[1].firstclipnode = hull->firstclipnode;
		loadmodel->hulls[1].lastclipnode = hull->lastclipnode;
	}
	if (!loadmodel->hulls[2].clipnodes)
	{
		loadmodel->hulls[2].clip_maxs[2] -= loadmodel->hulls[2].clip_mins[2];
		loadmodel->hulls[2].clip_mins[2] = 0;
		loadmodel->hulls[2].clipnodes = loadmodel->hulls[1].clipnodes;
		loadmodel->hulls[2].firstclipnode = loadmodel->hulls[1].firstclipnode;
		loadmodel->hulls[2].lastclipnode = loadmodel->hulls[1].lastclipnode;
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
static void Mod_LoadMarksurfaces (lump_t *l, int bsp2)
{
	int		i, j, count;
	msurface_t **out;
	if (bsp2)
	{
		unsigned int *in = (unsigned int *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (msurface_t **)Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		for (i=0 ; i<count ; i++)
		{
			j = LittleLong(in[i]);
			if (j >= loadmodel->numsurfaces)
				Host_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = loadmodel->surfaces + j;
		}
	}
	else
	{
		short *in = (short *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (msurface_t **)Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		//johnfitz -- warn mappers about exceeding old limits
		if (count > 32767)
			Con_DWarning ("%i marksurfaces exceeds standard limit of 32767.\n", count);
		//johnfitz

		for (i=0 ; i<count ; i++)
		{
			j = (unsigned short)LittleShort(in[i]); //johnfitz -- explicit cast as unsigned short
			if (j >= loadmodel->numsurfaces)
				Sys_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = loadmodel->surfaces + j;
		}
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
static void Mod_LoadSurfedges (lump_t *l)
{
	int		i, count;
	int		*in, *out;

	in = (int *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (int *) Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for (i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
static void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;

	in = (dplane_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mplane_t *) Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
		out->pad[0] = out->pad[1] = 0;
	}
}

/*
=================
RadiusFromBounds
=================
*/
static float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength (corner);
}

static qboolean Mod_BSPLumpValid (size_t filesize, const lump_t *lump, size_t itemsize, int *count)
{
	if (!lump)
		return false;

	if ((size_t)lump->fileofs > filesize || (size_t)lump->filelen > filesize - (size_t)lump->fileofs)
		return false;

	if (itemsize && (lump->filelen % itemsize))
		return false;

	if (count)
		*count = itemsize ? (int)(lump->filelen / itemsize) : 0;

	return true;
}

static qboolean Mod_BSPTextureName (const byte *modbase, const lump_t *textures_lump, const texinfo_t *info, char name[17])
{
	const dmiptexlump_t *miptex_lump;
	const miptex_t *tex;
	int nummiptex, miptex;
	int dataofs;
	size_t header_size;
	const char *fallback;

	name[0] = '\0';
	if (!info)
		return false;

	miptex = LittleLong(info->miptex);
	fallback = (LittleLong(info->flags) & TEX_SPECIAL) ? "notexture2" : "notexture";

	if (!textures_lump->filelen || (size_t)textures_lump->filelen < sizeof(int))
	{
		q_strlcpy(name, fallback, 17);
		return true;
	}

	miptex_lump = (const dmiptexlump_t *)(modbase + textures_lump->fileofs);
	nummiptex = LittleLong(miptex_lump->nummiptex);
	if (nummiptex <= 0 || miptex < 0 || miptex >= nummiptex)
	{
		q_strlcpy(name, fallback, 17);
		return true;
	}

	header_size = sizeof(miptex_lump->nummiptex) + (size_t)nummiptex * sizeof(miptex_lump->dataofs[0]);
	if (header_size > textures_lump->filelen)
		return false;
	dataofs = LittleLong(miptex_lump->dataofs[miptex]);
	if (dataofs < 0 || (size_t)dataofs + 16 > textures_lump->filelen)
	{
		q_strlcpy(name, fallback, 17);
		return true;
	}

	tex = (const miptex_t *)((const byte *)miptex_lump + dataofs);
	memcpy(name, tex->name, 16);
	name[16] = '\0';

	return true;
}

static qboolean Mod_BSPGetVertexPosition (const dvertex_t *vertexes, int numvertexes,
	const dsedge_t *edges16, const dledge_t *edges32, int numedges,
	const int *surfedges, int numsurfedges, int surfedgeindex, qboolean bsp2, double out[3])
{
	int edge, edgeindex, vertslot, vertindex;

	if (surfedgeindex < 0 || surfedgeindex >= numsurfedges)
		return false;

	edge = LittleLong(surfedges[surfedgeindex]);
	if (edge >= 0)
	{
		edgeindex = edge;
		vertslot = 0;
	}
	else
	{
		edgeindex = -edge;
		vertslot = 1;
	}

	if (edgeindex < 0 || edgeindex >= numedges)
		return false;

	vertindex = bsp2 ? LittleLong(edges32[edgeindex].v[vertslot]) : (unsigned short)LittleShort(edges16[edgeindex].v[vertslot]);
	if (vertindex < 0 || vertindex >= numvertexes)
		return false;

	out[0] = LittleFloat(vertexes[vertindex].point[0]);
	out[1] = LittleFloat(vertexes[vertindex].point[1]);
	out[2] = LittleFloat(vertexes[vertindex].point[2]);

	return true;
}

static float Mod_CalcBSPSurfaceArea (const dvertex_t *vertexes, int numvertexes,
	const dsedge_t *edges16, const dledge_t *edges32, int numedges,
	const int *surfedges, int numsurfedges, int firstedge, int face_numedges, qboolean bsp2)
{
	double sum[3] = {0.0, 0.0, 0.0};
	double v0[3];
	int i;

	if (face_numedges < 3)
		return 0.0f;
	if (!Mod_BSPGetVertexPosition(vertexes, numvertexes, edges16, edges32, numedges,
		surfedges, numsurfedges, firstedge, bsp2, v0))
		return 0.0f;

	for (i = 1; i < face_numedges - 1; ++i)
	{
		double a[3], b[3], v1[3], v2[3];

		if (!Mod_BSPGetVertexPosition(vertexes, numvertexes, edges16, edges32, numedges,
			surfedges, numsurfedges, firstedge + i, bsp2, v1))
			return 0.0f;
		if (!Mod_BSPGetVertexPosition(vertexes, numvertexes, edges16, edges32, numedges,
			surfedges, numsurfedges, firstedge + i + 1, bsp2, v2))
			return 0.0f;

		a[0] = v1[0] - v0[0];
		a[1] = v1[1] - v0[1];
		a[2] = v1[2] - v0[2];
		b[0] = v2[0] - v0[0];
		b[1] = v2[1] - v0[1];
		b[2] = v2[2] - v0[2];

		sum[0] += a[1] * b[2] - a[2] * b[1];
		sum[1] += a[2] * b[0] - a[0] * b[2];
		sum[2] += a[0] * b[1] - a[1] * b[0];
	}

	return (float)(0.5 * sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]));
}

qboolean Mod_CalcBSPFileSurfaceAreas (const char *path, map_surface_areas_t *areas)
{
	byte *buffer;
	size_t filesize;
	dheader_t header;
	const dmodelq1_t *models;
	const dplane_t *planes;
	const dvertex_t *vertexes;
	const texinfo_t *texinfo;
	const dsface_t *faces16 = NULL;
	const dlface_t *faces32 = NULL;
	const dsedge_t *edges16 = NULL;
	const dledge_t *edges32 = NULL;
	const int *surfedges;
	int bsp2;
	int nummodels, numplanes, numvertexes, numtexinfo, numfaces, numedges, numsurfedges;
	int firstface, count, i;
	double total_area = 0.0;
	double floor_area = 0.0;
	double wall_area = 0.0;
	double ceiling_area = 0.0;
	int counted_faces = 0;
	qboolean ok = false;

	if (!path || !areas)
		return false;

	memset(areas, 0, sizeof(*areas));

	buffer = COM_LoadMallocFile(path, NULL);
	if (!buffer)
		return false;

	filesize = (size_t)com_filesize;
	if (filesize < sizeof(header))
		goto done;

	memcpy(&header, buffer, sizeof(header));
	header.version = LittleLong(header.version);

	switch (header.version)
	{
	case BSPVERSION:
	case BSPVERSION_QUAKE64:
		bsp2 = false;
		break;
	case BSP2VERSION_2PSB:
	case BSP2VERSION_BSP2:
		bsp2 = true;
		break;
	default:
		goto done;
	}

	for (i = 0; i < HEADER_LUMPS; ++i)
	{
		header.lumps[i].fileofs = LittleLong(header.lumps[i].fileofs);
		header.lumps[i].filelen = LittleLong(header.lumps[i].filelen);
	}

	if (!Mod_BSPLumpValid(filesize, &header.lumps[LUMP_MODELS], sizeof(*models), &nummodels) || nummodels <= 0)
		goto done;
	if (!Mod_BSPLumpValid(filesize, &header.lumps[LUMP_PLANES], sizeof(*planes), &numplanes) || numplanes <= 0)
		goto done;
	if (!Mod_BSPLumpValid(filesize, &header.lumps[LUMP_VERTEXES], sizeof(*vertexes), &numvertexes) || numvertexes <= 0)
		goto done;
	if (!Mod_BSPLumpValid(filesize, &header.lumps[LUMP_TEXINFO], sizeof(*texinfo), &numtexinfo) || numtexinfo <= 0)
		goto done;
	if (!Mod_BSPLumpValid(filesize, &header.lumps[LUMP_SURFEDGES], sizeof(*surfedges), &numsurfedges) || numsurfedges <= 0)
		goto done;
	if (!Mod_BSPLumpValid(filesize, &header.lumps[LUMP_EDGES], bsp2 ? sizeof(*edges32) : sizeof(*edges16), &numedges) || numedges <= 0)
		goto done;
	if (!Mod_BSPLumpValid(filesize, &header.lumps[LUMP_FACES], bsp2 ? sizeof(*faces32) : sizeof(*faces16), &numfaces) || numfaces < 0)
		goto done;
	if (header.lumps[LUMP_TEXTURES].filelen && !Mod_BSPLumpValid(filesize, &header.lumps[LUMP_TEXTURES], 0, NULL))
		goto done;

	models = (const dmodelq1_t *)(buffer + header.lumps[LUMP_MODELS].fileofs);
	planes = (const dplane_t *)(buffer + header.lumps[LUMP_PLANES].fileofs);
	vertexes = (const dvertex_t *)(buffer + header.lumps[LUMP_VERTEXES].fileofs);
	texinfo = (const texinfo_t *)(buffer + header.lumps[LUMP_TEXINFO].fileofs);
	surfedges = (const int *)(buffer + header.lumps[LUMP_SURFEDGES].fileofs);
	if (bsp2)
		edges32 = (const dledge_t *)(buffer + header.lumps[LUMP_EDGES].fileofs);
	else
		edges16 = (const dsedge_t *)(buffer + header.lumps[LUMP_EDGES].fileofs);
	if (bsp2)
		faces32 = (const dlface_t *)(buffer + header.lumps[LUMP_FACES].fileofs);
	else
		faces16 = (const dsface_t *)(buffer + header.lumps[LUMP_FACES].fileofs);

	firstface = LittleLong(models[0].firstface);
	count = LittleLong(models[0].numfaces);
	if (firstface < 0 || count < 0 || firstface > numfaces || count > numfaces - firstface)
		goto done;

	areas->total_faces = count;

	for (i = 0; i < count; ++i)
	{
		int planenum, side, face_firstedge, face_numedges, texinfo_idx;
		float nz, area;
		char texname[17];

		if (bsp2)
		{
			const dlface_t *face = &faces32[firstface + i];
			planenum = LittleLong(face->planenum);
			side = LittleLong(face->side);
			face_firstedge = LittleLong(face->firstedge);
			face_numedges = LittleLong(face->numedges);
			texinfo_idx = LittleLong(face->texinfo);
		}
		else
		{
			const dsface_t *face = &faces16[firstface + i];
			planenum = LittleShort(face->planenum);
			side = LittleShort(face->side);
			face_firstedge = LittleLong(face->firstedge);
			face_numedges = LittleShort(face->numedges);
			texinfo_idx = LittleShort(face->texinfo);
		}

		if (planenum < 0 || planenum >= numplanes)
			continue;
		if (texinfo_idx < 0 || texinfo_idx >= numtexinfo)
			continue;
		if (face_firstedge < 0 || face_numedges < 3 || face_firstedge > numsurfedges || face_numedges > numsurfedges - face_firstedge)
			continue;
		if (!Mod_BSPTextureName(buffer, &header.lumps[LUMP_TEXTURES], &texinfo[texinfo_idx], texname))
			continue;
		if (!q_strncasecmp(texname, "sky", 3))
			continue;
		if (Mod_IsToolTexture(texname))
			continue;

		area = Mod_CalcBSPSurfaceArea(vertexes, numvertexes, edges16, edges32, numedges,
			surfedges, numsurfedges, face_firstedge, face_numedges, bsp2);
		if (area <= 0.0f)
			continue;

		nz = LittleFloat(planes[planenum].normal[2]);
		if (side)
			nz = -nz;

		total_area += area;
		counted_faces++;

		if (nz > MIN_FLOOR_NORMAL)
			floor_area += area;
		else if (nz < -MIN_FLOOR_NORMAL)
			ceiling_area += area;
		else
			wall_area += area;
	}

	areas->total_surface_area = (float)total_area;
	areas->floor_surface_area = (float)floor_area;
	areas->wall_surface_area = (float)wall_area;
	areas->ceiling_surface_area = (float)ceiling_area;
	areas->counted_faces = counted_faces;
	ok = true;

done:
	free(buffer);
	return ok;
}

/*
=================
Mod_LoadSubmodels
=================
*/
static void Mod_LoadSubmodels (lump_t *l)
{
	mmodel_t	*out;
	size_t			i, j, count;

	//detect whether this is a hexen2 8-hull map or a quake 4-hull map
	dmodelq1_t	*inq1 = (dmodelq1_t *)(mod_base + l->fileofs);
	dmodelh2_t	*inh2 = (dmodelh2_t *)(mod_base + l->fileofs);
	//the numfaces is a bit of a hack. hexen2 only actually uses 6 of its 8 hulls and we depend upon this.
	//this means that the 7th and 8th are null. q1.numfaces of the world equates to h2.hull[6], so should have a value for q1, and be 0 for hexen2.
	//this should work even for maps that have enough submodels to realign the size.
	//note that even if the map loads, you're on your own regarding the palette (hurrah for retexturing projects?).
	//fixme: we don't fix up the clipnodes yet, the player is fine, shamblers/ogres/fiends/vores will have issues.
	//unfortunately c doesn't do templates, which would make all this code a bit less copypastay
	if ((size_t)l->filelen >= sizeof(*inh2) && !(l->filelen % sizeof(*inh2)) && !inq1->numfaces && inq1[1].firstface)
	{
		dmodelh2_t	*in = inh2;
		if (l->filelen % sizeof(*in))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (mmodel_t *) Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

		loadmodel->submodels = out;
		loadmodel->numsubmodels = count;

		for (i=0 ; i<count ; i++, in++, out++)
		{
			for (j=0 ; j<3 ; j++)
			{	// spread the mins / maxs by a pixel
				out->mins[j] = LittleFloat (in->mins[j]) - 1;
				out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
				out->origin[j] = LittleFloat (in->origin[j]);
			}
			for (j=0 ; j<MAX_MAP_HULLS && j<sizeof(in->headnode)/sizeof(in->headnode[0]) ; j++)
				out->headnode[j] = LittleLong (in->headnode[j]);
			for (; j<MAX_MAP_HULLS ; j++)
				out->headnode[j] = 0;
			out->visleafs = LittleLong (in->visleafs);
			out->firstface = LittleLong (in->firstface);
			out->numfaces = LittleLong (in->numfaces);
		}
	}
	else
	{
		dmodelq1_t	*in = inq1;
		if (l->filelen % sizeof(*in))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (mmodel_t *) Hunk_AllocNameNoFill ( count*sizeof(*out), loadname);

		loadmodel->submodels = out;
		loadmodel->numsubmodels = count;

		for (i=0 ; i<count ; i++, in++, out++)
		{
			for (j=0 ; j<3 ; j++)
			{	// spread the mins / maxs by a pixel
				out->mins[j] = LittleFloat (in->mins[j]) - 1;
				out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
				out->origin[j] = LittleFloat (in->origin[j]);
			}
			for (j=0 ; j<MAX_MAP_HULLS && j<sizeof(in->headnode)/sizeof(in->headnode[0]) ; j++)
				out->headnode[j] = LittleLong (in->headnode[j]);
			for (; j<MAX_MAP_HULLS ; j++)
				out->headnode[j] = 0;
			out->visleafs = LittleLong (in->visleafs);
			out->firstface = LittleLong (in->firstface);
			out->numfaces = LittleLong (in->numfaces);
		}
	}

	// johnfitz -- check world visleafs -- adapted from bjp
	out = loadmodel->submodels;

	if (out->visleafs > 8192)
		Con_DWarning ("%i visleafs exceeds standard limit of 8192.\n", out->visleafs);
	//johnfitz
}

/*
=================
Mod_BoundsFromClipNode -- johnfitz

update the model's clipmins and clipmaxs based on each node's plane.

This works because of the way brushes are expanded in hull generation.
Each brush will include all six axial planes, which bound that brush.
Therefore, the bounding box of the hull can be constructed entirely
from axial planes found in the clipnodes for that hull.
=================
*/
#if 0 /* disabled for now -- see in Mod_SetupSubmodels()  */
static void Mod_BoundsFromClipNode (qmodel_t *mod, int hull, int nodenum)
{
	mplane_t	*plane;
	mclipnode_t	*node;

	if (nodenum < 0)
		return; //hit a leafnode

	node = &mod->clipnodes[nodenum];
	plane = mod->hulls[hull].planes + node->planenum;
	switch (plane->type)
	{

	case PLANE_X:
		if (plane->signbits == 1)
			mod->clipmins[0] = q_min(mod->clipmins[0], -plane->dist - mod->hulls[hull].clip_mins[0]);
		else
			mod->clipmaxs[0] = q_max(mod->clipmaxs[0], plane->dist - mod->hulls[hull].clip_maxs[0]);
		break;
	case PLANE_Y:
		if (plane->signbits == 2)
			mod->clipmins[1] = q_min(mod->clipmins[1], -plane->dist - mod->hulls[hull].clip_mins[1]);
		else
			mod->clipmaxs[1] = q_max(mod->clipmaxs[1], plane->dist - mod->hulls[hull].clip_maxs[1]);
		break;
	case PLANE_Z:
		if (plane->signbits == 4)
			mod->clipmins[2] = q_min(mod->clipmins[2], -plane->dist - mod->hulls[hull].clip_mins[2]);
		else
			mod->clipmaxs[2] = q_max(mod->clipmaxs[2], plane->dist - mod->hulls[hull].clip_maxs[2]);
		break;
	default:
		//skip nonaxial planes; don't need them
		break;
	}

	Mod_BoundsFromClipNode (mod, hull, node->children[0]);
	Mod_BoundsFromClipNode (mod, hull, node->children[1]);
}
#endif /* #if 0 */

/* EXTERNAL VIS FILE SUPPORT:
 */
typedef struct vispatch_s
{
	char	mapname[32];
	int	filelen;	// length of data after header (VIS+Leafs)
} vispatch_t;
#define VISPATCH_HEADER_LEN 36

static FILE *Mod_FindVisibilityExternal(int *entrylen)
{
	vispatch_t header;
	char visfilename[MAX_QPATH];
	char mapname[sizeof(header.mapname) + 1];
	const char* shortname;
	unsigned int path_id;
	FILE *f;
	long basepos;
	int filesize;
	size_t offset;

	*entrylen = 0;

	q_snprintf(visfilename, sizeof(visfilename), "maps/%s.vis", loadname);
	filesize = COM_FOpenFile(visfilename, &f, &path_id);
	if (filesize < 0)
	{
		Con_DPrintf("%s not found, trying ", visfilename);
		q_snprintf(visfilename, sizeof(visfilename), "%s.vis", COM_SkipPath(com_gamedir));
		Con_DPrintf("%s\n", visfilename);
		filesize = COM_FOpenFile(visfilename, &f, &path_id);
		if (filesize < 0)
		{
			Con_DPrintf("external vis not found\n");
			return NULL;
		}
	}
	if (path_id < loadmodel->path_id)
	{
		fclose(f);
		Con_DPrintf("ignored %s from a gamedir with lower priority\n", visfilename);
		return NULL;
	}

	Con_DPrintf("Found external VIS %s\n", visfilename);

	shortname = COM_SkipPath(loadmodel->name);
	basepos = ftell(f);
	if (basepos < 0)
	{
		fclose(f);
		Con_Warning("Ignoring external VIS %s: couldn't determine file position.\n", visfilename);
		return NULL;
	}

	for (offset = 0; offset + VISPATCH_HEADER_LEN <= (size_t)filesize; )
	{
		if (fseek(f, basepos + (long)offset, SEEK_SET) != 0 ||
			fread(&header, 1, VISPATCH_HEADER_LEN, f) != VISPATCH_HEADER_LEN)
		{
			fclose(f);
			Con_Warning("Ignoring external VIS %s: couldn't read an entry header.\n", visfilename);
			return NULL;
		}

		header.filelen = LittleLong(header.filelen);
		if (header.filelen <= 0 ||
			(size_t)header.filelen > (size_t)filesize - offset - VISPATCH_HEADER_LEN)
		{	/* bad entry -- don't trust the rest. */
			fclose(f);
			Con_Warning("Ignoring external VIS %s: invalid entry length.\n", visfilename);
			return NULL;
		}

		memcpy(mapname, header.mapname, sizeof(header.mapname));
		mapname[sizeof(header.mapname)] = '\0';
		if (!q_strcasecmp(mapname, shortname))
		{
			*entrylen = header.filelen;
			return f;
		}

		offset += VISPATCH_HEADER_LEN + (size_t)header.filelen;
	}

	fclose(f);
	Con_DPrintf("%s not found in %s\n", shortname, visfilename);
	return NULL;
}

static qboolean Mod_ExternalLeafsMatchBSP(const dsleaf_t *leafs, int leaflen,
	const lump_t *bspleaflump, int vislen)
{
	const dsleaf_t *bspleafs;
	int i, count;

	if (leaflen != (int)bspleaflump->filelen || leaflen <= 0 ||
		leaflen % (int)sizeof(*leafs))
	{
		Con_Warning("Ignoring external VIS for %s: leaf count does not match the BSP; using BSP visibility.\n",
			loadmodel->name);
		return false;
	}

	bspleafs = (const dsleaf_t *)(mod_base + bspleaflump->fileofs);
	count = leaflen / sizeof(*leafs);
	if (!loadmodel->submodels || loadmodel->numsubmodels <= 0 ||
		count <= loadmodel->submodels[0].visleafs)
	{
		Con_Warning("Ignoring external VIS for %s: world leaf count is inconsistent; using BSP visibility.\n",
			loadmodel->name);
		return false;
	}

	for (i = 0; i < count; i++)
	{
		int visofs = LittleLong(leafs[i].visofs);
		unsigned int firstmark = (unsigned short)LittleShort(leafs[i].firstmarksurface);
		unsigned int nummarks = (unsigned short)LittleShort(leafs[i].nummarksurfaces);

		if (leafs[i].contents != bspleafs[i].contents ||
			memcmp(leafs[i].mins, bspleafs[i].mins, sizeof(leafs[i].mins)) ||
			memcmp(leafs[i].maxs, bspleafs[i].maxs, sizeof(leafs[i].maxs)) ||
			leafs[i].firstmarksurface != bspleafs[i].firstmarksurface ||
			leafs[i].nummarksurfaces != bspleafs[i].nummarksurfaces)
		{
			Con_Warning("Ignoring external VIS for %s: leaf %d does not match the BSP; using BSP visibility.\n",
				loadmodel->name, i);
			return false;
		}
		if (visofs < -1 || visofs >= vislen)
		{
			Con_Warning("Ignoring external VIS for %s: leaf %d has an invalid VIS offset; using BSP visibility.\n",
				loadmodel->name, i);
			return false;
		}
		if (firstmark > (unsigned int)loadmodel->nummarksurfaces ||
			nummarks > (unsigned int)loadmodel->nummarksurfaces - firstmark)
		{
			Con_Warning("Ignoring external VIS for %s: leaf %d has invalid marksurfaces; using BSP visibility.\n",
				loadmodel->name, i);
			return false;
		}
	}

	return true;
}

static qboolean Mod_LoadVisibilityExternal(FILE *f, int entrylen, lump_t *bspleaflump)
{
	byte *visdata, *hunkvisdata;
	dsleaf_t *leafdata;
	int vislen, leaflen;
	qboolean diagnosed, valid;

	visdata = NULL;
	leafdata = NULL;
	diagnosed = false;
	valid = false;
	if (entrylen < 2 * (int)sizeof(int) ||
		fread(&vislen, 1, sizeof(vislen), f) != sizeof(vislen))
		goto done;
	vislen = LittleLong(vislen);
	if (vislen <= 0 || vislen > entrylen - 2 * (int)sizeof(int))
		goto done;

	Con_DPrintf("...%d bytes visibility data\n", vislen);
	visdata = (byte *)malloc(vislen);
	if (!visdata || fread(visdata, 1, vislen, f) != (size_t)vislen)
		goto done;
	if (fread(&leaflen, 1, sizeof(leaflen), f) != sizeof(leaflen))
		goto done;
	leaflen = LittleLong(leaflen);
	if (leaflen <= 0 || leaflen != entrylen - 2 * (int)sizeof(int) - vislen)
		goto done;

	Con_DPrintf("...%d bytes leaf data\n", leaflen);
	leafdata = (dsleaf_t *)malloc(leaflen);
	if (!leafdata || fread(leafdata, 1, leaflen, f) != (size_t)leaflen)
		goto done;
	if (!Mod_ExternalLeafsMatchBSP(leafdata, leaflen, bspleaflump, vislen))
	{
		diagnosed = true;
		goto done;
	}

	hunkvisdata = (byte *)Hunk_AllocNameNoFill(vislen, "EXT_VIS");
	memcpy(hunkvisdata, visdata, vislen);
	loadmodel->viswarn = false;
	loadmodel->visdata = hunkvisdata;
	loadmodel->visdata_size = vislen;
	Mod_ProcessLeafs_S(leafdata, leaflen);
	valid = true;

done:
	free(leafdata);
	free(visdata);
	if (!valid && !diagnosed)
		Con_Warning("External VIS data for %s is invalid; using BSP visibility.\n",
			loadmodel->name);
	return valid;
}

/*
=================
Mod_LoadBrushModel
=================
*/
static void Mod_LoadBrushModel (qmodel_t *mod, void *buffer)
{
	int			i, j;
	int			bsp2;
	dheader_t	*header;
	mmodel_t 	*bm;
	float		radius; //johnfitz

	loadmodel->type = mod_brush;

	header = (dheader_t *)buffer;

	mod->bspversion = LittleLong (header->version);

	switch(mod->bspversion)
	{
	case BSPVERSION:
		bsp2 = false;
		break;
	case BSP2VERSION_2PSB:
		bsp2 = 1;	//first iteration
		break;
	case BSP2VERSION_BSP2:
		bsp2 = 2;	//sanitised revision
		break;
	case BSPVERSION_QUAKE64:
		bsp2 = false;
		break;
	default:
		loadmodel->type = mod_ext_invalid;
		Con_Warning ("Mod_LoadBrushModel: %s has unsupported version number (%i should be %i)\n", mod->name, mod->bspversion, BSPVERSION);
		return;
	}

// swap all the lumps
	mod_base = (byte *)header;

	for (i = 0; i < (int) sizeof(dheader_t) / 4; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

	Q1BSPX_Setup(mod, buffer, com_filesize, header->lumps, HEADER_LUMPS);

// load into heap

	{
	extern double com_findfile_time, texmgr_load_time;
	extern unsigned int com_findfile_calls, texmgr_load_calls;
	qboolean profile = developer.value != 0;
	double ff0 = 0, tm0 = 0;
	unsigned int ffc0 = 0, tmc0 = 0;
	double t0 = 0, t_geom = 0, t_tex = 0, t_light = 0, t_faces = 0;

	if (profile)
		t0 = Sys_DoubleTime ();
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES], bsp2);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	if (profile)
	{
		t_geom = Sys_DoubleTime ();
		ff0 = com_findfile_time;
		tm0 = texmgr_load_time;
		ffc0 = com_findfile_calls;
		tmc0 = texmgr_load_calls;
	}
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	if (profile)
	{
		t_tex = Sys_DoubleTime ();
		Con_DPrintf ("Mod_LoadTextures %s: findfile %.1fms (%u calls) texmgr %.1fms (%u calls)\n", mod->name,
			(com_findfile_time-ff0)*1000.0, com_findfile_calls-ffc0, (texmgr_load_time-tm0)*1000.0, texmgr_load_calls-tmc0);
	}
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	if (profile)
		t_light = Sys_DoubleTime ();
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);	//Spike: moved this earlier, so that we can parse worldspawn keys earlier.
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);	// faces use the world face range while loading
	Mod_LoadFaces (&header->lumps[LUMP_FACES], bsp2);
	if (profile)
		t_faces = Sys_DoubleTime ();
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES], bsp2);
	if (profile)
		Con_DPrintf ("Mod_LoadBrushModel %s: geom %.1fms tex %.1fms light %.1fms faces %.1fms\n",
			mod->name, (t_geom-t0)*1000.0, (t_tex-t_geom)*1000.0, (t_light-t_tex)*1000.0, (t_faces-t_light)*1000.0);
	}

	if (mod->bspversion == BSPVERSION && external_vis.value/* && sv.modelname[0] && !q_strcasecmp(loadname, sv.name)*/) // woods allow vis load online
	{
		FILE* fvis;
		int entrylen;
		Con_DPrintf("trying to open external vis file\n");
		fvis = Mod_FindVisibilityExternal(&entrylen);
		if (fvis) {
			qboolean valid = Mod_LoadVisibilityExternal(fvis, entrylen,
				&header->lumps[LUMP_LEAFS]);
			fclose(fvis);
			if (valid) {
				Con_DPrintf("found valid external .vis file for map\n");
				goto visdone;
			}
		}
	}

	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS], bsp2);
visdone:
	{
	qboolean profile = developer.value != 0;
	double t0 = 0, t_nodes = 0, t_wvis;

	if (profile)
		t0 = Sys_DoubleTime ();
	Mod_LoadNodes (&header->lumps[LUMP_NODES], bsp2);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES], bsp2);

	Mod_MakeHull0 ();

	mod->numframes = 2;		// regular and alternate animation

	if (profile)
		t_nodes = Sys_DoubleTime ();
	Mod_CheckWaterVis();
	if (profile)
	{
		t_wvis = Sys_DoubleTime ();
		Con_DPrintf ("Mod_LoadBrushModel %s: nodes %.1fms watervis %.1fms\n",
			mod->name, (t_nodes-t0)*1000.0, (t_wvis-t_nodes)*1000.0);
	}
	}

//
// set up the submodels (FIXME: this is confusing)
//

	mod->submodelof = loadmodel;

	// johnfitz -- okay, so that i stop getting confused every time i look at this loop, here's how it works:
	// we're looping through the submodels starting at 0.  Submodel 0 is the main model, so we don't have to
	// worry about clobbering data the first time through, since it's the same data.  At the end of the loop,
	// we create a new copy of the data to use the next time through.
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j=1 ; j<MAX_MAP_HULLS ; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			if (mod->hulls[j].clipnodes == mod->hulls[0].clipnodes)
				mod->hulls[j].lastclipnode = mod->hulls[0].lastclipnode;
			else
				mod->hulls[j].lastclipnode = mod->numclipnodes-1;
		}

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;
		mod->submodelof = loadmodel->submodelof;
		mod->submodelidx = i;

		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		//johnfitz -- calculate rotate bounds and yaw bounds
		radius = RadiusFromBounds (mod->mins, mod->maxs);
		mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = mod->ymaxs[0] = mod->ymaxs[1] = mod->ymaxs[2] = radius;
		mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = mod->ymins[0] = mod->ymins[1] = mod->ymins[2] = -radius;
		//johnfitz

		//johnfitz -- correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
		if (i > 0 || strcmp(mod->name, sv.modelname) != 0) //skip submodel 0 of sv.worldmodel, which is the actual world
		{
			// start with the hull0 bounds
			VectorCopy (mod->maxs, mod->clipmaxs);
			VectorCopy (mod->mins, mod->clipmins);

			// process hull1 (we don't need to process hull2 becuase there's
			// no such thing as a brush that appears in hull2 but not hull1)
			//Mod_BoundsFromClipNode (mod, 1, mod->hulls[1].firstclipnode); // (disabled for now becuase it fucks up on rotating models)
		}
		//johnfitz

		mod->numleafs = bm->visleafs;

		if (i < mod->numsubmodels-1)
		{	// duplicate the basic information
			char	name[12];

			sprintf (name, "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
			mod = loadmodel;

			Mod_SetExtraFlags(mod);
		}
	}
}

/*
=================
Mod_LoadMapDescription -- woods #mapdescriptions (ironwail port)

Parses the entity lump in the given map to find its worldspawn message
Writes at most maxchars bytes to dest, including the NUL terminator
Returns true if map is playable, false otherwise
=================
*/
qboolean Mod_LoadMapDescription(char* desc, size_t maxchars, const char* map)
{
	char		buf[4 * 1024];
	char		path[MAX_QPATH];
	const char* data;
	FILE* f;
	lump_t* entlump;
	dheader_t	header;
	int			i, j, k, filesize;
	qboolean	ret = false;
	const int MAX_DESC_LENGTH = 70;

	if (!maxchars)
		return false;
	*desc = '\0';

	if ((size_t)q_snprintf(path, sizeof(path), "maps/%s.bsp", map) >= sizeof(path))
		return false;

	filesize = COM_FOpenFile(path, &f, NULL);
	if (filesize <= (int)sizeof(header))
	{
		if (filesize != -1)
			fclose(f);
		return false;
	}

	if (fread(&header, sizeof(header), 1, f) != 1)
	{
		fclose(f);
		return false;
	}

	header.version = LittleLong(header.version);

	switch (header.version)
	{
	case BSPVERSION:
	case BSP2VERSION_2PSB:
	case BSP2VERSION_BSP2:
	case BSPVERSION_QUAKE64:
		break;
	default:
		fclose(f);
		return false;
	}

	for (i = 1; i < (int)(sizeof(header) / sizeof(int)); i++)
		((int*)&header)[i] = LittleLong(((int*)&header)[i]);

	entlump = &header.lumps[LUMP_ENTITIES];
	if ((int)entlump->filelen < 0 || (int)entlump->filelen >= filesize ||
		(int)entlump->fileofs < 0 || (int)entlump->fileofs + (int)entlump->filelen > filesize)
	{
		fclose(f);
		return false;
	}

	// if the entity lump is large enough we assume the map is playable
	// and only try to parse the first entity (worldspawn) for the map title
	if (entlump->filelen >= sizeof(buf))
	{
		ret = true;
		entlump->filelen = sizeof(buf) - 1;
	}

	fseek(f, entlump->fileofs - sizeof(header), SEEK_CUR);
	i = fread(buf, 1, entlump->filelen, f);
	fclose(f);

	if (i <= 0)
		return false;
	buf[i] = '\0';

	for (i = 0, data = buf; data; i++)
	{
		data = COM_Parse(data);
		if (!data || com_token[0] != '{')
			return ret;

		while (1)
		{
			qboolean is_message;
			qboolean is_classname;

			// parse key
			data = COM_Parse(data);
			if (!data)
				return ret;
			if (com_token[0] == '}')
				break;

			is_message = i == 0 && !strcmp(com_token, "message");
			is_classname = i != 0 && !strcmp(com_token, "classname");

			// parse value
			data = COM_ParseEx(data, CPE_ALLOWTRUNC);
			if (!data)
				return ret;

			if (is_message)
			{
				unsigned char* ch; // woods dequake
				for (ch = (unsigned char*)com_token; *ch; ch++)
					*ch = dequake[*ch];

				// copy map title and clean it up a bit
				for (j = k = 0; com_token[j] && (size_t)k + 1 < maxchars; j++)
				{					
					char c = com_token[j] & 0x7f;
					if (c == '\n' || c == '\r') // replace newlines with spaces
						c = ' ';
					else if (c == '\\' && com_token[j + 1] == 'n') // replace '\\' followed by 'n' with space
					{
						c = ' ';
						j++;
					}

					// remove leading spaces, replace consecutive spaces with single one
					if (c != ' ' || (k > 0 && desc[k - 1] != c))
						desc[k++] = c;
				}
				// remove trailing space, if any
				if (k > 0 && desc[k - 1] == ' ')
					--k;

				if (k < (int)maxchars)
					desc[k] = '\0';
				else
					desc[maxchars - 1] = '\0';

				if (strlen(desc) > (size_t)MAX_DESC_LENGTH)
				{
					size_t trunc_len = MAX_DESC_LENGTH;
					if (trunc_len + 1 > maxchars)
						trunc_len = maxchars - 1;

					if (trunc_len >= 3)
					{
						desc[trunc_len - 3] = '.';
						desc[trunc_len - 2] = '.';
						desc[trunc_len - 1] = '.';
						desc[trunc_len] = '\0';
					}
					else
					{
						desc[trunc_len] = '\0'; // not enough space even for '...'
					}
				}

				if (ret)
					return true;
			}
			else if (is_classname)
			{
#define CLASSNAME_STARTS_WITH(str)	(!strncmp (com_token, str, strlen (str)))
#define CLASSNAME_IS(str)			(!strcmp (com_token, str))

				if (CLASSNAME_STARTS_WITH("info_player_") ||
					CLASSNAME_STARTS_WITH("ammo_") ||
					CLASSNAME_STARTS_WITH("weapon_") ||
					CLASSNAME_STARTS_WITH("monster_") ||
					CLASSNAME_IS("trigger_changelevel"))
				{
					return true;
				}

#undef CLASSNAME_IS
#undef CLASSNAME_STARTS_WITH
			}
		}
	}

	return ret;
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t	*pheader;

stvert_t	stverts[MAXALIASVERTS];
mtriangle_t	*triangles;
int	max_triangles;

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t	*poseverts_mdl[MAXALIASFRAMES];
static int			posenum;

/*
=================
Mod_LoadAliasFrame
=================
*/
static void *Mod_LoadAliasFrame (void * pin, maliasframedesc_t *frame, int pvtype)
{
	trivertx_t		*pinframe;
	int				i;
	daliasframe_t	*pdaliasframe;

	if (posenum >= MAXALIASFRAMES)
		Sys_Error ("posenum >= MAXALIASFRAMES");

	pdaliasframe = (daliasframe_t *)pin;

	q_strlcpy (frame->name, pdaliasframe->name, sizeof (frame->name));
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about
		// endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}

	pinframe = (trivertx_t *)(pdaliasframe + 1);

	poseverts_mdl[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts*(pvtype==PV_QUAKEFORGE?2:1);

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
static void *Mod_LoadAliasGroup (void * pin,  maliasframedesc_t *frame, int pvtype)
{
	daliasgroup_t		*pingroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	void				*ptemp;

	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		if (posenum >= MAXALIASFRAMES) Sys_Error ("posenum >= MAXALIASFRAMES");

		poseverts_mdl[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);
		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts*(pvtype==PV_QUAKEFORGE?2:1);
	}

	return ptemp;
}

/*
===============
Mod_LoadAllSkins
===============
*/
static void *Mod_LoadAllSkins (int numskins, daliasskintype_t *pskintype)
{
	int			i, j, k, size, groupskins;
	char			name[MAX_QPATH];
	byte			*texels;
	daliasskingroup_t	*pinskingroup;
	daliasskininterval_t	*pinskinintervals;
	char			fbr_mask_name[MAX_QPATH]; //johnfitz -- added for fullbright support
	src_offset_t		offset; //johnfitz
	unsigned int		texflags = TEXPREF_PAD;

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d", numskins);

	size = pheader->skinwidth * pheader->skinheight;

	if (loadmodel->flags & MF_HOLEY)
		texflags |= TEXPREF_ALPHA;

	// alias skins need to floodfill both on initial load and following a vid_restart
	texflags |= TEXPREF_FLOODFILL;

	for (i=0 ; i<numskins ; i++)
	{
		if (pskintype->type == ALIAS_SKIN_SINGLE)
		{
			// save 8 bit texels for the player model to remap
			texels = (byte *) Hunk_AllocName(size, loadname);
			pheader->texels[i] = texels - (byte *)pheader;
			memcpy (texels, (byte *)(pskintype + 1), size);

			//spike - external model textures with dp naming -- eg progs/foo.mdl_0.tga
			//always use the alpha channel for external images. gpus prefer aligned data anyway.
			int mark = Hunk_LowMark ();
			char filename[MAX_QPATH];
			char filename2[MAX_QPATH];
			byte *data;
			int fwidth=0, fheight=0;
			qboolean malloced=false;
			enum srcformat fmt = SRC_RGBA;
			q_snprintf (filename, sizeof(filename), "%s_%i", loadmodel->name, i);
			if (gl_load24bit.value == 2) // woods #load24bit2
				data = /*!gl_load24bit.value ? NULL : */Image_LoadImage(filename, &fwidth, &fheight, &fmt, &malloced);
			else
				data = !gl_load24bit.value ? NULL : Image_LoadImage(filename, &fwidth, &fheight, &fmt, &malloced);
			//now load whatever we found
			if (data) //load external image
			{
				pheader->textures[i][0].base = TexMgr_LoadImage (loadmodel, filename, fwidth, fheight,
					fmt, data, filename, 0, TEXPREF_ALPHA|texflags|TEXPREF_MIPMAP );

				if (malloced)
					free(data);
				Hunk_FreeToLowMark (mark);

				q_snprintf (filename2, sizeof(filename2), "%s_pants", filename);
				pheader->textures[i][0].lower = TexMgr_LoadImage(loadmodel, filename2, fwidth, fheight, SRC_EXTERNAL, NULL, filename2, 0, TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);

				q_snprintf (filename2, sizeof(filename2), "%s_shirt", filename);
				pheader->textures[i][0].upper = TexMgr_LoadImage(loadmodel, filename2, fwidth, fheight, SRC_EXTERNAL, NULL, filename2, 0, TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);

				//now try to load glow/luma image from the same place
				q_snprintf (filename2, sizeof(filename2), "%s_glow", filename);
				data = (!gl_load24bit.value || gl_load24bit.value == 2) ?NULL:Image_LoadImage (filename2, &fwidth, &fheight, &fmt, &malloced); // woods #load24bit2
				if (!data)
				{
					q_snprintf (filename2, sizeof(filename2), "%s_luma", filename);
					data = (!gl_load24bit.value || gl_load24bit.value == 2) ?NULL:Image_LoadImage (filename2, &fwidth, &fheight, &fmt, &malloced); // woods #load24bit2
				}

				if (data)
					pheader->textures[i][0].luma = TexMgr_LoadImage (loadmodel, filename2, fwidth, fheight,
						fmt, data, filename, 0, TEXPREF_ALPHA|texflags|TEXPREF_MIPMAP );
				else
					pheader->textures[i][0].luma = NULL;

				if (malloced)
					free(data);
				Hunk_FreeToLowMark (mark);
			}
			else
			{
				//johnfitz -- rewritten
				q_snprintf (name, sizeof(name), "%s:frame%i", loadmodel->name, i);
				offset = (src_offset_t)(pskintype+1) - (src_offset_t)mod_base;
				if (Mod_CheckFullbrights ((byte *)(pskintype+1), size))
				{
					pheader->textures[i][0].base = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, texflags | TEXPREF_NOBRIGHT);
					q_snprintf (fbr_mask_name, sizeof(fbr_mask_name), "%s:frame%i_glow", loadmodel->name, i);
					pheader->textures[i][0].luma = TexMgr_LoadImage (loadmodel, fbr_mask_name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, texflags | TEXPREF_FULLBRIGHT);
				}
				else
				{
					pheader->textures[i][0].base = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, texflags);
					pheader->textures[i][0].luma = NULL;
				}

				pheader->textures[i][0].upper = NULL;
				pheader->textures[i][0].lower = NULL;
			}

			pheader->textures[i][3] = pheader->textures[i][2] = pheader->textures[i][1] = pheader->textures[i][0];
			//johnfitz

			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + size);
		}
		else
		{
			// animating skin group.  yuck.
			pskintype++;
			pinskingroup = (daliasskingroup_t *)pskintype;
			groupskins = LittleLong (pinskingroup->numskins);
			pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

			pskintype = (daliasskintype_t *)(pinskinintervals + groupskins);

			for (j=0 ; j<groupskins ; j++)
			{
				if (j == 0) {
					texels = (byte *) Hunk_AllocName(size, loadname);
					pheader->texels[i] = texels - (byte *)pheader;
					memcpy (texels, (byte *)(pskintype), size);
				}

				//johnfitz -- rewritten
				q_snprintf (name, sizeof(name), "%s:frame%i_%i", loadmodel->name, i,j);
				offset = (src_offset_t)(pskintype) - (src_offset_t)mod_base; //johnfitz
				if (Mod_CheckFullbrights ((byte *)(pskintype), size))
				{
					pheader->textures[i][j&3].base = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype), loadmodel->name, offset, texflags | TEXPREF_NOBRIGHT);
					q_snprintf (fbr_mask_name, sizeof(fbr_mask_name), "%s:frame%i_%i_glow", loadmodel->name, i,j);
					pheader->textures[i][j&3].luma = TexMgr_LoadImage (loadmodel, fbr_mask_name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype), loadmodel->name, offset, texflags | TEXPREF_FULLBRIGHT);
				}
				else
				{
					pheader->textures[i][j&3].base = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype), loadmodel->name, offset, texflags);
					pheader->textures[i][j&3].luma = NULL;
				}
				//johnfitz
				pheader->textures[i][j&3].upper = NULL;
				pheader->textures[i][j&3].lower = NULL;

				pskintype = (daliasskintype_t *)((byte *)(pskintype) + size);
			}
			k = j;
			for (/**/; j < 4; j++)
				pheader->textures[i][j&3] = pheader->textures[i][j - k];
		}
	}

	return (void *)pskintype;
}

//=========================================================================

/*
=================
Mod_CalcAliasBounds -- johnfitz -- calculate bounds of alias model for nonrotated, yawrotated, and fullrotated cases
=================
*/
void Mod_CalcAliasBounds (aliashdr_t *a)
{
	int			i,j,k;
	float		dist, yawradius, radius;
	vec3_t		v;

	//clear out all data
	for (i=0; i<3;i++)
	{
		loadmodel->mins[i] = loadmodel->ymins[i] = loadmodel->rmins[i] = FLT_MAX;
		loadmodel->maxs[i] = loadmodel->ymaxs[i] = loadmodel->rmaxs[i] = -FLT_MAX;
	}
	radius = yawradius = 0;

	for (;;)
	{
		if (a->nummorphposes && a->numverts)
		{
			switch(a->poseverttype)
			{
			case PV_QUAKE1:
				//process verts
				for (i=0 ; i<a->nummorphposes; i++)
					for (j=0; j<a->numverts; j++)
					{
						for (k=0; k<3;k++)
							v[k] = poseverts_mdl[i][j].v[k] * pheader->scale[k] + pheader->scale_origin[k];

						for (k=0; k<3;k++)
						{
							loadmodel->mins[k] = q_min(loadmodel->mins[k], v[k]);
							loadmodel->maxs[k] = q_max(loadmodel->maxs[k], v[k]);
						}
						dist = v[0] * v[0] + v[1] * v[1];
						if (yawradius < dist)
							yawradius = dist;
						dist += v[2] * v[2];
						if (radius < dist)
							radius = dist;
					}
				break;
			case PV_QUAKEFORGE:
				//process verts
				for (i=0 ; i<a->nummorphposes; i++)
					for (j=0; j<a->numverts; j++)
					{
						for (k=0; k<3;k++)
							v[k] = (poseverts_mdl[i][j].v[k] * pheader->scale[k]) + (poseverts_mdl[i][j+a->numverts].v[k] * pheader->scale[k]/256.f) + (pheader->scale_origin[k]);

						for (k=0; k<3;k++)
						{
							loadmodel->mins[k] = q_min(loadmodel->mins[k], v[k]);
							loadmodel->maxs[k] = q_max(loadmodel->maxs[k], v[k]);
						}
						dist = v[0] * v[0] + v[1] * v[1];
						if (yawradius < dist)
							yawradius = dist;
						dist += v[2] * v[2];
						if (radius < dist)
							radius = dist;
					}
				break;
			case PV_QUAKE3:
				//process verts
				for (i=0 ; i<a->nummorphposes; i++)
				{
					md3XyzNormal_t *pv = (md3XyzNormal_t *)((byte*)a+a->vertexes) + i*a->numverts;
					for (j=0; j<a->numverts; j++)
					{
						for (k=0; k<3;k++)
							v[k] = pv[j].xyz[k] * 1/64.0;

						for (k=0; k<3;k++)
						{
							loadmodel->mins[k] = q_min(loadmodel->mins[k], v[k]);
							loadmodel->maxs[k] = q_max(loadmodel->maxs[k], v[k]);
						}
						dist = v[0] * v[0] + v[1] * v[1];
						if (yawradius < dist)
							yawradius = dist;
						dist += v[2] * v[2];
						if (radius < dist)
							radius = dist;
					}
				}
				break;
			case PV_IQM:
				//process verts
				for (i=0 ; i<a->nummorphposes; i++)
				{
					const iqmvert_t *pv = (const iqmvert_t *)((byte*)a+a->vertexes) + i*a->numverts;
					for (j=0; j<a->numverts; j++)
					{
						for (k=0; k<3;k++)
							v[k] = pv[j].xyz[k];

						for (k=0; k<3;k++)
						{
							loadmodel->mins[k] = q_min(loadmodel->mins[k], v[k]);
							loadmodel->maxs[k] = q_max(loadmodel->maxs[k], v[k]);
						}
						dist = v[0] * v[0] + v[1] * v[1];
						if (yawradius < dist)
							yawradius = dist;
						dist += v[2] * v[2];
						if (radius < dist)
							radius = dist;
					}
				}
				break;
			}
		}

		if (!a->nextsurface)
			break;
		a = (aliashdr_t*)((byte*)a + a->nextsurface);
	}

	//dodgy model that lacks any frames or verts
	for (i=0; i<3;i++)
	{
		if (loadmodel->mins[i] > loadmodel->maxs[i])
		{	//set sizes to 0 if its invalid.
			loadmodel->mins[i] = 0;
			loadmodel->maxs[i] = 0;
		}
	}

	//rbounds will be used when entity has nonzero pitch or roll
	radius = sqrt(radius);
	loadmodel->rmins[0] = loadmodel->rmins[1] = loadmodel->rmins[2] = -radius;
	loadmodel->rmaxs[0] = loadmodel->rmaxs[1] = loadmodel->rmaxs[2] = radius;

	//ybounds will be used when entity has nonzero yaw
	yawradius = sqrt(yawradius);
	loadmodel->ymins[0] = loadmodel->ymins[1] = -yawradius;
	loadmodel->ymaxs[0] = loadmodel->ymaxs[1] = yawradius;
	loadmodel->ymins[2] = loadmodel->mins[2];
	loadmodel->ymaxs[2] = loadmodel->maxs[2];
}

qboolean // woods #obmodelslist remove static
nameInList(const char *list, const char *name)
{
	const char *s;
	char tmp[MAX_QPATH];
	int i;

	s = list;

	while (*s)
	{
		// make a copy until the next comma or end of string
		i = 0;
		while (*s && *s != ',')
		{
			if (i < MAX_QPATH - 1)
				tmp[i++] = *s;
			s++;
		}
		tmp[i] = '\0';
		//compare it to the model name
		if (!strcmp(name, tmp))
		{
			return true;
		}
		//search forwards to the next comma or end of string
		while (*s && *s == ',')
			s++;
	}
	return false;
}

/*
=================
Mod_SetExtraFlags -- johnfitz -- set up extra flags that aren't in the mdl
=================
*/
void Mod_SetExtraFlags (qmodel_t *mod)
{
	extern cvar_t r_nolerp_list, r_noshadow_list;
	extern cvar_t r_model_light_desat_list, r_nooutline_list; // woods #desat #routline

	if (!mod)
		return;

	mod->flags &= (0xFF | MF_HOLEY | MOD_HDRLIGHTING); //only preserve first byte, plus MF_HOLEY

	if (mod->type == mod_alias)
	{
		// nolerp flag
		if (nameInList(r_nolerp_list.string, mod->name))
			mod->flags |= MOD_NOLERP;

		// noshadow flag
		if (nameInList(r_noshadow_list.string, mod->name))
			mod->flags |= MOD_NOSHADOW;

		// list memberships the render loop would otherwise re-parse per frame
		if (nameInList(r_model_light_desat_list.string, mod->name)) // woods #desat
			mod->flags |= MOD_DESATLISTED;
		if (nameInList(r_nooutline_list.string, mod->name)) // woods #routline
			mod->flags |= MOD_NOOUTLINE;

		// fullbright hack (TODO: make this a cvar list)
		if (!strcmp (mod->name, "progs/flame2.mdl") ||
			!strcmp (mod->name, "progs/flame.mdl") ||
			!strcmp (mod->name, "progs/boss.mdl"))
		{
			mod->flags |= MOD_FBRIGHTHACK;
		}
	}

	if (mod->type == mod_brush) // woods #shadow
	{
		if (nameInList(r_noshadow_list.string, mod->name))
			mod->flags |= MOD_NOSHADOW;
	}

#ifdef PSET_SCRIPT
	PScript_UpdateModelEffects(mod);
#endif
}

/*
=================
Mod_LoadAliasModel
=================
*/
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer, int pvtype)
{
	int					i, j;
	mdl_t				*pinmodel;
	stvert_t			*pinstverts;
	dtriangle_t			*pintriangles;
	int					version, numframes;
	int					size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int					start, end, total;

	start = Hunk_LowMark ();

	pinmodel = (mdl_t *)buffer;
	mod_base = (byte *)buffer; //johnfitz

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

	numframes = LittleLong (pinmodel->numframes);
	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d", numframes);
	if (numframes > MAXALIASFRAMES)
	{
		Con_Warning("model %s has too many frames (%i > %i)\n", mod->name, numframes, MAXALIASFRAMES);
		numframes = MAXALIASFRAMES;
	}

//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	if ((size_t)(numframes - 1) > ((size_t)INT_MAX - sizeof(aliashdr_t)) / sizeof(pheader->frames[0]))
		Sys_Error ("Mod_LoadAliasModel: header too large");
	size	= sizeof(aliashdr_t) +
		 (numframes - 1) * sizeof (pheader->frames[0]);
	pheader = (aliashdr_t *) Hunk_AllocName (size, loadname);

	mod->flags = LittleLong (pinmodel->flags);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Con_DWarning ("model %s has a skin taller than %d\n", mod->name,
				   MAX_LBM_HEIGHT);	//Spike -- this was always a bogus error in gl renderers. its width*height that really matters.

	pheader->numverts = LittleLong (pinmodel->numverts);

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);
	if (pheader->numverts > MAXALIASVERTS)
	{	//Spike -- made this more tollerant. its still an error of course.
		Con_Warning("model %s has too many vertices (%i > %i)\n", mod->name, pheader->numverts, MAXALIASVERTS);
		mod->type = mod_ext_invalid;
		Hunk_FreeToLowMark (start);
		return;
	}
	if (pheader->numverts > VANILLA_MAXALIASVERTS)
		Con_DWarning("model %s exceeds standard vertex limit (%i > %i)\n", mod->name, pheader->numverts, VANILLA_MAXALIASVERTS);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);
	if ((size_t)pheader->numtris > (size_t)INT_MAX / 3)
	{
		Con_Warning("model %s has too many triangles (%i)\n", mod->name, pheader->numtris);
		mod->type = mod_ext_invalid;
		Hunk_FreeToLowMark (start);
		return;
	}
	if (pheader->numtris > max_triangles)
	{
		mtriangle_t *n = malloc(sizeof(*triangles) * pheader->numtris);
		if (n)
		{
			free(triangles);
			triangles = n;
			max_triangles = pheader->numtris;
		}
		else
		{
			max_triangles = 0;
			//Spike -- added this check, because I'm segfaulting out.
			Con_Warning("model %s has too many triangles (%i)\n", mod->name, pheader->numtris);
			mod->type = mod_ext_invalid;
			Hunk_FreeToLowMark (start);
			return;
		}
	}

	pheader->numframes = numframes;

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}

//
// load the skins
//
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = (daliasskintype_t *) Mod_LoadAllSkins (pheader->numskins, pskintype);

//
// load base s and t vertices
//
	pinstverts = (stvert_t *)pskintype;

	for (i=0 ; i<pheader->numverts ; i++)
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);	//should only be 0 or ALIAS_ONSEAM. other values (particuarly 1) is a model bug and will be treated as ALIAS_ONSEAM in this implementation.
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

//
// load triangle lists
//
	pintriangles = (dtriangle_t *)&pinstverts[pheader->numverts];

	for (i=0 ; i<pheader->numtris ; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j=0 ; j<3 ; j++)
		{
			triangles[i].vertindex[j] =
					LittleLong (pintriangles[i].vertindex[j]);
		}
	}

//
// load the frames
//
	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t	frametype;
		frametype = (aliasframetype_t) LittleLong (pframetype->type);
		if (frametype == ALIAS_SINGLE)
			pframetype = (daliasframetype_t *) Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i], pvtype);
		else
			pframetype = (daliasframetype_t *) Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i], pvtype);
	}

	pheader->nummorphposes = posenum;
	pheader->poseverttype = pvtype;	//it would be safe to always store PV_QUAKE1 here if you wanted to drop the low-order data.

	mod->type = mod_alias;

	Mod_CalcAliasBounds (pheader); //johnfitz

	//Spike: for setmodel compat with vanilla
	mod->clipmins[0] = mod->clipmins[1] = mod->clipmins[2] = -16;
	mod->clipmaxs[0] = mod->clipmaxs[1] = mod->clipmaxs[2] = 16;

	//
	// build the draw lists
	//
	GL_MakeAliasModelDisplayLists (mod, pheader);

//
// move the complete, relocatable alias model to the cache
//
	end = Hunk_LowMark ();
	total = end - start;

	// hunk segments may not be contiguous in memory; alias models rely on
	// pheader + offset addressing, so refuse the load before anything walks
	// those offsets.
	if (!Hunk_IsContiguous (start, end))
		Sys_Error ("Mod_LoadAliasModel: %s spans multiple hunk segments (try a larger -heapsize)", mod->name);

	GLMesh_LoadVertexBuffer (mod, pheader);

	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
	{
		Hunk_FreeToLowMark (start);
		return;
	}
	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
static void *Mod_LoadSpriteFrame (void * pin, mspriteframe_t **ppframe, int framenum, enum srcformat fmt)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[64];
	src_offset_t			offset; //johnfitz
	byte* data = NULL;
	qboolean malloced = false;
	int fwidth = 0, fheight = 0;
	enum srcformat rfmt = SRC_RGBA;
	int hunkmark;

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;
	if (fmt == SRC_RGBA)
		size *= 4;

	pspriteframe = (mspriteframe_t *) Hunk_AllocName (sizeof (mspriteframe_t),loadname);
	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	//johnfitz -- image might be padded
	pspriteframe->smax = (float)width/(float)TexMgr_PadConditional(width);
	pspriteframe->tmax = (float)height/(float)TexMgr_PadConditional(height);
	//johnfitz

	if (gl_load24bit.value) // woods #extsprites
	{
		hunkmark = Hunk_LowMark();
		q_snprintf(name, sizeof(name), "%s_%i", loadmodel->name, framenum);
		data = Image_LoadImage(name, &fwidth, &fheight, &rfmt, &malloced);

		if (data)
		{
			pspriteframe->gltexture =
				TexMgr_LoadImage(loadmodel, name, fwidth, fheight, rfmt,
					data, name, 0, TEXPREF_PAD | TEXPREF_NOPICMIP | TEXPREF_LINEAR | TEXPREF_ALPHA);

			if (malloced)
				free(data);
			Hunk_FreeToLowMark(hunkmark);

			return (void*)((byte*)pinframe + sizeof(dspriteframe_t) + size);
		}
	}

	q_snprintf (name, sizeof(name), "%s:frame%i", loadmodel->name, framenum);
	offset = (src_offset_t)(pinframe+1) - (src_offset_t)mod_base; //johnfitz
	pspriteframe->gltexture =
		TexMgr_LoadImage (loadmodel, name, width, height, fmt,
				  (byte *)(pinframe + 1), loadmodel->name, offset,
				  TEXPREF_PAD | TEXPREF_ALPHA | TEXPREF_NOPICMIP); //johnfitz -- TexMgr

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
static void *Mod_LoadSpriteGroup (void * pin, mspriteframe_t **ppframe, int framenum, enum srcformat fmt, spriteframetype_t type)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;
	float				prevtime;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteGroup: Invalid # of frames: %d", numframes);
	if (type == SPR_ANGLED && numframes != 8)
		Sys_Error ("Mod_LoadSpriteGroup: Bad # of frames: %d", numframes);
	if ((size_t)(numframes - 1) > ((size_t)INT_MAX - sizeof(mspritegroup_t)) / sizeof(pspritegroup->frames[0]))
		Sys_Error ("Mod_LoadSpriteGroup: group too large");

	pspritegroup = (mspritegroup_t *) Hunk_AllocName (sizeof (mspritegroup_t) +
				(numframes - 1) * sizeof (pspritegroup->frames[0]), loadname);

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	if ((size_t)numframes > (size_t)INT_MAX / sizeof (float))
		Sys_Error ("Mod_LoadSpriteGroup: intervals too large");
	poutintervals = (float *) Hunk_AllocName (numframes * sizeof (float), loadname);

	pspritegroup->intervals = poutintervals;

	for (i=0,prevtime=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");
		//Spike -- we need to accumulate the previous time too, so we get actual timestamps, otherwise spritegroups won't animate (vanilla bug).
		prevtime = *poutintervals = prevtime+*poutintervals;

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i], framenum * 100 + i, fmt);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;
	enum srcformat fmt = SRC_INDEXED;

	pin = (dsprite_t *)buffer;
	mod_base = (byte *)buffer; //johnfitz

	version = LittleLong (pin->version);
	if (version == 32)
		fmt = SRC_RGBA;	//Spike -- spr32 is identical to regular sprites, but uses rgba instead of indexed values. should probably also blend these sprites instead of alphatest, but meh.
	else if (version != SPRITE_VERSION)
	{
		//Spike -- made this more tolerant. its still an error, it just won't crash us out
		Con_Printf(	"%s has wrong version number "
					"(%i should be %i)\n", mod->name, version, SPRITE_VERSION);
					mod->type = mod_ext_invalid;
		return;
	}

	numframes = LittleLong (pin->numframes);
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d", numframes);
	if ((size_t)(numframes - 1) > ((size_t)INT_MAX - sizeof(msprite_t)) / sizeof(psprite->frames[0]))
		Sys_Error ("Mod_LoadSpriteModel: sprite too large");

	size = sizeof (msprite_t) + (numframes - 1) * sizeof (psprite->frames);

	psprite = (msprite_t *) Hunk_AllocName (size, loadname);

	mod->cache.data = psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = (synctype_t) LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;
	VectorCopy(mod->mins, mod->clipmins);
	VectorCopy(mod->maxs, mod->clipmaxs);

//
	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t	frametype;

		frametype = (spriteframetype_t) LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteFrame (pframetype + 1, &psprite->frames[i].frameptr, i, fmt);
		}
		else
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteGroup (pframetype + 1, &psprite->frames[i].frameptr, i, fmt, frametype);
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
static void Mod_Print (void)
{
	int		i;
	qmodel_t	*mod;

	Con_SafePrintf ("Cached models:\n"); //johnfitz -- safeprint instead of print
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		Con_SafePrintf ("%8p : %s\n", mod->cache.data, mod->name); //johnfitz -- safeprint instead of print
	}
	Con_Printf ("%i models\n",mod_numknown); //johnfitz -- print the total too
}
