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

// draw.c -- 2d drawing

#include "quakedef.h"

//extern unsigned char d_15to8table[65536]; //johnfitz -- never used

qboolean	draw_load24bit;
qboolean	premul_hud = false;//true;
cvar_t		scr_conalpha = {"scr_conalpha", "0.5", CVAR_ARCHIVE}; //johnfitz

qpic_t		*draw_disc;
qpic_t		*draw_backtile;
qboolean	custom_conchars; // woods (iw) #democontrols

gltexture_t *char_texture; //johnfitz
qpic_t		*pic_ovr, *pic_ins; //johnfitz -- new cursor handling
qpic_t		*pic_nul; //johnfitz -- for missing gfx, don't crash

extern cvar_t gl_load24bit_hud; // woods #24bithud
extern cvar_t scr_conback; // woods #conback

extern gltexture_t* char_texture; // woods #goldtext
static const plcolour_t scr_positive_diff_default_color = { .type = 2, .rgb = { 0xF6, 0xC2, 0x2C }, .basic = 0 }; // woods #goldtext

//johnfitz -- new pics
byte pic_ovr_data[8][8] =
{
	{255,255,255,255,255,255,255,255},
	{255, 15, 15, 15, 15, 15, 15,255},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255, 15, 15, 15, 15, 15, 15,  2},
	{255,255,  2,  2,  2,  2,  2,  2},
};

byte pic_ins_data[9][8] =
{
	{ 15, 15,255,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{ 15, 15,  2,255,255,255,255,255},
	{255,  2,  2,255,255,255,255,255},
};

byte pic_nul_data[8][8] =
{
	{252,252,252,252,  0,  0,  0,  0},
	{252,252,252,252,  0,  0,  0,  0},
	{252,252,252,252,  0,  0,  0,  0},
	{252,252,252,252,  0,  0,  0,  0},
	{  0,  0,  0,  0,252,252,252,252},
	{  0,  0,  0,  0,252,252,252,252},
	{  0,  0,  0,  0,252,252,252,252},
	{  0,  0,  0,  0,252,252,252,252},
};

byte pic_stipple_data[8][8] =
{
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
};

byte pic_crosshair_data[8][8] =
{
	{255,255,255,255,255,255,255,255},
	{255,255,255,  8,  9,255,255,255},
	{255,255,255,  6,  8,  2,255,255},
	{255,  6,  8,  8,  6,  8,  8,255},
	{255,255,  2,  8,  8,  2,  2,  2},
	{255,255,255,  7,  8,  2,255,255},
	{255,255,255,255,  2,  2,255,255},
	{255,255,255,255,255,255,255,255},
};
//johnfitz

typedef struct
{
	gltexture_t *gltexture;
	float		sl, tl, sh, th;
} glpic_t;

canvastype currentcanvas = CANVAS_NONE; //johnfitz -- for GL_SetCanvas

//==============================================================================
//
//  PIC CACHING
//
//==============================================================================

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic
} cachepic_t;

#define	MAX_CACHED_PICS		512	//Spike -- increased to avoid csqc issues.
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int			menu_numcachepics;

byte		menuplyr_pixels[4096];

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

#define	MAX_SCRAPS		2
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256
#define	SCRAP_PADDING	1 // woods iw

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT]; //johnfitz -- removed *4 after BLOCK_HEIGHT
qboolean	scrap_dirty;
gltexture_t	*scrap_textures[MAX_SCRAPS]; //johnfitz


/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i=0 ; i<BLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (scrap_allocated[texnum][i+j] >= best)
					break;
				if (scrap_allocated[texnum][i+j] > best2)
					best2 = scrap_allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error ("Scrap_AllocBlock: full"); //johnfitz -- correct function name
	return 0; //johnfitz -- shut up compiler
}

/*
================
Scrap_Upload -- johnfitz -- now uses TexMgr
================
*/
void Scrap_Upload (void)
{
	char name[8];
	int	i;

	for (i=0; i<MAX_SCRAPS; i++)
	{
		sprintf (name, "scrap%i", i);
		scrap_textures[i] = TexMgr_LoadImage (NULL, name, BLOCK_WIDTH, BLOCK_HEIGHT, SRC_INDEXED, scrap_texels[i],
			"", (src_offset_t)scrap_texels[i], (premul_hud?TEXPREF_PREMULTIPLY:0)|TEXPREF_ALPHA | TEXPREF_OVERWRITE | TEXPREF_NOPICMIP);
	}

	scrap_dirty = false;
}

/*
================
Draw_PicFromWad
================
*/
qpic_t *Draw_PicFromWad2 (const char *name, unsigned int texflags)
{
	int i;
	cachepic_t *pic;
	qpic_t	*p;
	glpic_t	gl;
	src_offset_t offset; //johnfitz
	lumpinfo_t *info;

	texflags |= (premul_hud?TEXPREF_PREMULTIPLY:0);

	//Spike -- added cachepic stuff here, to avoid glitches if the function is called multiple times with the same image.
	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
	{
		if (!strcmp (name, pic->name))
			return &pic->pic;
	}
	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");

	p = (qpic_t *) W_GetLumpName (name, &info);
	if (!p)
	{
		Con_SafePrintf ("W_GetLumpName: %s not found\n", name);
		return pic_nul; //johnfitz
	}
	if (info->type != TYP_QPIC) {Con_SafePrintf ("Draw_PicFromWad: lump \"%s\" is not a qpic\n", name); return pic_nul;}
	if (info->size < sizeof(int)*2) {Con_SafePrintf ("Draw_PicFromWad: pic \"%s\" is too small for its qpic header (%u bytes)\n", name, info->size); return pic_nul;}
	if (info->size < sizeof(int)*2+p->width*p->height) {Con_SafePrintf ("Draw_PicFromWad: pic \"%s\" truncated (%u*%u requires %u at least bytes)\n", name, p->width,p->height, 8+p->width*p->height); return pic_nul;}
	if (info->size > sizeof(int)*2+p->width*p->height) Con_DPrintf ("Draw_PicFromWad: pic \"%s\" over-sized (%u*%u requires only %u bytes)\n", name, p->width,p->height, 8+p->width*p->height);

	//Spike -- if we're loading external images, and one exists, then use that instead.
	if (draw_load24bit && (gl.gltexture=TexMgr_LoadImage (NULL, name, 0, 0, SRC_EXTERNAL, NULL, va("gfx/%s", name), 0, texflags|TEXPREF_MIPMAP|TEXPREF_ALLOWMISSING)))
	{
		gl.sl = 0;
		gl.sh = (texflags&TEXPREF_PAD)?(float)gl.gltexture->source_width/(float)TexMgr_PadConditional(gl.gltexture->source_width):1;
		gl.tl = 0;
		gl.th = (texflags&TEXPREF_PAD)?(float)gl.gltexture->source_height/(float)TexMgr_PadConditional(gl.gltexture->source_height):1;
	}
	// load little ones into the scrap
	else if (p->width < 64 && p->height < 64 && texflags==((premul_hud?TEXPREF_PREMULTIPLY:0)|TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP))
	{
		int		x, y;
		int		i, j, k;
		int		texnum;

		texnum = Scrap_AllocBlock (p->width + SCRAP_PADDING, p->height + SCRAP_PADDING, &x, &y); // woods iw
		scrap_dirty = true;
		k = 0;
		for (i=0 ; i<(int)p->height ; i++)
		{
			for (j=0 ; j<(int)p->width ; j++, k++)
				scrap_texels[texnum][(y+i)*BLOCK_WIDTH + x + j] = p->data[k];
		}
		gl.gltexture = scrap_textures[texnum]; //johnfitz -- changed to an array
		//johnfitz -- no longer go from 0.01 to 0.99
		gl.sl = x/(float)BLOCK_WIDTH;
		gl.sh = (x+p->width)/(float)BLOCK_WIDTH;
		gl.tl = y/(float)BLOCK_WIDTH;
		gl.th = (y+p->height)/(float)BLOCK_WIDTH;
	}
	else
	{
		char texturename[64]; //johnfitz
		q_snprintf (texturename, sizeof(texturename), "%s:%s", WADFILENAME, name); //johnfitz

		offset = (src_offset_t)p - (src_offset_t)wad_base + sizeof(int)*2; //johnfitz

		gl.gltexture = TexMgr_LoadImage (NULL, texturename, p->width, p->height, SRC_INDEXED, p->data, WADFILENAME,
										  offset, texflags); //johnfitz -- TexMgr
		gl.sl = 0;
		gl.sh = (texflags&TEXPREF_PAD)?(float)p->width/(float)TexMgr_PadConditional(p->width):1; //johnfitz
		gl.tl = 0;
		gl.th = (texflags&TEXPREF_PAD)?(float)p->height/(float)TexMgr_PadConditional(p->height):1; //johnfitz
	}

	menu_numcachepics++;
	strcpy (pic->name, name);
	pic->pic = *p;
	memcpy (pic->pic.data, &gl, sizeof(glpic_t));

	return &pic->pic;
}

qpic_t *Draw_PicFromWad (const char *name)
{
	return Draw_PicFromWad2(name, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP);
}

qpic_t	*Draw_GetCachedPic (const char *path)
{
	cachepic_t	*pic;
	int			i;

	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	return NULL;
}

/*
================
Draw_CachePic
================
*/
qpic_t	*Draw_TryCachePic (const char *path, unsigned int texflags)
{
	cachepic_t	*pic;
	int			i;
	qpic_t		*dat;
	glpic_t		gl;
	char newname[MAX_QPATH];

	texflags |= (premul_hud?TEXPREF_PREMULTIPLY:0);

	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");
	menu_numcachepics++;
	strcpy (pic->name, path);

	if (strcmp("lmp", COM_FileGetExtension(path)))
	{
		char npath[MAX_QPATH];
		COM_StripExtension(path, npath, sizeof(npath));
		gl.gltexture = TexMgr_LoadImage (NULL, npath, 0, 0, SRC_EXTERNAL, NULL, npath, 0, texflags);

		pic->pic.width = gl.gltexture->width;
		pic->pic.height = gl.gltexture->height;

		gl.sl = 0;
		gl.sh = (texflags&TEXPREF_PAD)?(float)pic->pic.width/(float)TexMgr_PadConditional(pic->pic.width):1; //johnfitz
		gl.tl = 0;
		gl.th = (texflags&TEXPREF_PAD)?(float)pic->pic.height/(float)TexMgr_PadConditional(pic->pic.height):1; //johnfitz
		memcpy (pic->pic.data, &gl, sizeof(glpic_t));

		return &pic->pic;
	}

//
// load the pic from disk
//
	dat = (qpic_t *)COM_LoadTempFile (path, NULL);
	if (!dat)
		return NULL;
	SwapPic (dat);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width*dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	//Spike -- if we're loading external images, and one exists, then use that instead (but with the sizes of the lmp).
	COM_StripExtension(path, newname, sizeof(newname));
	if (draw_load24bit && (gl.gltexture=TexMgr_LoadImage (NULL, path, 0, 0, SRC_EXTERNAL, NULL, newname, 0, texflags|TEXPREF_MIPMAP|TEXPREF_ALLOWMISSING|TEXPREF_CLAMP))) // woods iw add clamp
	{
		gl.sl = 0;
		gl.sh = (texflags&TEXPREF_PAD)?(float)gl.gltexture->source_width/(float)TexMgr_PadConditional(gl.gltexture->source_width):1;
		gl.tl = 0;
		gl.th = (texflags&TEXPREF_PAD)?(float)gl.gltexture->source_height/(float)TexMgr_PadConditional(gl.gltexture->source_height):1;
	}
	else
	{
		gl.gltexture = TexMgr_LoadImage (NULL, path, dat->width, dat->height, SRC_INDEXED, dat->data, path,
										  sizeof(int)*2, texflags | TEXPREF_NOPICMIP | TEXPREF_CLAMP); //johnfitz -- TexMgr -- woods iw add clamp
		gl.sl = 0;
		gl.sh = (texflags&TEXPREF_PAD)?(float)dat->width/(float)TexMgr_PadConditional(dat->width):1; //johnfitz
		gl.tl = 0;
		gl.th = (texflags&TEXPREF_PAD)?(float)dat->height/(float)TexMgr_PadConditional(dat->height):1; //johnfitz
	}
	memcpy (pic->pic.data, &gl, sizeof(glpic_t));

	return &pic->pic;
}

qpic_t	*Draw_CachePic (const char *path)
{
	qpic_t *pic = Draw_TryCachePic(path, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP);
	if (!pic)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	return pic;
}

/*
================
Draw_MakePic -- johnfitz -- generate pics from internal data
================
*/
qpic_t *Draw_MakePic (const char *name, int width, int height, byte *data)
{
	int flags = TEXPREF_NEAREST | TEXPREF_ALPHA | TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_PAD;
	qpic_t		*pic;
	glpic_t		gl;

	pic = (qpic_t *) Hunk_Alloc (sizeof(qpic_t) - 4 + sizeof (glpic_t));
	pic->width = width;
	pic->height = height;

	gl.gltexture = TexMgr_LoadImage (NULL, name, width, height, SRC_INDEXED, data, "", (src_offset_t)data, flags);
	gl.sl = 0;
	gl.sh = (float)width/(float)TexMgr_PadConditional(width);
	gl.tl = 0;
	gl.th = (float)height/(float)TexMgr_PadConditional(height);
	memcpy (pic->data, &gl, sizeof(glpic_t));

	return pic;
}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
===============
Draw_LoadPics -- johnfitz
===============
*/
void Draw_LoadPics (void)
{
	byte		*data;
	src_offset_t	offset;
	lumpinfo_t *info;
	extern cvar_t gl_load24bit;

	const unsigned int conchar_texflags = (premul_hud?TEXPREF_PREMULTIPLY:0)|TEXPREF_ALPHA | TEXPREF_NOPICMIP | TEXPREF_CONCHARS;	//Spike - we use nearest with 8bit, but not replacements. replacements also use mipmaps because they're just noise otherwise.

	if (gl_load24bit.value > 0 && gl_load24bit_hud.value) // woods #24bithud
		draw_load24bit = true;
	else
		draw_load24bit = false;

	char_texture = NULL;
	//logical path
	if (!char_texture)
		char_texture = draw_load24bit?TexMgr_LoadImage (NULL, WADFILENAME":conchars", 0, 0, SRC_EXTERNAL, NULL, "gfx/conchars", 0, conchar_texflags | TEXPREF_MIPMAP | TEXPREF_ALLOWMISSING):NULL; // woods enable TEXPREF_MIPMAP
	//stupid quakeworldism
	if (!char_texture)
		char_texture = draw_load24bit?TexMgr_LoadImage (NULL, WADFILENAME":conchars", 0, 0, SRC_EXTERNAL, NULL, "charsets/conchars", 0, conchar_texflags | TEXPREF_MIPMAP | TEXPREF_ALLOWMISSING):NULL; // woods enable TEXPREF_MIPMAP
	
	custom_conchars = (char_texture != NULL); // woods (iw) #democontrols
	
	//vanilla.
	if (!char_texture)
	{
		data = (byte *) W_GetLumpName ("conchars", &info);
		if (!data || info->size < 128*128)	Sys_Error ("Draw_LoadPics: couldn't load conchars");
		if (info->size != 128*128)			Con_Warning("Invalid size for gfx.wad conchars lump (%u, expected %u) - attempting to ignore for compat.\n", info->size, 128*128);
		else if (info->type != TYP_MIPTEX)	Con_DWarning("Invalid type for gfx.wad conchars lump - attempting to ignore for compat.\n"); //not really a miptex, but certainly NOT a qpic.
		offset = (src_offset_t)data - (src_offset_t)wad_base;
		char_texture = TexMgr_LoadImage (NULL, WADFILENAME":conchars", 128, 128, SRC_INDEXED, data,
			WADFILENAME, offset, conchar_texflags | TEXPREF_NEAREST);
	}

	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad2 ("backtile", TEXPREF_NOPICMIP);	//do NOT use PAD because that breaks wrapping. do NOT use alpha, because that could result in glitches.
}

/*
===============
Draw_NewGame -- johnfitz
===============
*/
void Draw_NewGame (void)
{
	cachepic_t	*pic;
	int			i;

	// empty scrap and reallocate gltextures
	memset(scrap_allocated, 0, sizeof(scrap_allocated));
	memset(scrap_texels, 255, sizeof(scrap_texels));

	Scrap_Upload (); //creates 2 empty gltextures

	// empty lmp cache
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
		pic->name[0] = 0;
	menu_numcachepics = 0;

	// reload wad pics
	W_LoadWadFile (); //johnfitz -- filename is now hard-coded for honesty
	Draw_LoadPics ();
	SCR_LoadPics ();
	Sbar_LoadPics ();
	PR_ReloadPics(false);
}

qboolean Draw_ReloadTextures(qboolean force)
{
	extern cvar_t gl_load24bit;
	if (draw_load24bit != !!gl_load24bit.value)
		force = true;

	if (draw_load24bit != !!gl_load24bit_hud.value) // woods #24bithud
		force = true;

	if (force)
	{
		TexMgr_NewGame ();
		Draw_NewGame ();


		Cache_Flush ();
		Mod_ResetAll();
		return true;
	}
	return false;
}

/*
===============
Draw_Init -- johnfitz -- rewritten
===============
*/
void Draw_Init (void)
{
	Cvar_RegisterVariable (&scr_conalpha);

	// clear scrap and allocate gltextures
	memset(scrap_allocated, 0, sizeof(scrap_allocated));
	memset(scrap_texels, 255, sizeof(scrap_texels));

	Scrap_Upload (); //creates 2 empty textures

	// create internal pics
	pic_ins = Draw_MakePic ("ins", 8, 9, &pic_ins_data[0][0]);
	pic_ovr = Draw_MakePic ("ovr", 8, 8, &pic_ovr_data[0][0]);
	pic_nul = Draw_MakePic ("nul", 8, 8, &pic_nul_data[0][0]);

	// load game pics
	Draw_LoadPics ();
}

//==============================================================================
//
//  2D DRAWING
//
//==============================================================================

/*
================
Draw_CharacterQuad -- johnfitz -- seperate function to spit out verts
================
*/
void Draw_CharacterQuad (int x, int y, char num)
{
	int				row, col;
	float			frow, fcol, size;

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + size, frow);
	glVertex2f (x+8, y);
	glTexCoord2f (fcol + size, frow + size);
	glVertex2f (x+8, y+8);
	glTexCoord2f (fcol, frow + size);
	glVertex2f (x, y+8);
}

/*
================
Draw_Character -- johnfitz -- modified to call Draw_CharacterQuad
================
*/
void Draw_Character (int x, int y, int num)
{
	/*if (y <= -8)
		return;			// totally off screen*/ // woods alllow for negative drawing for #observerhud

	num &= 255;

	if (num == 32)
		return; //don't waste verts on spaces

	GL_Bind (char_texture);
	glBegin (GL_QUADS);

	Draw_CharacterQuad (x, y, (char) num);

	glEnd ();
}

/*
================
Draw_CharacterRGBA -- woods -- https://github.com/nzp-team/quakespasm commit c7ba1d4 -- #iwtabcomplete
================
*/
void Draw_CharacterRGBA (int x, int y, int num, plcolour_t c, float alpha)
{
	int				row, col;
	float			frow, fcol, size;

	num &= 255;

	if (num == 32)
		return; //don't waste verts on spaces

	glEnable (GL_BLEND);

	if (c.type == 2)
		glColor4f(c.rgb[0] / 255.0, c.rgb[1] / 255.0, c.rgb[2] / 255.0, alpha);
	else
	{
		byte* pal = (byte*)&d_8to24table[(c.basic << 4) + 8];
		glColor4f(pal[0] / 255.0, pal[1] / 255.0, pal[2] / 255.0, alpha);
	}

	glDisable (GL_ALPHA_TEST);
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	GL_Bind (char_texture);
	glBegin (GL_QUADS);

	row = num >> 4;
	col = num & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + size, frow);
	glVertex2f (x + 8, y);
	glTexCoord2f (fcol + size, frow + size);
	glVertex2f (x + 8, y + 8);
	glTexCoord2f (fcol, frow + size);
	glVertex2f (x, y + 8);

	glEnd();

	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glEnable (GL_ALPHA_TEST);
	glDisable (GL_BLEND);
	glColor4f (1, 1, 1, 1);
}

void Draw_Character_Rotation (int x, int y, int num, int rotation) // woods #movementkeys
{
	num &= 255;

	if (num == 32)
		return; // don't waste verts on spaces

	GL_Bind(char_texture);
	glPushMatrix(); // Save the current transformation state

	glTranslatef(x + 4, y + 4, 0); // Move the center of rotation to the character's center

	glRotatef(rotation, 0, 0, 1); // Rotate clockwise by using a positive angle

	if (rotation == 360 || rotation == -360) 
		glScalef(-1, 1, 1); // Flip horizontally

	glTranslatef(-4, -4, 0); // Move back by the offset

	glBegin(GL_QUADS);

	int row, col;
	float frow, fcol, size;

	row = num >> 4;
	col = num & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	// Normal rendering
	glTexCoord2f(fcol, frow);
	glVertex2f(0, 0);
	glTexCoord2f(fcol + size, frow);
	glVertex2f(8, 0);
	glTexCoord2f(fcol + size, frow + size);
	glVertex2f(8, 8);
	glTexCoord2f(fcol, frow + size);
	glVertex2f(0, 8);

	glEnd();

	glPopMatrix(); // Restore the previous transformation state
}

/*
================
Draw_String -- johnfitz -- modified to call Draw_CharacterQuad
================
*/
void Draw_String (int x, int y, const char *str)
{
	//if (y <= -8) // woods enabled for more printing options #varmatchclock
	//	return;			// totally off screen

	GL_Bind (char_texture);
	glBegin (GL_QUADS);

	while (*str)
	{
		if (*str != 32) //don't waste verts on spaces
			Draw_CharacterQuad (x, y, *str);
		str++;
		x += 8;
	}

	glEnd ();
}

/*
================
Draw_StringRGBA -- woods
================
*/
void Draw_StringRGBA (int x, int y, const char* str, plcolour_t c, float alpha)
{
	//if (y <= -8) // woods enabled for more printing options #varmatchclock
	//	return;			// totally off screen

	glEnable(GL_BLEND);

	if (c.type == 2)
		glColor4f(c.rgb[0] / 255.0, c.rgb[1] / 255.0, c.rgb[2] / 255.0, alpha);
	else
	{
		byte* pal = (byte*)&d_8to24table[(c.basic << 4) + 8];
		glColor4f(pal[0] / 255.0, pal[1] / 255.0, pal[2] / 255.0, alpha);
	}

	glDisable(GL_ALPHA_TEST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	GL_Bind(char_texture);
	glBegin(GL_QUADS);

	while (*str)
	{
		if (*str != 32) //don't waste verts on spaces
			Draw_CharacterQuad(x, y, *str);
		str++;
		x += 8;
	}

	glEnd();

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glEnable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glColor4f(1, 1, 1, 1);
}

static void Draw_BoostAccentRGB(int* r, int* g, int* b) // woods #goldtext
{
	int maxc = q_max(*r, q_max(*g, *b));
	if (maxc <= 0) return;

	// Tunables: tiny lift so darks don't stay muddy, and a gentle gain.
	const float gain = 1.5f; // ~+12% brightness
	const int   lift = 8;     // add a small floor

	// Compute target brightness and derive a uniform scale factor.
	int target = (int)(maxc * gain + 0.5f) + lift;
	if (target > 255) target = 255;
	if (target < maxc) target = maxc; // never dim

	float f = (float)target / (float)maxc;
	*r = q_min(255, (int)(*r * f + 0.5f));
	*g = q_min(255, (int)(*g * f + 0.5f));
	*b = q_min(255, (int)(*b * f + 0.5f));
}

static qboolean Draw_ComputeConcharsAccentColor(plcolour_t* result) // woods #goldtext
{
	gltexture_t* texture = char_texture;
	byte* data = NULL;
	qboolean free_data = false;
	qboolean release_hunk = false;
	int hunk_mark = 0;
	enum srcformat format;
	int width, height;

	if (!texture)
		return false;

	width = texture->source_width;
	height = texture->source_height;
	if (width <= 0 || height <= 0)
		return false;

	format = texture->source_format;

	if (format == SRC_INDEXED)
	{
		size_t size = (size_t)width * height;

		if (!size)
			return false;

		if (texture->source_file[0] && texture->source_offset)
		{
			FILE* f;

			if (COM_FOpenFile(texture->source_file, &f, NULL) == -1 || !f)
				return false;

			data = (byte*)Q_malloc(size);
			if (fseek(f, (long)texture->source_offset, SEEK_SET) != 0 || fread(data, 1, size, f) != size)
			{
				fclose(f);
				free(data);
				return false;
			}

			fclose(f);
			free_data = true;
		}
		else if (!texture->source_file[0] && texture->source_offset)
		{
			data = (byte*)texture->source_offset;
		}
		else
		{
			return false;
		}
	}
	else
	{
		int load_width = width;
		int load_height = height;
		enum srcformat loaded_format = format;
		qboolean malloced = false;

		hunk_mark = Hunk_LowMark();
		release_hunk = true;
		data = Image_LoadImage(texture->source_file, &load_width, &load_height, &loaded_format, &malloced);
		if (!data)
		{
			Hunk_FreeToLowMark(hunk_mark);
			return false;
		}

		width = load_width;
		height = load_height;
		format = loaded_format;

		if (malloced)
			free_data = true;
	}

	if (!data)
	{
		if (release_hunk)
			Hunk_FreeToLowMark(hunk_mark);
		return false;
	}

	{
		int cell_width = width / 16;
		int cell_height = height / 16;

		if (cell_width <= 0 || cell_height <= 0)
			goto cleanup;

		if (format == SRC_INDEXED)
		{
			unsigned int counts[256];
			unsigned int sum_r[256];
			unsigned int sum_g[256];
			unsigned int sum_b[256];
			unsigned int best_count = 0;
			int best_sat = -1;
			int best_r = 0, best_g = 0, best_b = 0;
			int brightest_value = 0;

			memset(counts, 0, sizeof(counts));
			memset(sum_r, 0, sizeof(sum_r));
			memset(sum_g, 0, sizeof(sum_g));
			memset(sum_b, 0, sizeof(sum_b));

			for (int digit = 0; digit < 10; ++digit)
			{
				int ch_index = 18 + digit;
				int row = ch_index >> 4;
				int col = ch_index & 15;
				int x0 = col * cell_width;
				int y0 = row * cell_height;

				for (int py = 0; py < cell_height; ++py)
				{
					int y = y0 + py;
					if (y >= height)
						continue;

					for (int px = 0; px < cell_width; ++px)
					{
						int x = x0 + px;
						byte index;
						byte* rgba;
						int r, g, b;

						if (x >= width)
							continue;

						index = data[y * width + x];
						if (!index)
							continue;

						rgba = (byte*)&d_8to24table_conchars[index];
						if (!rgba[3])
							continue;

						r = rgba[0];
						g = rgba[1];
						b = rgba[2];

						if (r <= 16 && g <= 16 && b <= 16)
							continue;

						{
							int value = q_max(r, q_max(g, b));

							if (value > brightest_value)
								brightest_value = value;
						}

						counts[index]++;
						sum_r[index] += (unsigned int)r;
						sum_g[index] += (unsigned int)g;
						sum_b[index] += (unsigned int)b;
					}
				}
			}

			for (int i = 0; i < 256; ++i)
			{
				unsigned int count = counts[i];

				if (!count)
					continue;

				int r = (int)((sum_r[i] + count / 2) / count);
				int g = (int)((sum_g[i] + count / 2) / count);
				int b = (int)((sum_b[i] + count / 2) / count);
				int maxc = q_max(r, q_max(g, b));
				int minc = q_min(r, q_min(g, b));
				int sat = maxc - minc;

				if (count > best_count || (count == best_count && sat > best_sat))
				{
					best_count = count;
					best_sat = sat;
					best_r = r;
					best_g = g;
					best_b = b;
				}
			}

			if (best_count > 0)
			{
				int best_max = q_max(best_r, q_max(best_g, best_b));

				if (brightest_value > best_max && best_max > 0)
				{
					int scale = brightest_value;
					int r = (best_r * scale + best_max / 2) / best_max;
					int g = (best_g * scale + best_max / 2) / best_max;
					int b = (best_b * scale + best_max / 2) / best_max;

					if (r > 255)
						r = 255;
					if (g > 255)
						g = 255;
					if (b > 255)
						b = 255;

					best_r = r;
					best_g = g;
					best_b = b;
				}

				// Final gentle brightness boost
				Draw_BoostAccentRGB(&best_r, &best_g, &best_b);

				result->type = 2;
				result->rgb[0] = (byte)best_r;
				result->rgb[1] = (byte)best_g;
				result->rgb[2] = (byte)best_b;
				result->basic = 0;

				if (free_data)
					free(data);
				if (release_hunk)
					Hunk_FreeToLowMark(hunk_mark);
				return true;
			}
		}
		else if (format == SRC_RGBA)
		{
			enum { bucket_count = 16 * 16 * 16 };
			unsigned int counts[bucket_count];
			unsigned int sum_r[bucket_count];
			unsigned int sum_g[bucket_count];
			unsigned int sum_b[bucket_count];
			unsigned int best_count = 0;
			int best_sat = -1;
			int best_r = 0, best_g = 0, best_b = 0;
			int brightest_value = 0;

			memset(counts, 0, sizeof(counts));
			memset(sum_r, 0, sizeof(sum_r));
			memset(sum_g, 0, sizeof(sum_g));
			memset(sum_b, 0, sizeof(sum_b));

			for (int digit = 0; digit < 10; ++digit)
			{
				int ch_index = 18 + digit;
				int row = ch_index >> 4;
				int col = ch_index & 15;
				int x0 = col * cell_width;
				int y0 = row * cell_height;

				for (int py = 0; py < cell_height; ++py)
				{
					int y = y0 + py;
					if (y >= height)
						continue;

					for (int px = 0; px < cell_width; ++px)
					{
						int x = x0 + px;
						byte* rgba;
						byte a;
						int r, g, b;
						int bucket;

						if (x >= width)
							continue;

						rgba = data + ((y * width + x) << 2);
						a = rgba[3];
						if (!a)
							continue;

						r = rgba[0];
						g = rgba[1];
						b = rgba[2];

						if (r <= 16 && g <= 16 && b <= 16)
							continue;

						{
							int value = q_max(r, q_max(g, b));

							if (value > brightest_value)
								brightest_value = value;
						}

						bucket = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
						counts[bucket]++;
						sum_r[bucket] += (unsigned int)r;
						sum_g[bucket] += (unsigned int)g;
						sum_b[bucket] += (unsigned int)b;
					}
				}
			}

			for (int i = 0; i < bucket_count; ++i)
			{
				unsigned int count = counts[i];

				if (!count)
					continue;

				int r = (int)((sum_r[i] + count / 2) / count);
				int g = (int)((sum_g[i] + count / 2) / count);
				int b = (int)((sum_b[i] + count / 2) / count);
				int maxc = q_max(r, q_max(g, b));
				int minc = q_min(r, q_min(g, b));
				int sat = maxc - minc;

				if (count > best_count || (count == best_count && sat > best_sat))
				{
					best_count = count;
					best_sat = sat;
					best_r = r;
					best_g = g;
					best_b = b;
				}
			}

			if (best_count > 0)
			{
				int best_max = q_max(best_r, q_max(best_g, best_b));

				if (brightest_value > best_max && best_max > 0)
				{
					int scale = brightest_value;
					int r = (best_r * scale + best_max / 2) / best_max;
					int g = (best_g * scale + best_max / 2) / best_max;
					int b = (best_b * scale + best_max / 2) / best_max;

					if (r > 255)
						r = 255;
					if (g > 255)
						g = 255;
					if (b > 255)
						b = 255;

					best_r = r;
					best_g = g;
					best_b = b;
				}

				// Final gentle brightness boost
				Draw_BoostAccentRGB(&best_r, &best_g, &best_b);

				result->type = 2;
				result->rgb[0] = (byte)best_r;
				result->rgb[1] = (byte)best_g;
				result->rgb[2] = (byte)best_b;
				result->basic = 0;

				if (free_data)
					free(data);
				if (release_hunk)
					Hunk_FreeToLowMark(hunk_mark);
				return true;
			}
		}
	}

cleanup:
	if (free_data && data)
		free(data);
	if (release_hunk)
		Hunk_FreeToLowMark(hunk_mark);

	return false;
}

plcolour_t Draw_GetConcharsAccentColor(void) // woods #goldtext
{
	static plcolour_t cached_color;
	static qboolean cached_valid = false;
	static unsigned int cached_texnum = 0;
	static unsigned short cached_crc = 0;
	static unsigned int cached_width = 0;
	static unsigned int cached_height = 0;
	static char cached_source[MAX_QPATH];

	gltexture_t* texture = char_texture;

	if (!texture)
	{
		if (!cached_valid)
			cached_color = scr_positive_diff_default_color;
		return cached_color;
	}

	if (!cached_valid || cached_texnum != texture->texnum ||
		cached_crc != texture->source_crc ||
		cached_width != texture->source_width ||
		cached_height != texture->source_height ||
		strcmp(cached_source, texture->source_file))
	{
		plcolour_t color;

		if (!Draw_ComputeConcharsAccentColor(&color))
			color = scr_positive_diff_default_color;

		cached_color = color;
		cached_valid = true;
		cached_texnum = texture->texnum;
		cached_crc = texture->source_crc;
		cached_width = texture->source_width;
		cached_height = texture->source_height;
		q_strlcpy(cached_source, texture->source_file, sizeof(cached_source));
	}

	return cached_color;
}

/*
================
Draw_StringAnimatedDots -- woods
Draws "..." with animated opacity cycling through each dot -- woods
================
*/
void Draw_StringAnimatedDots(int x, int y, const char* str)
{
	if (!str || strlen(str) != 3) return; // Only works for 3-character strings like "..."

	static double last_time = 0;
	static int current_dot = 0;
	static double dot_timer = 0;

	const double DOT_CYCLE_TIME = 0.7; // Time for each dot to be fully bright
	const float MIN_ALPHA = 0.1f;
	const float MAX_ALPHA = 1.0f;

	// Update timing
	if (realtime - last_time > DOT_CYCLE_TIME) {
		current_dot = (current_dot + 1) % 3;
		last_time = realtime;
		dot_timer = 0;
	}
	dot_timer = realtime - last_time;

	glEnable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	GL_Bind(char_texture);
	glBegin(GL_QUADS);

	// Draw each dot with appropriate alpha
	for (int i = 0; i < 3; i++) {
		float alpha;

		if (i == current_dot) {
			// Current dot: fade in to full brightness
			float progress = dot_timer / DOT_CYCLE_TIME;
			alpha = MIN_ALPHA + (MAX_ALPHA - MIN_ALPHA) * progress;
		}
		else if (i == (current_dot + 2) % 3) {
			// Previous dot: fade out from full brightness
			float progress = dot_timer / DOT_CYCLE_TIME;
			alpha = MAX_ALPHA - (MAX_ALPHA - MIN_ALPHA) * progress;
		}
		else {
			// Other dots: dim
			alpha = MIN_ALPHA;
		}

		glColor4f(1.0f, 1.0f, 1.0f, alpha);

		if (str[i] != 32) // don't waste verts on spaces
			Draw_CharacterQuad(x + i * 8, y, str[i]);
	}

	glEnd();

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glEnable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glColor4f(1, 1, 1, 1);
}

/*
================
Draw_StringGradientSweep -- woods
Unmasked: white → palette(128) red with a bright core + warm tail.
Masked  : baseline EXACT red; sweep LIGHTENS toward white (no base alpha boost).
================
*/
void Draw_StringGradientSweep(int x, int y, const char* str, float speed, float span_px, float alpha, qboolean masked)
{
	if (!str || !*str) return;

	const int   char_w = 8;
	const int   len = (int)strlen(str);
	if (!len) return;

	const float total_px = (float)(len * char_w);
	if (total_px <= 0.0f) return;

	const float sweep_span = (span_px <= 0.0f) ? 1.0f : span_px;
	const float cycle_w = total_px + sweep_span;
	if (cycle_w <= 0.0f) return;

	const float px_speed = q_max(0.0f, speed);
	const float t = (px_speed > 0.0f) ? fmodf((float)realtime * px_speed, cycle_w) : 0.0f;

	// palette[128] sample for “Quake red”
	byte* red = (byte*)&d_8to24table[128];
	const float red_r = red[0] / 255.0f;
	const float red_g = red[1] / 255.0f;
	const float red_b = red[2] / 255.0f;

	const float draw_alpha = CLAMP(0.0f, alpha, 1.0f);

	// Tunables (in-function “knobs”)
	const float glow_strength_unmasked = 0.20f;   // extra pop at the center of the band (unmasked only)
	const float alpha_boost_unmasked = 0.25f;   // alpha lift in the bright core (unmasked only)
	const float tail_span_factor = 0.35f;   // tail length as fraction of sweep_span

	const float masked_add_gain = 0.40f;   // how much additive lift the masked sweep gives
	const float masked_tail_gain = 0.10f;   // subtle trailing lift for masked
	const float tail_span = q_max(4.0f, sweep_span * tail_span_factor);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_ALPHA_TEST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	GL_Bind(char_texture);
	glBegin(GL_QUADS);

	float px = (float)x;
	for (int i = 0; i < len; ++i)
	{
		unsigned char ch = (unsigned char)str[i];
		if (ch != 32)
		{
			const float cx = (float)(i * char_w + char_w * 0.5f);

			// position of sweep relative to this glyph center
			float d = cx - t;
			if (d < 0.0f) d += cycle_w;

			// inside-band mix (0..1), using smoothstep for soft edges
			float mix = 0.0f;
			float highlight = 0.0f; // peaked at center of sweep
			if (d >= 0.0f && d <= sweep_span)
			{
				float u = CLAMP(0.0f, d / sweep_span, 1.0f);
				// smoothstep
				mix = u * u * (3.0f - 2.0f * u);

				float centered = 1.0f - fabsf(u - 0.5f) * 2.0f; // 0 at edges, 1 at center
				if (centered > 0.0f)
				{
					centered *= centered;
					highlight = centered; // 0..1, bell-like
				}
			}

			// trailing tail (0..1), behind the sweep head
			float tail = 0.0f;
			if (tail_span > 0.0f)
			{
				float wrap = cycle_w - d;
				if (wrap > 0.0f && wrap <= tail_span)
				{
					float u = 1.0f - (wrap / tail_span);
					u = CLAMP(0.0f, u, 1.0f);
					tail = u * u * (3.0f - 2.0f * u);
				}
			}

			int glyph = masked ? ((int)ch + 128) & 255 : (int)ch;

			if (!masked)
			{
				// UNMASKED (white font): white → red, plus bright core + warm tail; alpha can lift in core.
				float r = (1.0f - mix) + mix * red_r; // lerp(white, red, mix)
				float g = (1.0f - mix) + mix * red_g;
				float b = (1.0f - mix) + mix * red_b;

				if (highlight > 0.0f)
				{
					float glow = highlight * glow_strength_unmasked;
					r = CLAMP(0.0f, r + glow, 1.0f);
					g = CLAMP(0.0f, g + glow * 0.5f, 1.0f);
					b = CLAMP(0.0f, b + glow * 0.5f, 1.0f);
				}

				if (tail > 0.0f)
				{
					float fade = tail * 0.15f;
					r = CLAMP(0.0f, r + fade, 1.0f);
					g = CLAMP(0.0f, g + fade * 0.35f, 1.0f);
					b = CLAMP(0.0f, b + fade * 0.35f, 1.0f);
				}

				float final_alpha = draw_alpha;
				if (highlight > 0.0f)
					final_alpha = CLAMP(0.0f, draw_alpha * (1.0f + highlight * alpha_boost_unmasked), 1.0f);

				glColor4f(r, g, b, final_alpha);
				Draw_CharacterQuad((int)px, y, (char)glyph);
			}
			else
			{
				// MASKED (red font): baseline EXACT red (no brightening), sweep LIGHTENS via small additive pass.

				// Base pass: keep the original red exactly (color=white under MODULATE).
				glColor4f(1.0f, 1.0f, 1.0f, draw_alpha);
				Draw_CharacterQuad((int)px, y, (char)glyph);

				// Additive lift only where the sweep/tail passes.
				float add_amt = 0.0f;
				if (highlight > 0.0f)
					add_amt += highlight * masked_add_gain;
				if (tail > 0.0f)
					add_amt += tail * masked_tail_gain;

				if (add_amt > 0.0f)
				{
					// one tiny additive pass in white to lift brightness toward white
					glEnd(); // close current batch to safely change blend func

					glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive
					glBegin(GL_QUADS);
					glColor4f(1.0f, 1.0f, 1.0f, CLAMP(0.0f, add_amt * draw_alpha, 1.0f));
					Draw_CharacterQuad((int)px, y, (char)glyph);
					glEnd();

					// restore normal blending and resume batching
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					glBegin(GL_QUADS);
				}
			}
		}
		px += (float)char_w;
	}

	glEnd();

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glEnable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glColor4f(1, 1, 1, 1);
}


/*
=============
Draw_ScaledPicAlpha -- woods #observerhud #eyemouse
=============
*/
void Draw_ScaledPicAlpha (int x, int y, qpic_t* pic, float scale, float alpha)
{
	if (!pic)
		return;

	glpic_t* gl;

	if (scrap_dirty)
		Scrap_Upload();
	gl = (glpic_t*)pic->data;

	if ((uintptr_t)gl < 0x1000)
		return;

	float width = pic->width * scale;
	float height = pic->height * scale;

	glEnable(GL_BLEND);

	glColor4f(1, 1, 1, alpha);

	glDisable(GL_ALPHA_TEST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	GL_Bind(gl->gltexture);
	glBegin(GL_QUADS);
	glTexCoord2f(gl->sl, gl->tl);
	glVertex2f(x, y);
	glTexCoord2f(gl->sh, gl->tl);
	glVertex2f(x + width, y);
	glTexCoord2f(gl->sh, gl->th);
	glVertex2f(x + width, y + height);
	glTexCoord2f(gl->sl, gl->th);
	glVertex2f(x, y + height);
	glEnd();

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glEnable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
}

/*
=============
Draw_Pic -- johnfitz -- modified
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	if (!pic) return; // woods (iw) #democontrols
	
	glpic_t			*gl;

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;

	if ((uintptr_t)gl < 0x1000) // woods (iw) #democontrols
		return;

	GL_Bind (gl->gltexture);
	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (x, y+pic->height);
	glEnd ();
}

/*
=============
Draw_Pic_RGBA_Outline -- woods #varmatchclock
=============
*/
void Draw_Pic_RGBA_Outline (int x, int y, qpic_t* pic, plcolour_t c, float alpha, float outlineThickness)
{
	if (!pic) return;

	glpic_t* gl;

	if (scrap_dirty)
		Scrap_Upload();
	gl = (glpic_t*)pic->data;

	if ((uintptr_t)gl < 0x1000)
		return;

	glEnable(GL_BLEND);

	float red, green, blue;
	if (c.type == 2) {
		red = c.rgb[0] / 255.0;
		green = c.rgb[1] / 255.0;
		blue = c.rgb[2] / 255.0;
	}
	else {
		byte* pal = (byte*)&d_8to24table[(c.basic << 4) + 8];
		red = pal[0] / 255.0;
		green = pal[1] / 255.0;
		blue = pal[2] / 255.0;
	}

	glDisable(GL_ALPHA_TEST);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	// Draw the outline by drawing a slightly larger quad behind the original
	glColor4f(0.0f, 0.0f, 0.0f, alpha);
	GL_Bind(gl->gltexture);
	glBegin(GL_QUADS);
	glTexCoord2f(gl->sl, gl->tl); glVertex2f(x - outlineThickness, y - outlineThickness);
	glTexCoord2f(gl->sh, gl->tl); glVertex2f(x + pic->width + outlineThickness, y - outlineThickness);
	glTexCoord2f(gl->sh, gl->th); glVertex2f(x + pic->width + outlineThickness, y + pic->height + outlineThickness);
	glTexCoord2f(gl->sl, gl->th); glVertex2f(x - outlineThickness, y + pic->height + outlineThickness);
	glEnd();

	// Draw the filled quad
	glColor4f(red, green, blue, alpha);
	glBegin(GL_QUADS);
	glTexCoord2f(gl->sl, gl->tl); glVertex2f(x, y);
	glTexCoord2f(gl->sh, gl->tl); glVertex2f(x + pic->width, y);
	glTexCoord2f(gl->sh, gl->th); glVertex2f(x + pic->width, y + pic->height);
	glTexCoord2f(gl->sl, gl->th); glVertex2f(x, y + pic->height);
	glEnd();

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glEnable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
}

// woods #sbarstyles for qw hud

void Draw_SubPic_QW (int x, int y, qpic_t *pic, int ofsx, int ofsy, int w, int h)
{
	glpic_t			*gl;
	float nsl, ntl, nsh, nth;
	float oldw, oldh;

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	GL_Bind (gl->gltexture);

    oldw = gl->sh - gl->sl;
    oldh = gl->th - gl->tl;

    nsl = gl->sl + (ofsx * oldw) / pic->width;
    nsh = nsl + (w * oldw) / pic->width;

    ntl = gl->tl + (ofsy * oldh) / pic->height;
    nth = ntl + (h * oldh) / pic->height;

    glBegin (GL_QUADS);

    glTexCoord2f (nsl, ntl);
    glVertex2f (x, y);
    glTexCoord2f (nsh, ntl);
    glVertex2f (x+w, y);
    glTexCoord2f (nsh, nth);
    glVertex2f (x+w, y+h);
    glTexCoord2f (nsl, nth);
    glVertex2f (x, y+h);

    glEnd ();
}

void Draw_SubPic (float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2)
{
	glpic_t			*gl;

	s2 += s1;
	t2 += t1;

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	GL_Bind (gl->gltexture);
	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl*(1-s1) + s1*gl->sh, gl->tl*(1-t1) + t1*gl->th);
	glVertex2f (x, y);
	glTexCoord2f (gl->sl*(1-s2) + s2*gl->sh, gl->tl*(1-t1) + t1*gl->th);
	glVertex2f (x+w, y);
	glTexCoord2f (gl->sl*(1-s2) + s2*gl->sh, gl->tl*(1-t2) + t2*gl->th);
	glVertex2f (x+w, y+h);
	glTexCoord2f (gl->sl*(1-s1) + s1*gl->sh, gl->tl*(1-t2) + t2*gl->th);
	glVertex2f (x, y+h);
	glEnd ();
}

//Spike -- this is for CSQC to do fancy drawing.
void Draw_PicPolygon(qpic_t *pic, unsigned int numverts, polygonvert_t *verts)
{
	glpic_t			*gl;

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	GL_Bind (gl->gltexture);
	glBegin (GL_TRIANGLE_FAN);
	while (numverts --> 0)
	{
		glColor4fv(verts->rgba);
		glTexCoord2f (gl->sl*(1-verts->st[0]) + verts->st[0]*gl->sh, gl->tl*(1-verts->st[1]) + verts->st[1]*gl->th);
		glVertex2f(verts->xy[0], verts->xy[1]);
		verts++;
	}
	glEnd ();
}

/*
=============
Draw_TransPicTranslate -- johnfitz -- rewritten to use texmgr to do translation

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (int x, int y, qpic_t *pic, plcolour_t top, plcolour_t bottom)
{
	static plcolour_t oldtop = {{-2}};
	static plcolour_t oldbottom = {{-2}};

	if (!CL_PLColours_Equals(top, oldtop) || !CL_PLColours_Equals(bottom, oldbottom))
	{
		glpic_t *p = (glpic_t *)pic->data;
		gltexture_t *glt = p->gltexture;
		oldtop = top;
		oldbottom = bottom;
		TexMgr_ReloadImage (glt, top, bottom);
	}
	Draw_Pic (x, y, pic);
}

extern cvar_t scr_concolor; // woods #concolor

/*
================
Draw_ConsoleBackground -- johnfitz -- rewritten -- woods #concolor #conback
================
*/
void Draw_ConsoleBackground (void)
{
	static char      last_conback[MAX_QPATH] = "";
	static qboolean  reported_missing = false;
	static qboolean  reported_blocked = false; /* scr_conback ignored due to gl_load24bit 0 */

	float alpha;
	plcolour_t conback_color;
	const char* conback_str = scr_concolor.string;
	float r, g, b;
	byte* rgb_temp;
	byte  rgb[3];

	/* Parse the scr_concolor cvar. */
	conback_color = CL_PLColours_Parse(conback_str);

	/* Track scr_conback changes so we only warn once per new value. */
	if (strcmp(last_conback, scr_conback.string) != 0) {
		q_strlcpy(last_conback, scr_conback.string, sizeof(last_conback));
		reported_missing = false;
		reported_blocked = false;
	}

	/* Decide between image path vs solid fill. */
	{
		int use_default =
			(conback_color.type == 0) ||
		(conback_color.type == 2 &&
			conback_color.rgb[0] == 0xFF &&
			conback_color.rgb[1] == 0xFF &&
				conback_color.rgb[2] == 0xFF);

	GL_SetCanvas (CANVAS_CONSOLE); // Ensure we're drawing on the console canvas

	alpha = (con_forcedup) ? 1.0f : scr_conalpha.value;
		if (alpha <= 0.0f)
			return;

		if (use_default) {
			qpic_t* pic = NULL;

			/* Decide whether the user override is allowed. */
			qboolean allow_user_img = false;
			if (scr_conback.string[0]) {
				const char* ext = COM_FileGetExtension(scr_conback.string);
				if (ext && !q_strcasecmp(ext, "lmp")) {
					allow_user_img = true;        /* always allow .lmp */
				}
				else if (draw_load24bit) {      /* global hi‑res allowed? */
					allow_user_img = true;
				}
				else if (!reported_blocked) {
					Con_Printf("Console background ignored: gl_load24bit is 0 (only .lmp allowed).\n");
					reported_blocked = true;
				}
			}

			/* Try user-specified override first (if allowed & non-empty). */
			if (allow_user_img) {
				char path[MAX_QPATH];
				char temp_path[MAX_QPATH];
				char base_path[MAX_QPATH];   /* original user string */
				char base_nopfx[MAX_QPATH];  /* ext stripped copy    */
				static const char* ext_full[] = { ".png",".tga",".jpg",".jpeg",".dds",".pcx",".lmp" };
				static const char* ext_lmp[] = { ".lmp" };
				const char** extensions = draw_load24bit ? ext_full : ext_lmp;
				int num_extensions = draw_load24bit
					? (int)(sizeof(ext_full) / sizeof(ext_full[0]))
					: 1;
				int i;
				qboolean found_file = false;

				q_strlcpy(temp_path, scr_conback.string, sizeof(temp_path));
				q_strlcpy(base_path, scr_conback.string, sizeof(base_path));
				q_strlcpy(base_nopfx, scr_conback.string, sizeof(base_nopfx));

				/* If user gave extension, try exactly that. */
				if (COM_FileGetExtension(temp_path)) {
					if (!Q_strncmp(temp_path, "gfx/", 4))
						q_strlcpy(path, temp_path, sizeof(path));
					else
						q_snprintf(path, sizeof(path), "gfx/%s", temp_path);

					if (COM_FileExists(path, NULL)) {
						pic = Draw_TryCachePic(path, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP);
						found_file = true;
					}
				}

				/* No ext (or missing file)? Try allowed extensions. */
				if (!pic) {
					COM_StripExtension(base_nopfx, base_nopfx, sizeof(base_nopfx));
					for (i = 0; i < num_extensions && !pic; i++) {
						if (!Q_strncmp(base_nopfx, "gfx/", 4))
							q_snprintf(path, sizeof(path), "%s%s", base_nopfx, extensions[i]);
						else
							q_snprintf(path, sizeof(path), "gfx/%s%s", base_nopfx, extensions[i]);

						if (COM_FileExists(path, NULL)) {
							pic = Draw_TryCachePic(path, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP);
							found_file = true;
							break;
						}
					}
				}

				/* Inform user once (only when we actually searched). */
				if (!found_file && !reported_missing) {
					char msg[1024];
					size_t ofs = 0;

					reported_missing = true; /* guard early */

					ofs += q_snprintf(msg + ofs, sizeof(msg) - ofs,
						"Console background file not found: %s (tried: ",
						scr_conback.string);

					if (COM_FileGetExtension(base_path)) {
						if (!Q_strncmp(base_path, "gfx/", 4))
							ofs += q_snprintf(msg + ofs, sizeof(msg) - ofs, "%s", base_path);
						else
							ofs += q_snprintf(msg + ofs, sizeof(msg) - ofs, "gfx/%s", base_path);
					}
					else {
						for (i = 0; i < num_extensions; i++) {
							if (i > 0)
								ofs += q_snprintf(msg + ofs, sizeof(msg) - ofs, ", ");
							if (!Q_strncmp(base_nopfx, "gfx/", 4))
								ofs += q_snprintf(msg + ofs, sizeof(msg) - ofs, "%s%s", base_nopfx, extensions[i]);
							else
								ofs += q_snprintf(msg + ofs, sizeof(msg) - ofs, "gfx/%s%s", base_nopfx, extensions[i]);
						}
					}
					q_snprintf(msg + ofs, sizeof(msg) - ofs, ")\n");
					Con_Printf("%s", msg);
				}
			} /* allow_user_img */

			/* Fallback: behave like scr_conback "" (use gfx/conback.lmp). */
			if (!pic) {
				pic = Draw_CachePic("gfx/conback.lmp");
			}

		pic->width = vid.conwidth;
		pic->height = vid.conheight;

		if (alpha < 1.0f)
		{
			if (premul_hud)
				glColor4f (alpha,alpha,alpha,alpha);
			else
			{
				glEnable (GL_BLEND);
				glDisable (GL_ALPHA_TEST);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				glColor4f (1,1,1,alpha);
			}
		}

		Draw_Pic (0, 0, pic);

		if (alpha < 1.0f)
		{
			if (!premul_hud)
			{
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				glEnable (GL_ALPHA_TEST);
				glDisable (GL_BLEND);
			}
			glColor4f (1,1,1,1);
		}
	}
	else
	{
		rgb_temp = CL_PLColours_ToRGB(&conback_color);

		if (rgb_temp) // copy the RGB values to a local array to ensure safe usage

		{
			rgb[0] = rgb_temp[0];
			rgb[1] = rgb_temp[1];
			rgb[2] = rgb_temp[2];

			r = rgb[0] / 255.0f;
			g = rgb[1] / 255.0f;
			b = rgb[2] / 255.0f;
		}
		else
		{
			r = g = b = 0.0f; // fallback to black if RGB conversion fails
		}

		// Set the color with alpha
		glColor4f(r, g, b, alpha);

		// Enable blending for transparency
		glEnable (GL_BLEND);
		glDisable(GL_TEXTURE_2D); // Disable texture rendering

		// Draw a filled quad covering the console area
		glBegin(GL_QUADS);
		glVertex2f(0, 0);
		glVertex2f(vid.conwidth, 0);
		glVertex2f(vid.conwidth, vid.conheight);
		glVertex2f(0, vid.conheight);
		glEnd();

		// Reset OpenGL states
		glEnable(GL_TEXTURE_2D); // Re-enable textures
		glDisable(GL_BLEND);
		glColor4f(1, 1, 1, 1); // Reset color
	}
}
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h)
{
	glpic_t	*gl;

	gl = (glpic_t *)draw_backtile->data;

	glColor3f (1,1,1);
	GL_Bind (gl->gltexture);
	glBegin (GL_QUADS);
	glTexCoord2f (x/64.0, y/64.0);
	glVertex2f (x, y);
	glTexCoord2f ( (x+w)/64.0, y/64.0);
	glVertex2f (x+w, y);
	glTexCoord2f ( (x+w)/64.0, (y+h)/64.0);
	glVertex2f (x+w, y+h);
	glTexCoord2f ( x/64.0, (y+h)/64.0 );
	glVertex2f (x, y+h);
	glEnd ();
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c, float alpha) //johnfitz -- added alpha
{
	byte *pal = (byte *)d_8to24table; //johnfitz -- use d_8to24table instead of host_basepal

	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND); //johnfitz -- for alpha
	glDisable (GL_ALPHA_TEST); //johnfitz -- for alpha
	glColor4f (pal[c*4]/255.0, pal[c*4+1]/255.0, pal[c*4+2]/255.0, alpha); //johnfitz -- added alpha

	glBegin (GL_QUADS);
	glVertex2f (x,y);
	glVertex2f (x+w, y);
	glVertex2f (x+w, y+h);
	glVertex2f (x, y+h);
	glEnd ();

	glColor3f (1,1,1);
	glDisable (GL_BLEND); //johnfitz -- for alpha
	glEnable (GL_ALPHA_TEST); //johnfitz -- for alpha
	glEnable (GL_TEXTURE_2D);
}
void Draw_FillPlayer (int x, int y, int w, int h, plcolour_t c, float alpha)
{
	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND); //johnfitz -- for alpha
	glDisable (GL_ALPHA_TEST); //johnfitz -- for alpha
	if (c.type == 2)
		glColor4f (c.rgb[0]/255.0, c.rgb[1]/255.0, c.rgb[2]/255.0, alpha); //johnfitz -- added alpha
	else
	{
		byte *pal = (byte *)&d_8to24table[(c.basic<<4) + 8]; //johnfitz -- use d_8to24table instead of host_basepal
		glColor4f (pal[0]/255.0, pal[1]/255.0, pal[2]/255.0, alpha); //johnfitz -- added alpha
	}

	glBegin (GL_QUADS);
	glVertex2f (x,y);
	glVertex2f (x+w, y);
	glVertex2f (x+w, y+h);
	glVertex2f (x, y+h);
	glEnd ();

	glColor3f (1,1,1);
	glDisable (GL_BLEND); //johnfitz -- for alpha
	glEnable (GL_ALPHA_TEST); //johnfitz -- for alpha
	glEnable (GL_TEXTURE_2D);
}

/*
================
Draw_FadeScreen -- johnfitz -- revised
================
*/
void Draw_FadeScreen (void)
{
	GL_SetCanvas (CANVAS_DEFAULT);

	glEnable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_TEXTURE_2D);
	glColor4f (0, 0, 0, 0.5);
	glBegin (GL_QUADS);

	glVertex2f (0,0);
	glVertex2f (glwidth, 0);
	glVertex2f (glwidth, glheight);
	glVertex2f (0, glheight);

	glEnd ();
	glColor4f (1,1,1,1);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_ALPHA_TEST);
	glDisable (GL_BLEND);

	Sbar_Changed();
}

/*
================
Draw_GetMenuTransform -- woods #mousemenu (iw)
================
*/
void Draw_GetMenuTransform(vrect_t* bounds, vrect_t* viewport)
{
	float s;
	s = q_min((float)glwidth / 320.0, (float)glheight / 200.0);
	s = CLAMP(1.0, scr_menuscale.value, s);
	// ericw -- doubled width to 640 to accommodate long keybindings
	bounds->x = 0;
	bounds->y = 0;
	bounds->width = 640;
	bounds->height = 200;
	viewport->x = glx + (glwidth - 320 * s) / 2;
	viewport->y = gly + (glheight - 200 * s) / 2;
	viewport->width = 640 * s;
	viewport->height = 200 * s;
}

float canvas_scaling; // woods #autoid

/*
================
GL_SetCanvas -- johnfitz -- support various canvas types
================
*/
void GL_SetCanvas (canvastype newcanvas)
{
	extern vrect_t scr_vrect;
	float s;
	int lines;

	if (newcanvas == currentcanvas)
		return;

	currentcanvas = newcanvas;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity ();

	switch(newcanvas)
	{
	case CANVAS_DEFAULT:
		glOrtho (0, glwidth, glheight, 0, -99999, 99999);
		glViewport (glx, gly, glwidth, glheight);
		break;
	case CANVAS_AUTOID: // woods #autoid
		canvas_scaling = CLAMP(1.0f, scr_conscale.value - 1, (float)glwidth / vid.conwidth);
		glOrtho(0, glwidth / canvas_scaling, glheight / canvas_scaling, 0, -99999, 99999);
		glViewport(glx, gly, glwidth, glheight);
		break;
	case CANVAS_DEFAULT2: // woods
		glOrtho(0, glwidth / 2, glheight / 2, 0, -99999, 99999);
		glViewport(glx, gly, glwidth, glheight);
		break;
	case CANVAS_SCOREBOARD: // woods for +showscores #scoreboard
		s = (float)glwidth / vid.conwidth; //use console scale
		glOrtho (0, glwidth / s, glheight / s, 0, -99999, 99999);
		glViewport(glx, gly, glwidth, glheight);
		break;
	case CANVAS_CONSOLE:
		lines = vid.conheight - (scr_con_current * vid.conheight / glheight);
		glOrtho (0, vid.conwidth, vid.conheight + lines, lines, -99999, 99999);
		glViewport (glx, gly, glwidth, glheight);
		break;
	case CANVAS_MENU:
		s = q_min((float)glwidth / 320.0f, (float)glheight / 200.0f);
		s = CLAMP (1.0f, scr_menuscale.value, s);
		// ericw -- doubled width to 640 to accommodate long keybindings
		glOrtho (0, 640, 200, 0, -99999, 99999);
		glViewport (glx + (glwidth - 320*s) / 2, gly + (glheight - 200*s) / 2, 640*s, 200*s);
		break;
	case CANVAS_MENU2:
		s = q_min((float)glwidth / 320.0, (float)glheight / 200.0);
		s = CLAMP(1.0, scr_menuscale.value-1, s);
		// ericw -- doubled width to 640 to accommodate long keybindings
		glOrtho(0, 640, 200, 0, -99999, 99999);
		glViewport(glx + (glwidth - 320 * s) / 2, gly + (glheight - 200 * s) / 2, 640 * s, 200 * s);
		break;
	case CANVAS_MENUQC:
		s = q_min((float)glwidth / 320.0, (float)glheight / 200.0);
		s = CLAMP (1.0, scr_menuscale.value, s);
		glOrtho (0, glwidth/s, glheight/s, 0, -99999, 99999);
		glViewport (glx, gly, glwidth, glheight);
		break;
	case CANVAS_CSQC:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		glOrtho (0, glwidth/s, glheight/s, 0, -99999, 99999);
		glViewport (glx, gly, glwidth, glheight);
		break;
	case CANVAS_MOD:       //  woods added for server join messages, flag status, etc #modprint #centerprintbg
		s = (float)glwidth / vid.conwidth; //use console scale
		s = CLAMP (1, scr_conscale.value, s);
		glOrtho (-320, 640, 400, 0, -99999, 99999);
		glViewport (glx + (glwidth - 960*s) / 2, gly + (glheight - 525*s) / 1.25, 960*s, 400*s);
		break;
	case CANVAS_HINT: // woods #qssmhints
		s = CLAMP(1.0f, scr_conscale.value, (float)glwidth / vid.conwidth);
		glOrtho(0.0f, 800.0f, 500.0f, 0.0f, -99999.0f, 99999.0f);
		glViewport(glx + (glwidth - 800.0f * s) / 2.0f, gly + (glheight - 500.0f * s) / 2.0f, 800.0f * s, 500.0f * s);
		break;
	case CANVAS_OBSERVER:    //  woods for #observer mode #centerprintbg
		s = (float)glwidth / vid.conwidth; //use console scale
		s = CLAMP(1, scr_conscale.value, s);
		glOrtho (-320, 640, 400, 0, -99999, 99999);
		glViewport (glx + (glwidth - 960*s) / 2, gly + (glheight - 750*s) / (scr_conscale.value - 1) / 1.25, 960*s, 400*s);
		break;
	case CANVAS_SBAR:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		/*if (cl.gametype == GAME_DEATHMATCH) // woods #sbarmiddle
		{
			glOrtho (0, glwidth / s, 48, 0, -99999, 99999);
			glViewport (glx, gly, glwidth, 48*s);
		}
		else*/
		{
			glOrtho (0, 320, 48, 0, -99999, 99999);
			glViewport (glx + (glwidth - 320*s) / 2, gly, 320*s, 48*s);
		}
		break;
	case CANVAS_SBAR2: // woods #speed #speedometer
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		glOrtho (0, 320, 270, 0, -99999, 99999);
		glViewport (glx + (glwidth - 320*s) / 2, gly, 320*s, 270*s);
		break;
	case CANVAS_IBAR_QW: //split for cleaner QW hud support // woods #sbarstyles
        s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
        glOrtho (0, 44, 188, 0, -99999, 99999);
        glViewport (glwidth - 44*s, 48*s, 44*s, 188*s);
        break;
	case CANVAS_CROSSHAIR: //0,0 is center of viewport
		s = CLAMP (1.0f, scr_crosshairscale.value, 10.0f);
		glOrtho (scr_vrect.width/-2/s, scr_vrect.width/2/s, scr_vrect.height/2/s, scr_vrect.height/-2/s, -99999, 99999);
		glViewport (scr_vrect.x, glheight - scr_vrect.y - scr_vrect.height, scr_vrect.width & ~1, scr_vrect.height & ~1);
		break;
	case CANVAS_CROSSHAIR2: //0,0 is center of viewport -- woods #texturepointer
		s = CLAMP(1.0f, scr_conscale.value, 10.0f);
		glOrtho(scr_vrect.width / -2 / s, scr_vrect.width / 2 / s, scr_vrect.height / 2 / s, scr_vrect.height / -2 / s, -99999, 99999);
		glViewport(scr_vrect.x, glheight - scr_vrect.y - scr_vrect.height, scr_vrect.width & ~1, scr_vrect.height & ~1);
		break;
	case CANVAS_MATCHCLOCK: // 0,0 is now the top left of the viewport // woods #varmatchclock
		s = CLAMP (1.0, scr_matchclockscale.value, 10.0);
		glOrtho(0, scr_vrect.width / s, scr_vrect.height / s, 0, -99999, 99999);
		glViewport(scr_vrect.x, glheight - scr_vrect.y - scr_vrect.height, scr_vrect.width & ~1, scr_vrect.height & ~1);
		break;
	case CANVAS_BOTTOMLEFT: //used by devstats
		s = (float)glwidth/vid.conwidth; //use console scale
		glOrtho (0, 320, 200, 0, -99999, 99999);
		glViewport (glx, gly, 320*s, 200*s);
		break;
	case CANVAS_SCORES: // woods #observerhud
		s = (float)glwidth / vid.conwidth; //use console scale
		glOrtho(0, 320, 300, 0, -99999, 99999);
		glViewport(glx, gly, 320 * s, 300 * s);
		break;
	case CANVAS_BOTTOMLEFT2: //used by devstats    // woods #scrping
		s = (float)glwidth/vid.conwidth; //use console scale
		glOrtho (0, 320, 200, 0, -99999, 99999);
		glViewport (glx, gly - 100*s, 320*s, 200*s);
		break;
	case CANVAS_IBAR_QWQE: // woods for QE hud, scr_sbar 3
		s = CLAMP(1.0, scr_sbarscale.value / 1.4, (float)glwidth / 320.0);
		glOrtho(0, 44, 188, 0, -99999, 99999);
		glViewport(glwidth - 65 * s, 117 * s, 44 * s, 188 * s);
		break;
	case CANVAS_SBARQE: // woods for QE hud, scr_sbar 3
		s = CLAMP(1.0, scr_conscale.value, (float)glwidth / 320.0);
		glOrtho(0, 320, 48, 0, -99999, 99999);
		glViewport(glx + (glwidth - 320 * s) / 2, gly, 320 * s, 48 * s);
		break;
	case CANVAS_BOTTOMLEFTQE: // woods for QE hud, scr_sbar 3
		s = CLAMP(1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		glOrtho(0, 320, 200, 0, -99999, 99999);
		glViewport(glx, gly, 320 * s, 200 * s);
		break;
	case CANVAS_BOTTOMLEFTQESCORES: // woods for QE hud, scr_sbar 3
		s = CLAMP(1.0, scr_sbarscale.value / 1.2, (float)glwidth / 320.0);
		glOrtho(0, 320, 300, 0, -99999, 99999);
		glViewport(glx, gly, 320 * s, 300 * s);
		break;
	case CANVAS_BOTTOMLEFTQESMALL: // woods for QE hud, scr_sbar 3
		s = CLAMP(1.0, scr_sbarscale.value/1.2, (float)glwidth / 320.0);
		glOrtho(0, 320, 200, 0, -99999, 99999);
		glViewport(glx, gly, 320 * s, 200 * s);
		break;
	case CANVAS_BOTTOMRIGHTQE: // woods for QE hud, scr_sbar 3
		s = CLAMP(1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		glOrtho(0, 320, 200, 0, -99999, 99999);
		glViewport(glx + glwidth - 320 * s, gly, 320 * s, 200 * s);
		break;
	case CANVAS_BOTTOMRIGHTQESMALL: // woods for QE hud, scr_sbar 3
		s = CLAMP(1.0, scr_sbarscale.value / 1.2, (float)glwidth / 320.0);
		glOrtho(0, 320, 200, 0, -99999, 99999);
		glViewport(glx + glwidth - 320 * s, gly, 320 * s, 200 * s);
		break;
	case CANVAS_BOTTOMRIGHT: //used by fps/clock
		s = (float)glwidth/vid.conwidth; //use console scale
		glOrtho (0, 320, 200, 0, -99999, 99999);
		glViewport (glx+glwidth-320*s, gly, 320*s, 200*s);
		break;
	case CANVAS_TOPRIGHT: //used by disc
		s = 1;
		glOrtho (0, 320, 200, 0, -99999, 99999);
		glViewport (glx+glwidth-320*s, gly+glheight-200*s, 320*s, 200*s);
		break;
	case CANVAS_TOPRIGHT2: // woods for upper right match clock placement #matchhud
		s = ((float)glwidth / vid.conwidth) * 2; //use console scale
		glOrtho(0, 320, 200, 0, -99999, 99999);
		glViewport(glx + glwidth - 320 * s, gly + glheight - 16 * s, 320 * s, 200 * s);
		break;
	case CANVAS_TOPRIGHT3: // woods for upper right match scores and colors placement #matchhud #flagstatus
		s = ((float)glwidth / vid.conwidth) * 2; //use console scale
		glOrtho(0, 320, 200, 0, -99999, 99999);
		glViewport(glx + glwidth - 48 * s, (gly + glheight - 212 * s), 320 * s, 200 * s);
		break;
	case CANVAS_TOPRIGHT4: // woods #hud_diff
		s = ((float)glwidth / vid.conwidth); //use console scale
		glOrtho(0, 320, 200, 0, -99999, 99999);
		glViewport(glx + glwidth - 200 * s, (gly + glheight - 212 * s), 320 * s, 200 * s);
		break;
	default:
		Sys_Error ("GL_SetCanvas: bad canvas type");
	}

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity ();
}

/*
================
GL_Set2D -- johnfitz -- rewritten
================
*/
void GL_Set2D (void)
{
	currentcanvas = CANVAS_INVALID;
	GL_SetCanvas (CANVAS_DEFAULT);

	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);

	if (premul_hud)
	{
		glEnable (GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	}
	else
	{
		glDisable (GL_BLEND);
		glEnable (GL_ALPHA_TEST);
	}
	glColor4f (1,1,1,1);
}
