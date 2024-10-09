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

#ifndef _QUAKE_DRAW_H
#define _QUAKE_DRAW_H

// draw.h -- these are the only functions outside the refresh allowed
// to touch the vid buffer

typedef union
{
	struct{
		byte type; //0 for none, 1 for legacy colours, 2 for rgb.
		byte rgb[3];
		byte basic; //used in legacy contexts where an rgb value will not work.
	};
	int key;	//for fast compares
} plcolour_t;
plcolour_t CL_PLColours_Parse(const char *s);
plcolour_t CL_PLColours_FromLegacy(int val);
char *CL_PLColours_ToString(plcolour_t c);
byte *CL_PLColours_ToRGB(plcolour_t *c);
#define CL_PLColours_Equals(a,b) (!memcmp(&a,&b, sizeof(plcolour_t)))

extern	qpic_t		*draw_disc;	// also used on sbar
extern	qboolean	custom_conchars; // woods (iw) #democontrols

#define CHARSIZE	8 // woods #centerprintbg (iw)

void Draw_Init (void);
void Draw_Character (int x, int y, int num);
void Draw_CharacterRGBA (int x, int y, int num, plcolour_t c, float alpha); // woods #iwtabcomplete
void Draw_Character_Rotation (int x, int y, int num, int rotation); // woods #movementkeys
void Draw_DebugChar (char num);
void Draw_Pic (int x, int y, qpic_t *pic);
void Draw_Pic_RGBA_Outline (int x, int y, qpic_t* pic, plcolour_t c, float alpha, float outlineThickness); // woods #varmatchclock
void Draw_SubPic (float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2);
void Draw_TransPicTranslate (int x, int y, qpic_t *pic, plcolour_t top, plcolour_t bottom); //johnfitz -- more parameters
void Draw_ConsoleBackground (void); //johnfitz -- removed parameter int lines
void Draw_TileClear (int x, int y, int w, int h);
void Draw_Fill (int x, int y, int w, int h, int c, float alpha); //johnfitz -- added alpha
void Draw_FillPlayer (int x, int y, int w, int h, plcolour_t c, float alpha); //Spike - for richer player colours.
void Draw_FadeScreen (void);
void Draw_String (int x, int y, const char *str);
void Draw_StringRGBA (int x, int y, const char* str, plcolour_t c, float alpha); // woods
qpic_t *Draw_PicFromWad2 (const char *name, unsigned int texflags);
qpic_t *Draw_PicFromWad (const char *name);
qpic_t *Draw_CachePic (const char *path);
qpic_t *Draw_TryCachePic (const char *path, unsigned int texflags);
void Draw_NewGame (void);
void Draw_GetMenuTransform(vrect_t* bounds, vrect_t* viewport); // woods #mousemenu (iw)
qboolean Draw_ReloadTextures(qboolean force);

//Spike -- this is for csqc
typedef struct
{
	vec_t xy[2];
	vec_t st[2];
	vec4_t rgba;
} polygonvert_t;
void Draw_PicPolygon(qpic_t *pic, unsigned int numverts, polygonvert_t *verts);

void GL_SetCanvas (canvastype newcanvas); //johnfitz

#endif	/* _QUAKE_DRAW_H */

