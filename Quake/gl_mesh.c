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
// gl_mesh.c: triangle model functions

#include "quakedef.h"


/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

static size_t GLMesh_CheckedSize (size_t count, size_t size, const char *caller)
{
	if (size && count > SIZE_MAX / size)
		Sys_Error ("%s: allocation too large", caller);
	return count * size;
}

static int GLMesh_CheckedHunkSize (size_t count, size_t size, const char *caller)
{
	size_t bytes = GLMesh_CheckedSize (count, size, caller);
	if (bytes > (size_t)INT_MAX)
		Sys_Error ("%s: allocation too large", caller);
	return (int)bytes;
}

static void GLMesh_AddCheckedSize (size_t *total, size_t bytes, const char *caller)
{
	if (*total > SIZE_MAX - bytes || *total + bytes > (size_t)INT_MAX)
		Sys_Error ("%s: allocation too large", caller);
	*total += bytes;
}

/*
================
GL_MakeAliasModelDisplayLists

Saves data needed to build the VBO for this model on the hunk. Afterwards this
is copied to Mod_Extradata.

Original code by MH from RMQEngine
================
*/
void GL_MakeAliasModelDisplayLists (qmodel_t *m, aliashdr_t *paliashdr)
{
	int i, j;
	int mark;
	trivertx_t *verts;
	unsigned short *indexes;
	unsigned short *remap;
	aliasmesh_t *desc;

	// there can never be more than this number of verts and we just put them all on the hunk
	// (each vertex can be used twice, once with the original UVs and once with the seam adjustment)
	desc = (aliasmesh_t *) Hunk_Alloc (GLMesh_CheckedHunkSize ((size_t)paliashdr->numverts * 2, sizeof (aliasmesh_t), "GL_MakeAliasModelDisplayLists"));

	// there will always be this number of indexes
	indexes = (unsigned short *) Hunk_Alloc (GLMesh_CheckedHunkSize ((size_t)paliashdr->numtris * 3, sizeof (unsigned short), "GL_MakeAliasModelDisplayLists"));

	paliashdr->indexes = (intptr_t) indexes - (intptr_t) paliashdr;
	paliashdr->meshdesc = (intptr_t) desc - (intptr_t) paliashdr;
	paliashdr->numindexes = 0;
	paliashdr->numverts_vbo = 0;

	mark = Hunk_LowMark ();

	// each pair of elements in the remap array corresponds to one source vertex
	// each value is the final index + 1, or 0 if the corresponding vertex hasn't been emitted yet
	remap = (unsigned short *) Hunk_Alloc (GLMesh_CheckedHunkSize ((size_t)paliashdr->numverts * 2, sizeof (remap[0]), "GL_MakeAliasModelDisplayLists"));

	for (i = 0; i < paliashdr->numtris; i++)
	{
		for (j = 0; j < 3; j++)
		{
			// index into hdr->vertexes
			unsigned short vertindex = triangles[i].vertindex[j];

			// index into remap table
			int v = vertindex * 2;

			// check for back side
			if (!triangles[i].facesfront && stverts[vertindex].onseam)
				v++;

			// emit new vertex if it doesn't already exist
			if (!remap[v])
			{
				// basic s/t coords
				int s = stverts[vertindex].s;
				int t = stverts[vertindex].t;

				// check for back side and adjust texcoord s
				if (v & 1)
					s += paliashdr->skinwidth / 2;

				desc[paliashdr->numverts_vbo].vertindex = vertindex;
				desc[paliashdr->numverts_vbo].st[0] = s;
				desc[paliashdr->numverts_vbo].st[1] = t;

				remap[v] = ++paliashdr->numverts_vbo;
			}

			// emit index
			indexes[paliashdr->numindexes++] = remap[v] - 1;
		}
	}

	// free temporary remap data before allocating persistent pose vertices
	Hunk_FreeToLowMark (mark);

	switch(paliashdr->poseverttype)
	{
	case PV_QUAKEFORGE:
		verts = (trivertx_t *) Hunk_AllocNoFill (GLMesh_CheckedHunkSize ((size_t)paliashdr->nummorphposes * (size_t)paliashdr->numverts_vbo * 2, sizeof(*verts), "GL_MakeAliasModelDisplayLists"));
		paliashdr->vertexes = (byte *)verts - (byte *)paliashdr;
		for (i=0 ; i<paliashdr->nummorphposes ; i++)
			for (j=0 ; j<paliashdr->numverts_vbo ; j++)
			{
				verts[i*paliashdr->numverts_vbo*2 + j] = poseverts_mdl[i][desc[j].vertindex];
				verts[i*paliashdr->numverts_vbo*2 + j + paliashdr->numverts_vbo] = poseverts_mdl[i][desc[j].vertindex + paliashdr->numverts_vbo];
			}
		break;
	case PV_QUAKE1:
		verts = (trivertx_t *) Hunk_AllocNoFill (GLMesh_CheckedHunkSize ((size_t)paliashdr->nummorphposes * (size_t)paliashdr->numverts_vbo, sizeof(*verts), "GL_MakeAliasModelDisplayLists"));
		paliashdr->vertexes = (byte *)verts - (byte *)paliashdr;
		for (i=0 ; i<paliashdr->nummorphposes ; i++)
			for (j=0 ; j<paliashdr->numverts_vbo ; j++)
				verts[i*paliashdr->numverts_vbo + j] = poseverts_mdl[i][desc[j].vertindex];
		break;
	case PV_IQM:
	case PV_QUAKE3:
		break;	//invalid here.
	}
}

/*
================
GLMesh_LoadVertexBuffer

Upload the given alias model's mesh to a VBO

Original code by MH from RMQEngine

may update the mesh vbo/ebo offsets.
================
*/
void GLMesh_LoadVertexBuffer (qmodel_t *m, aliashdr_t *mainhdr)
{
	//we always need vertex array data.
	//if we don't support vbos(gles?) then we just use system memory.
	//if we're not using glsl(gles1?), then we don't actually need all the data, but we do still need some so its easier to just alloc the lot.
	size_t totalvbosize = 0;
	const aliasmesh_t *desc;
	const void *trivertexes;
	byte *ebodata;
	byte *vbodata;
	int f;
	aliashdr_t *hdr;
	size_t numindexes, numverts;
	intptr_t stofs;
	intptr_t vertofs;

	if (isDedicated)
		return;

	//count how much space we're going to need.
	for(hdr = mainhdr, numverts = 0, numindexes = 0; ; )
	{
		if (hdr->nummorphposes < 0 || hdr->numverts_vbo < 0 || hdr->numindexes < 0)
			Sys_Error ("GLMesh_LoadVertexBuffer: invalid alias mesh counts");

		switch(hdr->poseverttype)
		{
		case PV_QUAKE1:
			GLMesh_AddCheckedSize (&totalvbosize, GLMesh_CheckedSize ((size_t)hdr->nummorphposes * (size_t)hdr->numverts_vbo, sizeof (meshxyz_mdl_t), "GLMesh_LoadVertexBuffer"), "GLMesh_LoadVertexBuffer"); // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
			break;
		case PV_QUAKEFORGE:
			GLMesh_AddCheckedSize (&totalvbosize, GLMesh_CheckedSize ((size_t)hdr->nummorphposes * (size_t)hdr->numverts_vbo, sizeof (meshxyz_mdl16_t), "GLMesh_LoadVertexBuffer"), "GLMesh_LoadVertexBuffer");
			break;
		case PV_QUAKE3:
			GLMesh_AddCheckedSize (&totalvbosize, GLMesh_CheckedSize ((size_t)hdr->nummorphposes * (size_t)hdr->numverts_vbo, sizeof (meshxyz_md3_t), "GLMesh_LoadVertexBuffer"), "GLMesh_LoadVertexBuffer");
			break;
		case PV_IQM:
			GLMesh_AddCheckedSize (&totalvbosize, GLMesh_CheckedSize ((size_t)hdr->nummorphposes * (size_t)hdr->numverts_vbo, sizeof (iqmvert_t), "GLMesh_LoadVertexBuffer"), "GLMesh_LoadVertexBuffer");
			break;
		}

		GLMesh_AddCheckedSize (&numverts, (size_t)hdr->numverts_vbo, "GLMesh_LoadVertexBuffer");
		GLMesh_AddCheckedSize (&numindexes, (size_t)hdr->numindexes, "GLMesh_LoadVertexBuffer");

		if (hdr->nextsurface)
			hdr = (aliashdr_t*)((byte*)hdr + hdr->nextsurface);
		else
			break;
	}
	hdr = NULL;

	vertofs = 0;
	if (totalvbosize > (size_t)INT_MAX - 7)
		Sys_Error ("GLMesh_LoadVertexBuffer: allocation too large");
	totalvbosize = (totalvbosize+7)&~7;	//align it.
	stofs = (intptr_t)totalvbosize;
	GLMesh_AddCheckedSize (&totalvbosize, GLMesh_CheckedSize (numverts, sizeof (meshst_t), "GLMesh_LoadVertexBuffer"), "GLMesh_LoadVertexBuffer");

	if (!totalvbosize) return;
	if (!numindexes) return;

	//create an elements buffer
	ebodata = (byte *) malloc(GLMesh_CheckedSize (numindexes, sizeof(unsigned short), "GLMesh_LoadVertexBuffer"));
	if (!ebodata)
		return;	//fatal

	// create the vertex buffer (empty)
	vbodata = (byte *) malloc(totalvbosize);
	if (!vbodata)
	{	//fatal
		free(ebodata);
		return;
	}
	memset(vbodata, 0, totalvbosize);

	numindexes = 0;

	for(hdr = mainhdr, numverts = 0, numindexes = 0; ; )
	{
		// grab the pointers to data in the extradata
		desc = (aliasmesh_t *) ((byte *) hdr + hdr->meshdesc);
		trivertexes = (void *) ((byte *)hdr + hdr->vertexes);

		//submit the index data.
		hdr->eboofs = (intptr_t)GLMesh_CheckedSize (numindexes, sizeof (unsigned short), "GLMesh_LoadVertexBuffer");
		numindexes += hdr->numindexes;
		memcpy(ebodata + hdr->eboofs, (short *) ((byte *) hdr + hdr->indexes), hdr->numindexes * sizeof (unsigned short));

		hdr->vbovertofs = vertofs;

	// fill in the vertices at the start of the buffer
		switch(hdr->poseverttype)
		{
		case PV_QUAKE1:
			for (f = 0; f < hdr->nummorphposes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
			{
				int v;
				meshxyz_mdl_t *xyz = (meshxyz_mdl_t *) (vbodata + vertofs);
				const trivertx_t *tv = (const trivertx_t*)trivertexes + (hdr->numverts_vbo * f);
				vertofs += hdr->numverts_vbo * sizeof (*xyz);

				for (v = 0; v < hdr->numverts_vbo; v++, tv++)
				{
					xyz[v].xyz[0] = tv->v[0];
					xyz[v].xyz[1] = tv->v[1];
					xyz[v].xyz[2] = tv->v[2];
					xyz[v].xyz[3] = 1;	// need w 1 for 4 byte vertex compression

					// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
					// this introduces some error (less than 0.004), but the normals were very coarse
					// to begin with
					xyz[v].normal[0] = 127 * r_avertexnormals[tv->lightnormalindex][0];
					xyz[v].normal[1] = 127 * r_avertexnormals[tv->lightnormalindex][1];
					xyz[v].normal[2] = 127 * r_avertexnormals[tv->lightnormalindex][2];
					xyz[v].normal[3] = 0;	// unused; for 4-byte alignment
				}
			}
			break;
		case PV_QUAKEFORGE:
			for (f = 0; f < hdr->nummorphposes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
			{
				int v;
				meshxyz_mdl16_t *xyz = (meshxyz_mdl16_t *) (vbodata + vertofs);
				const trivertx_t *tv = (const trivertx_t*)trivertexes + (hdr->numverts_vbo*2 * f);
				vertofs += hdr->numverts_vbo * sizeof (*xyz);

				for (v = 0; v < hdr->numverts_vbo; v++, tv++)
				{
					xyz[v].xyz[0] = (tv->v[0]<<8) | tv[hdr->numverts_vbo].v[0];
					xyz[v].xyz[1] = (tv->v[1]<<8) | tv[hdr->numverts_vbo].v[0];
					xyz[v].xyz[2] = (tv->v[2]<<8) | tv[hdr->numverts_vbo].v[0];
					xyz[v].xyz[3] = 1;	// need w 1 for 4 byte vertex compression

					// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
					// this introduces some error (less than 0.004), but the normals were very coarse
					// to begin with
					xyz[v].normal[0] = 127 * r_avertexnormals[tv->lightnormalindex][0];
					xyz[v].normal[1] = 127 * r_avertexnormals[tv->lightnormalindex][1];
					xyz[v].normal[2] = 127 * r_avertexnormals[tv->lightnormalindex][2];
					xyz[v].normal[3] = 0;	// unused; for 4-byte alignment
				}
			}
			break;
		case PV_QUAKE3:
			for (f = 0; f < hdr->nummorphposes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
			{
				int v;
				meshxyz_md3_t *xyz = (meshxyz_md3_t *) (vbodata + vertofs);
				const md3XyzNormal_t *tv = (const md3XyzNormal_t*)trivertexes + (hdr->numverts_vbo * f);
				float lat,lng;
				vertofs += hdr->numverts_vbo * sizeof (*xyz);

				for (v = 0; v < hdr->numverts_vbo; v++, tv++)
				{
					xyz[v].xyz[0] = tv->xyz[0];
					xyz[v].xyz[1] = tv->xyz[1];
					xyz[v].xyz[2] = tv->xyz[2];
					xyz[v].xyz[3] = 1;	// need w 1 for 4 byte vertex compression

					// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
					// this introduces some error (less than 0.004), but the normals were very coarse
					// to begin with
					lat = (float)tv->latlong[0] * (2 * M_PI)*(1.0 / 255.0);
					lng = (float)tv->latlong[1] * (2 * M_PI)*(1.0 / 255.0);
					xyz[v].normal[0] = 127 * cos ( lng ) * sin ( lat );
					xyz[v].normal[1] = 127 * sin ( lng ) * sin ( lat );
					xyz[v].normal[2] = 127 * cos ( lat );
					xyz[v].normal[3] = 0;	// unused; for 4-byte alignment
				}
			}
			break;
		case PV_IQM:
			for (f = 0; f < hdr->nummorphposes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
			{
				int v;
				iqmvert_t *xyz = (iqmvert_t *) (vbodata + vertofs);
				const iqmvert_t *tv = (const iqmvert_t*)trivertexes + (hdr->numverts_vbo * f);
				vertofs += hdr->numverts_vbo * sizeof (*xyz);

				for (v = 0; v < hdr->numverts_vbo; v++, tv++)
					xyz[v] = *tv;
			}
			break;
		}

		// fill in the ST coords at the end of the buffer
		{
			meshst_t *st;
			float hscale, vscale;

			//johnfitz -- padded skins
			hscale = (float)hdr->skinwidth/(float)TexMgr_PadConditional(hdr->skinwidth);
			vscale = (float)hdr->skinheight/(float)TexMgr_PadConditional(hdr->skinheight);
			//johnfitz

			hdr->vbostofs = stofs; 
			st = (meshst_t *) (vbodata + stofs);
			stofs += hdr->numverts_vbo*sizeof(*st);
			switch(hdr->poseverttype)
			{
			case PV_QUAKE3:
				for (f = 0; f < hdr->numverts_vbo; f++)
				{	//md3 has floating-point skin coords. use the values directly.
					st[f].st[0] = hscale * desc[f].st[0];
					st[f].st[1] = vscale * desc[f].st[1];
				}
				break;
			case PV_QUAKEFORGE:
			case PV_QUAKE1:
				for (f = 0; f < hdr->numverts_vbo; f++)
				{
					st[f].st[0] = hscale * ((float) desc[f].st[0] + 0.5f) / (float) hdr->skinwidth;
					st[f].st[1] = vscale * ((float) desc[f].st[1] + 0.5f) / (float) hdr->skinheight;
				}
				break;
			case PV_IQM:
				//st coords are interleaved.
				break;
			}
		}

		if (hdr->nextsurface)
			hdr = (aliashdr_t*)((byte*)hdr + hdr->nextsurface);
		else
			break;
	}
	hdr = NULL;

	if (gl_vbo_able)
	{
		// upload indexes buffer
		GL_DeleteBuffersFunc (1, &m->meshindexesvbo);
		GL_GenBuffersFunc (1, &m->meshindexesvbo);
		GL_BindBufferFunc (GL_ELEMENT_ARRAY_BUFFER, m->meshindexesvbo);
		GL_BufferDataFunc (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)GLMesh_CheckedSize (numindexes, sizeof (unsigned short), "GLMesh_LoadVertexBuffer"), ebodata, GL_STATIC_DRAW);

		// upload vertexes buffer
		GL_DeleteBuffersFunc (1, &m->meshvbo);
		GL_GenBuffersFunc (1, &m->meshvbo);
		GL_BindBufferFunc (GL_ARRAY_BUFFER, m->meshvbo);
		GL_BufferDataFunc (GL_ARRAY_BUFFER, (GLsizeiptr)totalvbosize, vbodata, GL_STATIC_DRAW);

		free (vbodata);
		free (ebodata);

		m->meshvboptr = NULL;
		m->meshindexesvboptr = NULL;
	}
	else
	{
		m->meshvboptr = vbodata;
		m->meshindexesvboptr = ebodata;
	}

// invalidate the cached bindings
	GL_ClearBufferBindings ();
}

/*
================
GLMesh_LoadVertexBuffers

Loop over all precached alias models, and upload each one to a VBO.
================
*/
void GLMesh_LoadVertexBuffers (void)
{
	int j;
	qmodel_t *m;
	aliashdr_t *hdr;

	for (j = 1; j < MAX_MODELS; j++)
	{
		if (!(m = cl.model_precache[j])) break;
		if (m->type != mod_alias) continue;

		hdr = (aliashdr_t *) Mod_Extradata (m);
		
		GLMesh_LoadVertexBuffer (m, hdr);
	}
}

/*
================
GLMesh_DeleteVertexBuffers

Delete VBOs for all loaded alias models
================
*/
void GLMesh_DeleteVertexBuffers (void)
{
	int j;
	qmodel_t *m;
	
	if (!gl_vbo_able)
		return;

	for (j = 1; j < MAX_MODELS; j++)
	{
		if (!(m = cl.model_precache[j])) break;
		if (m->type != mod_alias) continue;

		if (m->meshvbo)
			GL_DeleteBuffersFunc (1, &m->meshvbo);
		m->meshvbo = 0;
		free(m->meshvboptr);
		m->meshvboptr = NULL;

		if (m->meshindexesvbo)
			GL_DeleteBuffersFunc (1, &m->meshindexesvbo);
		m->meshindexesvbo = 0;
		free(m->meshindexesvboptr);
		m->meshindexesvboptr = NULL;
	}

	GL_ClearBufferBindings ();
}






//from gl_model.c
extern char	loadname[];	// for hunk tags
extern char diskname[]; // for skin files/etc.
void Mod_CalcAliasBounds (aliashdr_t *a);


#define MD3_VERSION 15
//structures from Tenebrae
typedef struct {
	int			ident;
	int			version;

	char		name[64];

	int			flags;	//assumed to match quake1 models, for lack of somewhere better.

	int			numFrames;
	int			numTags;
	int			numSurfaces;

	int			numSkins;

	int			ofsFrames;
	int			ofsTags;
	int			ofsSurfaces;
	int			ofsEnd;
} md3Header_t;

//then has header->numFrames of these at header->ofs_Frames
typedef struct md3Frame_s {
	vec3_t		bounds[2];
	vec3_t		localOrigin;
	float		radius;
	char		name[16];
} md3Frame_t;

typedef struct {
	char		name[64];
	vec3_t		origin;
	float		axis[3][3];
} md3Tag_t;

//there are header->numSurfaces of these at header->ofsSurfaces, following from ofsEnd
typedef struct {
	int		ident;				//

	char	name[64];	// polyset name

	int		flags;
	int		numFrames;			// all surfaces in a model should have the same

	int		numShaders;			// all surfaces in a model should have the same
	int		numVerts;

	int		numTriangles;
	int		ofsTriangles;

	int		ofsShaders;			// offset from start of md3Surface_t
	int		ofsSt;				// texture coords are common for all frames
	int		ofsXyzNormals;		// numVerts * numFrames

	int		ofsEnd;				// next surface follows
} md3Surface_t;

//at surf+surf->ofsXyzNormals
/*typedef struct {
	short		xyz[3];
	byte		latlong[2];
} md3XyzNormal_t;*/

//surf->numTriangles at surf+surf->ofsTriangles
typedef struct {
	int			indexes[3];
} md3Triangle_t;

//surf->numVerts at surf+surf->ofsSt
typedef struct {
	float		s;
	float		t;
} md3St_t;

typedef struct {
	char			name[64];
	int				shaderIndex;
} md3Shader_t;

static qboolean Mod_MD3Range (size_t base, size_t limit, int offset, size_t count, size_t size)
{
	size_t start, bytes;

	if (base > limit || offset < 0 || (size_t)offset > limit - base)
		return false;
	start = base + (size_t)offset;
	if (size && count > SIZE_MAX / size)
		return false;
	bytes = count * size;
	return bytes <= limit - start;
}

static qboolean Mod_MD3Multiply (size_t left, size_t right, size_t *result)
{
	if (left && right > SIZE_MAX / left)
		return false;
	*result = left * right;
	return true;
}

static qboolean Mod_MD3Invalid (qmodel_t *mod, int start, const char *reason)
{
	Con_Warning ("Mod_LoadMD3Model: %s: %s\n", mod->name, reason);
	mod->type = mod_ext_invalid;
	mod->flags = 0;
	Hunk_FreeToLowMark (start);
	return false;
}

static qboolean Mod_ValidateMD3Model (qmodel_t *mod, const void *buffer, size_t file_size, int start)
{
	const byte *base = (const byte *)buffer;
	const md3Header_t *header;
	const md3Surface_t *surface;
	size_t md3_end, surface_offset, surface_end, tag_count;
	size_t vertex_count;
	int numframes, numtags, numsurfs, surf;

	if (file_size < sizeof(md3Header_t))
		return Mod_MD3Invalid (mod, start, "file is smaller than its header");

	header = (const md3Header_t *)buffer;
	if (LittleLong(header->ident) != (('I'<<0)|('D'<<8)|('P'<<16)|('3'<<24)))
		return Mod_MD3Invalid (mod, start, "invalid ident");
	if (LittleLong(header->version) != MD3_VERSION)
		return Mod_MD3Invalid (mod, start, "wrong version");

	numframes = LittleLong(header->numFrames);
	numtags = LittleLong(header->numTags);
	numsurfs = LittleLong(header->numSurfaces);
	if (numframes < 1 || numframes > MAXALIASFRAMES)
		return Mod_MD3Invalid (mod, start, "invalid frame count");
	if (numtags < 0)
		return Mod_MD3Invalid (mod, start, "invalid tag count");
	if (numsurfs < 1)
		return Mod_MD3Invalid (mod, start, "no surfaces");

	if (LittleLong(header->ofsEnd) < (int)sizeof(md3Header_t) ||
		(size_t)LittleLong(header->ofsEnd) > file_size)
		return Mod_MD3Invalid (mod, start, "invalid file end offset");
	md3_end = (size_t)LittleLong(header->ofsEnd);
	if (LittleLong(header->ofsFrames) < (int)sizeof(md3Header_t) ||
		LittleLong(header->ofsSurfaces) < (int)sizeof(md3Header_t) ||
		(numtags && LittleLong(header->ofsTags) < (int)sizeof(md3Header_t)))
		return Mod_MD3Invalid (mod, start, "header data offset is inside the header");

	if (!Mod_MD3Range (0, md3_end, LittleLong(header->ofsFrames),
		(size_t)numframes, sizeof(md3Frame_t)))
		return Mod_MD3Invalid (mod, start, "frame range is outside the file");
	if (numtags &&
		(!Mod_MD3Multiply ((size_t)numtags, (size_t)numframes, &tag_count) ||
		 !Mod_MD3Range (0, md3_end, LittleLong(header->ofsTags),
		 tag_count, sizeof(md3Tag_t))))
		return Mod_MD3Invalid (mod, start, "tag range is outside the file");

	surface_offset = (size_t)LittleLong(header->ofsSurfaces);
	if (surface_offset >= md3_end)
		return Mod_MD3Invalid (mod, start, "surface offset is outside the file");

	for (surf = 0; surf < numsurfs; surf++)
	{
		if (!Mod_MD3Range (surface_offset, md3_end, 0, 1, sizeof(md3Surface_t)))
			return Mod_MD3Invalid (mod, start, "surface header is outside the file");

		surface = (const md3Surface_t *)(base + surface_offset);
		if (LittleLong(surface->ident) != (('I'<<0)|('D'<<8)|('P'<<16)|('3'<<24)))
			return Mod_MD3Invalid (mod, start, "corrupt surface ident");
		if (LittleLong(surface->numFrames) != numframes)
			return Mod_MD3Invalid (mod, start, "mismatched surface frame count");
		if (LittleLong(surface->numShaders) < 0)
			return Mod_MD3Invalid (mod, start, "invalid shader count");
		if (LittleLong(surface->numVerts) <= 0 || LittleLong(surface->numVerts) > 0xffff)
			return Mod_MD3Invalid (mod, start, "invalid vertex count");
		if (LittleLong(surface->numTriangles) <= 0 ||
			(size_t)LittleLong(surface->numTriangles) > (size_t)INT_MAX / 3)
			return Mod_MD3Invalid (mod, start, "invalid triangle count");
		if (LittleLong(surface->ofsEnd) < (int)sizeof(md3Surface_t))
			return Mod_MD3Invalid (mod, start, "surface does not advance");

		if ((size_t)LittleLong(surface->ofsEnd) > md3_end - surface_offset)
			return Mod_MD3Invalid (mod, start, "surface end is outside the file");
		surface_end = surface_offset + (size_t)LittleLong(surface->ofsEnd);

		if (!Mod_MD3Range (surface_offset, surface_end, LittleLong(surface->ofsTriangles),
			(size_t)LittleLong(surface->numTriangles), sizeof(md3Triangle_t)) ||
			!Mod_MD3Range (surface_offset, surface_end, LittleLong(surface->ofsShaders),
			(size_t)LittleLong(surface->numShaders), sizeof(md3Shader_t)) ||
			!Mod_MD3Range (surface_offset, surface_end, LittleLong(surface->ofsSt),
			(size_t)LittleLong(surface->numVerts), sizeof(md3St_t)))
			return Mod_MD3Invalid (mod, start, "surface data range is outside the surface");

		if (!Mod_MD3Multiply ((size_t)LittleLong(surface->numFrames),
			(size_t)LittleLong(surface->numVerts), &vertex_count) ||
			!Mod_MD3Range (surface_offset, surface_end, LittleLong(surface->ofsXyzNormals),
			vertex_count, sizeof(md3XyzNormal_t)))
			return Mod_MD3Invalid (mod, start, "vertex range is outside the surface");

		for (int triangle_index = 0; triangle_index < LittleLong(surface->numTriangles); triangle_index++)
		{
			const md3Triangle_t *triangle = (const md3Triangle_t *)
				(base + surface_offset + (size_t)LittleLong(surface->ofsTriangles) +
				 (size_t)triangle_index * sizeof(md3Triangle_t));
			for (int vertex_index = 0; vertex_index < 3; vertex_index++)
			{
				int index = LittleLong(triangle->indexes[vertex_index]);
				if (index < 0 || index >= LittleLong(surface->numVerts))
					return Mod_MD3Invalid (mod, start, "triangle index is outside the surface");
			}
		}

		surface_offset = surface_end;
	}

	/* Some exporters leave padding between the final surface and ofsEnd. */
	if (surface_offset > md3_end)
		return Mod_MD3Invalid (mod, start, "surface list exceeds file end");

	return true;
}

qboolean Mod_GetShaderNameForSurface(const char *skinfile, const char *surfacename, char *texturename, size_t texturenamesize)
{
	const char *linestart = skinfile;
//lots of `surfacename,shadername` pair lines.
	while (*skinfile)
	{
		if (*skinfile == ',')
		{
			while (*linestart == ' ' || *linestart == '\t')
				linestart++;
			if (skinfile - linestart == strlen(surfacename))
				if (!strncmp(linestart, surfacename, skinfile-linestart))
				{	//okay, this is the one we're after.
					skinfile++;
					while(*skinfile == ' ' || *skinfile == '\t')
						skinfile++;
					linestart = skinfile;
					while (*skinfile && *skinfile != '\n')
						skinfile++;
					while (skinfile > linestart && (skinfile[-1] == '\r' || skinfile[-1] == ' ' || skinfile[-1] == '\t'))
						skinfile--;

					if ((size_t)(skinfile - linestart) >= texturenamesize)
						return false;	//too long.
					memcpy(texturename, linestart, skinfile-linestart);
					texturename[skinfile-linestart] = 0;
					return true;
				}

			//walk to the end of the line.
			while (*skinfile && *skinfile != '\n')
				skinfile++;
		}
		else if (*skinfile++ == '\n')
			linestart = skinfile;
	}
	return false;
}

qboolean Mod_LoadMD3Model (qmodel_t *mod, void *buffer, size_t file_size)
{
	md3Header_t			*pinheader;
	md3Surface_t		*pinsurface;
	md3Frame_t			*pinframes;
	md3Triangle_t		*pintriangle;
	unsigned short		*poutindexes;
	md3XyzNormal_t		*pinvert;
	md3XyzNormal_t		*poutvert;
	md3St_t				*pinst;
	aliasmesh_t			*poutst;
	md3Shader_t			*pinshader;
	int					size;
	int					start, end, total;
	int					ival, j;
	int					numsurfs, surf;
	int					numframes;
	aliashdr_t			*outhdr;

	char				*skinfile[countof(outhdr->textures)];
	unsigned int		numskinfiles;

	start = Hunk_LowMark ();

	pinheader = (md3Header_t *)buffer;
	if (!Mod_ValidateMD3Model (mod, buffer, file_size, start))
		return false;

	numsurfs = LittleLong (pinheader->numSurfaces);
	numframes = LittleLong(pinheader->numFrames);

	if (numframes < 1)
		Sys_Error ("%s has no frames", diskname);
	if (numframes > MAXALIASFRAMES)
		Sys_Error ("%s has too many frames (%i vs %i)",
				 diskname, numframes, MAXALIASFRAMES);
	if (numsurfs < 1)
		Sys_Error ("%s has nosurfaces", diskname);

	pinframes = (md3Frame_t*)((byte*)buffer + LittleLong(pinheader->ofsFrames));
//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	if ((size_t)(numframes - 1) > ((size_t)INT_MAX - sizeof(aliashdr_t)) / sizeof(outhdr->frames[0]))
		Sys_Error ("%s header too large", diskname);
	size	= sizeof(aliashdr_t) + (numframes-1) * sizeof (outhdr->frames[0]);
	if ((size_t)numsurfs > (size_t)INT_MAX / (size_t)size)
		Sys_Error ("%s header too large", diskname);
	outhdr = (aliashdr_t *) Hunk_AllocName (size * numsurfs, loadname);

	for (numskinfiles = 0; numskinfiles < countof(skinfile); numskinfiles++)
	{	//q3 doesn't really support multiple skins any other way. plus this lets us fix up some texture paths...
		skinfile[numskinfiles] = (char*)COM_LoadMallocFile(va("%s_%i.skin", diskname, numskinfiles), NULL);
		if (!skinfile[numskinfiles])
			break;
	}

	for (surf = 0, pinsurface = (md3Surface_t*)((byte*)buffer + LittleLong(pinheader->ofsSurfaces)); surf < numsurfs; surf++, pinsurface = (md3Surface_t*)((byte*)pinsurface + LittleLong(pinsurface->ofsEnd)))
	{
		aliashdr_t	*osurf = (aliashdr_t*)((byte*)outhdr + size*surf);
		if (LittleLong(pinsurface->ident) != (('I'<<0)|('D'<<8)|('P'<<16)|('3'<<24)))
			Sys_Error ("%s corrupt surface ident", diskname);
		if (LittleLong(pinsurface->numFrames) != numframes)
			Sys_Error ("%s mismatched framecounts", diskname);
		if (LittleLong(pinsurface->ofsEnd) <= 0)
			Sys_Error ("%s corrupt surface size", diskname);

		if (surf+1 < numsurfs)
			osurf->nextsurface = size;
		else
			osurf->nextsurface = 0;
		
		osurf->poseverttype = PV_QUAKE3;
		osurf->numverts_vbo = osurf->numverts = LittleLong(pinsurface->numVerts);
		if (osurf->numverts <= 0 || osurf->numverts > 0xffff)
			Sys_Error ("%s has invalid vertex count (%i)", diskname, osurf->numverts);
		pinvert = (md3XyzNormal_t*)((byte*)pinsurface + LittleLong(pinsurface->ofsXyzNormals));
		poutvert = (md3XyzNormal_t *) Hunk_Alloc (GLMesh_CheckedHunkSize ((size_t)numframes * (size_t)osurf->numverts, sizeof(*poutvert), "Mod_LoadMD3Model"));
		osurf->vertexes = (byte *)poutvert - (byte *)osurf;
		for (ival = 0; ival < numframes; ival++)
		{
			osurf->frames[ival].firstpose = ival;
			osurf->frames[ival].numposes = 1;
			osurf->frames[ival].interval = 0.1;

			q_strlcpy(osurf->frames[ival].name, pinframes->name, sizeof(osurf->frames[ival].name));
			for (j = 0; j < 3; j++)
			{	//fixme...
				osurf->frames[ival].bboxmin.v[j] = 0;
				osurf->frames[ival].bboxmax.v[j] = 255;
			}

			for (j=0 ; j<osurf->numverts ; j++)
				poutvert[j] = pinvert[j];
			poutvert += osurf->numverts;
			pinvert += osurf->numverts;
		}
		osurf->nummorphposes = osurf->numframes = numframes;

		osurf->numtris = LittleLong(pinsurface->numTriangles);
		if (osurf->numtris <= 0 || (size_t)osurf->numtris > (size_t)INT_MAX / 3)
			Sys_Error ("%s has invalid triangle count (%i)", diskname, osurf->numtris);
		osurf->numindexes = osurf->numtris*3;
		pintriangle = (md3Triangle_t*)((byte*)pinsurface + LittleLong(pinsurface->ofsTriangles));
		poutindexes = (unsigned short *) Hunk_Alloc (GLMesh_CheckedHunkSize ((size_t)osurf->numindexes, sizeof (*poutindexes), "Mod_LoadMD3Model"));
		osurf->indexes = (intptr_t) poutindexes - (intptr_t) osurf;
		for (ival = 0; ival < osurf->numtris; ival++, pintriangle++, poutindexes+=3)
		{
			for (j = 0; j < 3; j++)
			{
				int index = LittleLong(pintriangle->indexes[j]);
				if (index < 0 || index >= osurf->numverts)
					Sys_Error ("%s has invalid triangle index (%i)", diskname, index);
				poutindexes[j] = (unsigned short)index;
			}
		}

		for (j = 0; j < 3; j++)
		{
			osurf->scale_origin[j] = 0;
			osurf->scale[j] = 1/64.0;
		}

		//guess at skin sizes
		osurf->skinwidth = 320;
		osurf->skinheight = 200;

		//load the textures
		if (!isDedicated)
		{
			int numshaders = LittleLong(pinsurface->numShaders);
			if (numshaders < 0)
				numshaders = 0;
			pinshader = (md3Shader_t*)((byte*)pinsurface + LittleLong(pinsurface->ofsShaders));
			if (numskinfiles)
				osurf->numskins = numskinfiles;
			else
				osurf->numskins = q_max(1, q_min((int)countof(osurf->textures), numshaders));
			for (j = 0; j < osurf->numskins; j++, pinshader++)
			{
				char texturename[MAX_QPATH];
				char fullbrightname[MAX_QPATH];
				char uppername[MAX_QPATH];
				char lowername[MAX_QPATH];
				char *ext;
				if (j >= numshaders)
				{	//invalid skin index for the model... not a major issue I guess
					q_strlcpy(texturename, diskname, sizeof(texturename));
					*(char*)COM_SkipPath(texturename) = 0;
					//and concat the specified name
					q_strlcat(texturename, pinsurface->name, sizeof(texturename));
				}
				else
				{
					//texture names in md3s are kinda fucked. they could be just names relative to the mdl, or full paths, or just simple shader names.
					//our texture manager is too lame to scan all 1000 possibilities
					if (strchr(pinshader->name, '/') || strchr(pinshader->name, '\\'))
					{	//so if there's a path then we want to use that.
						q_strlcpy(texturename, pinshader->name, sizeof(texturename));
					}
					else
					{	//and if there's no path then we want to prefix it with our own.
						q_strlcpy(texturename, diskname, sizeof(texturename));
						*(char*)COM_SkipPath(texturename) = 0;
						//and concat the specified name
						q_strlcat(texturename, pinshader->name, sizeof(texturename));
					}
				}
				if ((unsigned int)j < numskinfiles)
					Mod_GetShaderNameForSurface(skinfile[j], pinsurface->name, texturename, sizeof(texturename));	//swap it out for the skinned texture..
				//and make sure there's no extensions. these get ignored in q3, which is kinda annoying, but this is an md3 and standards are standards (and it makes luma easier).
				ext = (char*)COM_FileGetExtension(texturename);
				if (*ext)
					*--ext = 0;
				q_snprintf(uppername, sizeof(uppername), "%s_shirt", texturename);
				q_snprintf(lowername, sizeof(lowername), "%s_pants", texturename);
				osurf->textures[j][0].base = TexMgr_LoadImage(mod, texturename, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, texturename, 0, TEXPREF_PAD|TEXPREF_ALPHA|TEXPREF_NOBRIGHT|TEXPREF_MIPMAP);
				osurf->textures[j][0].lower = TexMgr_LoadImage(mod, lowername, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, lowername, 0, TEXPREF_ALLOWMISSING|TEXPREF_PAD|TEXPREF_MIPMAP);
				osurf->textures[j][0].upper = TexMgr_LoadImage(mod, uppername, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, uppername, 0, TEXPREF_ALLOWMISSING|TEXPREF_PAD|TEXPREF_MIPMAP);
				q_snprintf(fullbrightname, sizeof(fullbrightname), "%s_glow", texturename); // woods
				osurf->textures[j][0].luma = TexMgr_LoadImage(mod, fullbrightname, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, fullbrightname, 0, TEXPREF_ALLOWMISSING|TEXPREF_PAD|TEXPREF_ALPHA|TEXPREF_FULLBRIGHT|TEXPREF_MIPMAP);
				if (!osurf->textures[j][0].luma) // woods
				{
					q_snprintf(fullbrightname, sizeof(fullbrightname), "%s_luma", texturename);
					osurf->textures[j][0].luma = TexMgr_LoadImage(mod, fullbrightname, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, fullbrightname, 0, TEXPREF_ALLOWMISSING|TEXPREF_PAD|TEXPREF_ALPHA|TEXPREF_FULLBRIGHT|TEXPREF_MIPMAP);
				}
				osurf->textures[j][3] = osurf->textures[j][2] = osurf->textures[j][1] = osurf->textures[j][0];
			}
			if (osurf->numskins)
			{
				osurf->skinwidth = osurf->textures[0][0].base->source_width;
				osurf->skinheight = osurf->textures[0][0].base->source_height;
			}
		}

		//and figure out the texture coords properly, now we know the actual sizes.
		pinst = (md3St_t*)((byte*)pinsurface + LittleLong(pinsurface->ofsSt));
		poutst = (aliasmesh_t *) Hunk_Alloc (GLMesh_CheckedHunkSize ((size_t)osurf->numverts, sizeof (*poutst), "Mod_LoadMD3Model"));
		osurf->meshdesc = (intptr_t) poutst - (intptr_t) osurf;
		for (j = 0; j < osurf->numverts; j++)
		{
			poutst[j].vertindex = j;	//how is this useful?
			poutst[j].st[0] = pinst[j].s;
			poutst[j].st[1] = pinst[j].t;
		}
	}
	for (j = 0; (unsigned int)j < numskinfiles; j++)
		free (skinfile[j]);

//
// make sure pointer-relative alias data stayed inside one hunk segment before
// any consumer walks those offsets.
//
	end = Hunk_LowMark ();
	total = end - start;

	if (!Hunk_IsContiguous (start, end))
		Sys_Error ("Mod_LoadMD3Model: %s spans multiple hunk segments (try a larger -heapsize)", mod->name);

	GLMesh_LoadVertexBuffer (mod, outhdr);

	//small violation of the spec, but it seems like noone else uses it.
	mod->flags = LittleLong (pinheader->flags);
	if (!strcmp(mod->name, "progs/teleport.md3"))
		mod->flags = 0;


	mod->type = mod_alias;

	Mod_CalcAliasBounds (outhdr); //johnfitz

//
// move the complete, relocatable alias model to the cache
//
	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return true;
	memcpy (mod->cache.data, outhdr, total);

	Hunk_FreeToLowMark (start);
	return true;
}

/*
=================================================================
InterQuake Models.
=================================================================

Header:
*/
//Copyright (c) 2010-2019 Lee Salzman
//MIT License etc at: https://github.com/lsalzman/iqm
#define IQM_MAGIC "INTERQUAKEMODEL"
#define IQM_VERSION 2

struct iqmheader
{
    char magic[16];
    unsigned int version;
    unsigned int filesize;
    unsigned int flags;
    unsigned int num_text, ofs_text;			//text strings
    unsigned int num_meshes, ofs_meshes;		//surface info
    unsigned int num_vertexarrays, num_vertexes, ofs_vertexarrays;	//for loading vertex data
    unsigned int num_triangles, ofs_triangles, ofs_adjacency;	//the index data+neighbours(which we ignore)
    unsigned int num_joints, ofs_joints;		//mesh joints (base pose info)
    unsigned int num_poses, ofs_poses;			//animated joints (num_poses should match num_joints)
    unsigned int num_anims, ofs_anims;			//animations info
    unsigned int num_frames, num_framechannels, ofs_frames, ofs_bounds; //the actual per-pose(aka:single-frame) data
    unsigned int num_comment, ofs_comment;		//extra stuff
    unsigned int num_extensions, ofs_extensions;//extra stuff
};

struct iqmmesh
{
    unsigned int name;
    unsigned int material;
    unsigned int first_vertex, num_vertexes;
    unsigned int first_triangle, num_triangles;
};

enum
{
    IQM_POSITION     = 0,
    IQM_TEXCOORD     = 1,
    IQM_NORMAL       = 2,
    IQM_TANGENT      = 3,
    IQM_BLENDINDEXES = 4,
    IQM_BLENDWEIGHTS = 5,
    IQM_COLOR        = 6,
    IQM_CUSTOM       = 0x10
};

enum
{
    IQM_BYTE   = 0,
    IQM_UBYTE  = 1,
    IQM_SHORT  = 2,
    IQM_USHORT = 3,
    IQM_INT    = 4,
    IQM_UINT   = 5,
    IQM_HALF   = 6,
    IQM_FLOAT  = 7,
    IQM_DOUBLE = 8
};

/*struct iqmtriangle
{
    unsigned int vertex[3];
};

struct iqmadjacency
{
    unsigned int triangle[3];
};

struct iqmjointv1
{
    unsigned int name;
    int parent;
    float translate[3], rotate[3], scale[3];
};*/

struct iqmjoint
{
    unsigned int name;
    int parent;
    float translate[3], rotate[4], scale[3];
};

/*struct iqmposev1
{
    int parent;
    unsigned int mask;
    float channeloffset[9];
    float channelscale[9];
};*/

struct iqmpose
{
    int parent;
    unsigned int mask;
    float channeloffset[10];
    float channelscale[10];
};

struct iqmanim
{
    unsigned int name;
    unsigned int first_frame, num_frames;
    float framerate;
    unsigned int flags;
};

enum
{
    IQM_LOOP = 1<<0
};

struct iqmvertexarray
{
    unsigned int type;
    unsigned int flags;
    unsigned int format;
    unsigned int size;
    unsigned int offset;
};

/*struct iqmbounds
{
    float bbmin[3], bbmax[3];
    float xyradius, radius;
};*/

struct iqmextension
{
    unsigned int name;
    unsigned int num_data, ofs_data;
    unsigned int ofs_extensions; // pointer to next extension
};

//skin lump is made of 3 parts
struct iqmext_fte_skin
{
	unsigned int numskinframes;
	unsigned int nummeshskins;
	//unsigned int numskins[nummeshes];
	//iqmext_fte_skin_skinframe[numskinframes];
	//iqmext_fte_skin_meshskin mesh0[numskins[0]];
	//iqmext_fte_skin_meshskin mesh1[numskins[1]]; etc
};
struct iqmext_fte_skin_skinframe
{	//as many as needed
	unsigned int material_idx;
	unsigned int shadertext_idx;
};
struct iqmext_fte_skin_meshskin
{
	unsigned int firstframe;	//index into skinframes
	unsigned int countframes;	//skinframes
	float interval;
};

//IQM Implementation: Copyright 2019 spike, licensed like the rest of quakespasm.
static void IQM_LoadVertexes_Float(float *o, size_t c, size_t numverts, const byte *buffer, const struct iqmvertexarray *va)
{
	size_t j, k;
	if (c != va->size)
		return;	//erk, too lazy to handle weirdness.
	switch(va->format)
	{
//	case IQM_BYTE:
	case IQM_UBYTE:
		{	//weights+colours are often normalised bytes.
			const byte *in = (const byte*)(buffer+va->offset);
			for (j = 0; j < numverts; j++, in+=va->size, o+=sizeof(iqmvert_t)/sizeof(*o))
			{
				for (k = 0; k < c; k++)
					o[k] = in[k]/255.0;
			}
		}
		break;
//	case IQM_SHORT:
//	case IQM_USHORT:
//	case IQM_INT:
//	case IQM_UINT:
//	case IQM_HALF:
	case IQM_FLOAT:
		{
			const float *in = (const float*)(buffer+va->offset);
			for (j = 0; j < numverts; j++, in+=va->size, o+=sizeof(iqmvert_t)/sizeof(*o))
			{
				for (k = 0; k < c; k++)
					o[k] = in[k];
			}
		}
		break;
	case IQM_DOUBLE:
		{	//truncate, sorry...
			const double *in = (const double*)(buffer+va->offset);
			for (j = 0; j < numverts; j++, in+=va->size, o+=sizeof(iqmvert_t)/sizeof(*o))
			{
				for (k = 0; k < c; k++)
					o[k] = in[k];
			}
		}
		break;
	default:
		return;	//oh bum. my laziness strikes again.
	}
}

static void IQM_LoadVertexes_Index(byte *o, size_t c, size_t numverts, const byte *buffer, const struct iqmvertexarray *va)
{
	size_t j, k;
	if (c != va->size)
		return;	//erk, too lazy to handle weirdness.
	switch(va->format)
	{
//	case IQM_BYTE:
	case IQM_UBYTE:
		{
			const byte *in = (const byte*)(buffer+va->offset);
			for (j = 0; j < numverts; j++, in+=va->size, o+=sizeof(iqmvert_t)/sizeof(*o))
			{
				for (k = 0; k < c; k++)
					o[k] = in[k];
			}
		}
		break;
//	case IQM_SHORT:
	case IQM_USHORT:
		{	//truncate...
			const unsigned short *in = (const unsigned short*)(buffer+va->offset);
			for (j = 0; j < numverts; j++, in+=va->size, o+=sizeof(iqmvert_t)/sizeof(*o))
			{
				for (k = 0; k < c; k++)
					o[k] = in[k];
			}
		}
		break;
//	case IQM_INT:
	case IQM_UINT:
		{	//truncate... noesis likes writing these.
			const unsigned int *in = (const unsigned int*)(buffer+va->offset);
			for (j = 0; j < numverts; j++, in+=va->size, o+=sizeof(iqmvert_t)/sizeof(*o))
			{
				for (k = 0; k < c; k++)
					o[k] = in[k];
			}
		}
		break;
//	case IQM_HALF:
//	case IQM_FLOAT:
//	case IQM_DOUBLE:
	default:
		return;	//oh bum. my laziness strikes again.
	}
}

static void GenMatrixPosQuat4Scale(const vec3_t pos, const vec4_t quat, const vec3_t scale, float result[12])
{
	float xx, xy, xz, xw, yy, yz, yw, zz, zw;
	float x2, y2, z2;
	float s;
	x2 = quat[0] + quat[0];
	y2 = quat[1] + quat[1];
	z2 = quat[2] + quat[2];

	xx = quat[0] * x2;   xy = quat[0] * y2;   xz = quat[0] * z2;
	yy = quat[1] * y2;   yz = quat[1] * z2;   zz = quat[2] * z2;
	xw = quat[3] * x2;   yw = quat[3] * y2;   zw = quat[3] * z2;

	s = scale[0];
	result[0*4+0] = s*(1.0f - (yy + zz));
	result[1*4+0] = s*(xy + zw);
	result[2*4+0] = s*(xz - yw);

	s = scale[1];
	result[0*4+1] = s*(xy - zw);
	result[1*4+1] = s*(1.0f - (xx + zz));
	result[2*4+1] = s*(yz + xw);

	s = scale[2];
	result[0*4+2] = s*(xz + yw);
	result[1*4+2] = s*(yz - xw);
	result[2*4+2] = s*(1.0f - (xx + yy));

	result[0*4+3]  =     pos[0];
	result[1*4+3]  =     pos[1];
	result[2*4+3]  =     pos[2];
}
static void Matrix3x4_Invert_Simple (const float *in1, float *out)
{
	// we only support uniform scaling, so assume the first row is enough
	// (note the lack of sqrt here, because we're trying to undo the scaling,
	// this means multiplying by the inverse scale twice - squaring it, which
	// makes the sqrt a waste of time)
#if 1
	double scale = 1.0 / (in1[0] * in1[0] + in1[1] * in1[1] + in1[2] * in1[2]);
#else
	double scale = 3.0 / sqrt
		 (in1->m[0][0] * in1->m[0][0] + in1->m[0][1] * in1->m[0][1] + in1->m[0][2] * in1->m[0][2]
		+ in1->m[1][0] * in1->m[1][0] + in1->m[1][1] * in1->m[1][1] + in1->m[1][2] * in1->m[1][2]
		+ in1->m[2][0] * in1->m[2][0] + in1->m[2][1] * in1->m[2][1] + in1->m[2][2] * in1->m[2][2]);
	scale *= scale;
#endif

	// invert the rotation by transposing and multiplying by the squared
	// recipricol of the input matrix scale as described above
	out[0] = in1[0] * scale;
	out[1] = in1[4] * scale;
	out[2] = in1[8] * scale;
	out[4] = in1[1] * scale;
	out[5] = in1[5] * scale;
	out[6] = in1[9] * scale;
	out[8] = in1[2] * scale;
	out[9] = in1[6] * scale;
	out[10] = in1[10] * scale;

	// invert the translate
	out[3] = -(in1[3] * out[0] + in1[7] * out[1] + in1[11] * out[2]);
	out[7] = -(in1[3] * out[4] + in1[7] * out[5] + in1[11] * out[6]);
	out[11] = -(in1[3] * out[8] + in1[7] * out[9] + in1[11] * out[10]);
}


static const void *IQM_FindExtension(const char *buffer, size_t buffersize, const char *extname, int index, size_t *extsize)
{
	const struct iqmheader *h = (const struct iqmheader *)buffer;
	const char *strings = buffer + h->ofs_text;
	const struct iqmextension *ext;
	int i;
	for (i = 0, ext = (const struct iqmextension*)(buffer + h->ofs_extensions); (unsigned int)i < h->num_extensions; i++, ext = (const struct iqmextension*)(buffer + ext->ofs_extensions))
	{
		if ((const char*)ext > buffer+buffersize || ext->name > h->num_text || ext->ofs_data+ext->num_data>buffersize)
			break;
		if (!q_strcasecmp(strings + ext->name, extname) && index-->=0)
		{
			*extsize = ext->num_data;
			return buffer + ext->ofs_data;
		}
	}
	*extsize = 0;
	return NULL;
}
static void Mod_LoadIQMSkin (qmodel_t *mod, const struct iqmheader	*pinheader, aliashdr_t *osurf, unsigned int meshidx, unsigned int nummeshes, const char *fallback)
{
	unsigned int j, k;
	size_t extsize;
	const struct iqmext_fte_skin *iqmext =  IQM_FindExtension((const char *)pinheader, pinheader->filesize, "FTE_SKINS", 0, &extsize);
	if (iqmext)
	{
		const struct iqmext_fte_skin_skinframe *skinframe = (const struct iqmext_fte_skin_skinframe*)((const unsigned int*)(iqmext+1) + nummeshes), *sf;
		const struct iqmext_fte_skin_meshskin *skin = (const struct iqmext_fte_skin_meshskin*)(skinframe+iqmext->numskinframes);
		osurf->numskins = ((const unsigned int*)(iqmext+1))[meshidx];

		for (j = 0; j < meshidx; j++)
			skin += ((const unsigned int*)(iqmext+1))[j];
		for (j = 0; j < (unsigned int)osurf->numskins && j < (unsigned int)MAX_SKINS; j++, skin++)
		{
			if (!skin->countframes)
				break;	//doesn't make sense.
			if (skin->firstframe+skin->countframes>iqmext->numskinframes)
				break;	//some kind of error
			sf = skinframe+skin->firstframe;
			for (k = 0; k < skin->countframes && k < 4; k++, sf++)
			{
				const char *texturename = (const char *)pinheader + pinheader->ofs_text + sf->material_idx;
				char hackytexturename[MAX_QPATH];
				char filename2[MAX_QPATH];
				COM_StripExtension(texturename, hackytexturename, sizeof(hackytexturename));
				osurf->textures[j][k].base = TexMgr_LoadImage(mod, texturename, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, hackytexturename, 0, TEXPREF_PAD|TEXPREF_ALPHA|TEXPREF_NOBRIGHT|TEXPREF_MIPMAP);
				q_snprintf(filename2, sizeof(filename2), "%s_pants", hackytexturename);
				osurf->textures[j][k].lower = TexMgr_LoadImage(mod, filename2, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, filename2, 0, TEXPREF_PAD|TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);
				q_snprintf(filename2, sizeof(filename2), "%s_shirt", hackytexturename);
				osurf->textures[j][k].upper = TexMgr_LoadImage(mod, filename2, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, filename2, 0, TEXPREF_PAD|TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);
				q_snprintf(filename2, sizeof(filename2), "%s_luma", hackytexturename);
				osurf->textures[j][k].luma = TexMgr_LoadImage(mod, filename2, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, filename2, 0, TEXPREF_PAD|TEXPREF_ALPHA|TEXPREF_FULLBRIGHT|TEXPREF_MIPMAP|TEXPREF_ALLOWMISSING);
			}
			for (; k < 4; k++)
			{
				osurf->textures[j][k] = osurf->textures[j][k%skin->countframes];
			}
		}
		osurf->numskins = j;
	}
	else
	{
		osurf->numskins = 1;
		for (j = 0; j < 1; j++)
		{
			char texturename[MAX_QPATH];
			char filename2[MAX_QPATH];
			char *ext;
			//texture names in md3s are kinda fucked. they could be just names relative to the mdl, or full paths, or just simple shader names.
			//our texture manager is too lame to scan all 1000 possibilities
			if (strchr(fallback, '/') || strchr(fallback, '\\'))
			{	//so if there's a path then we want to use that.
				q_strlcpy(texturename, fallback, sizeof(texturename));
			}
			else
			{	//and if there's no path then we want to prefix it with our own.
				q_strlcpy(texturename, mod->name, sizeof(texturename));
				*(char*)COM_SkipPath(texturename) = 0;
				//and concat the specified name
				q_strlcat(texturename, fallback, sizeof(texturename));
			}
			//and make sure there's no extensions. these get ignored in q3, which is kinda annoying, but this is an md3 and standards are standards (and it makes luma easier).
			ext = (char*)COM_FileGetExtension(texturename);
			if (*ext)
				*--ext = 0;
			//luma has an extra postfix.
			osurf->textures[j][0].base = TexMgr_LoadImage(mod, texturename, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, texturename, 0, TEXPREF_PAD|TEXPREF_ALPHA|TEXPREF_NOBRIGHT|TEXPREF_MIPMAP);
			q_snprintf(filename2, sizeof(filename2), "%s_pants", texturename);
			osurf->textures[j][0].lower = TexMgr_LoadImage(mod, filename2, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, filename2, 0, TEXPREF_PAD|TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);
			q_snprintf(filename2, sizeof(filename2), "%s_shirt", texturename);
			osurf->textures[j][0].upper = TexMgr_LoadImage(mod, filename2, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, filename2, 0, TEXPREF_PAD|TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);
			q_snprintf(filename2, sizeof(filename2), "%s_luma", texturename);
			osurf->textures[j][0].luma = TexMgr_LoadImage(mod, filename2, osurf->skinwidth, osurf->skinheight, SRC_EXTERNAL, NULL, filename2, 0, TEXPREF_PAD|TEXPREF_ALPHA|TEXPREF_FULLBRIGHT|TEXPREF_MIPMAP|TEXPREF_ALLOWMISSING);

			osurf->textures[j][3] = osurf->textures[j][2] = osurf->textures[j][1] = osurf->textures[j][0];
		}
	}
	if (osurf->numskins)
	{
		osurf->skinwidth = osurf->textures[0][0].base->source_width;
		osurf->skinheight = osurf->textures[0][0].base->source_height;
	}
}

void Mod_LoadIQMModel (qmodel_t *mod, const void *buffer)
{
	const struct iqmheader	*pinheader;
	const char				*pintext;
	const struct iqmmesh	*pinsurface;
	const struct iqmanim	*pinframes;
	const unsigned int		*pintriangle;
	unsigned short			*poutindexes;
	iqmvert_t				*poutvert;
	int						size;
	int						start, end, total;
	int						ival, j, a;
	int						numsurfs, surf;
	aliashdr_t				*outhdr;
	int						numverts, numtriangles, firstidx, firstvert;
	int						numanims, numhdrframes;

	bonepose_t				*outposes;
	boneinfo_t				*outbones;
	int						numposes, numjoints;

	start = Hunk_LowMark ();

	pinheader = (const struct iqmheader *)buffer;


	if (strcmp(pinheader->magic, IQM_MAGIC))
		Sys_Error ("%s has invalid magic for iqm file", mod->name);
	if (LittleLong(pinheader->version) != IQM_VERSION)	//v1 is outdated.
		Sys_Error ("%s is an unsupported version, %i must be %i", mod->name, LittleLong(pinheader->version), IQM_VERSION);

	pintext = (const char*)buffer + LittleLong(pinheader->ofs_text);

	numsurfs = LittleLong (pinheader->num_meshes);
	if (numsurfs < 1)
		Sys_Error ("%s has no surfaces (animation-only iqms are not supported)", mod->name);

	numverts = LittleLong(pinheader->num_vertexes);
	if (numverts < 0 || numverts > 0xffff)	//indexes is an unsigned short.
		Sys_Error ("%s has too many verts (%i>%u)", mod->name, numverts, 0xffffu);
	numtriangles = LittleLong(pinheader->num_triangles);
	if (numtriangles < 0)
		Sys_Error ("%s has invalid triangle count (%i)", mod->name, numtriangles);
	numanims = LittleLong (pinheader->num_anims);
	if (numanims < 0 || numanims > MAXALIASFRAMES)
		Sys_Error ("%s has invalid animation count (%i)", mod->name, numanims);
	numhdrframes = q_max(1, numanims);
	if ((size_t)(numhdrframes - 1) > ((size_t)INT_MAX - sizeof(aliashdr_t)) / sizeof(outhdr->frames[0]))
		Sys_Error ("%s header too large", mod->name);
	size	= sizeof(aliashdr_t) + (numhdrframes - 1) * sizeof (outhdr->frames[0]);
	if ((size_t)numsurfs > (size_t)INT_MAX / (size_t)size)
		Sys_Error ("%s header too large", mod->name);
	outhdr = (aliashdr_t *) Hunk_AllocName (size * numsurfs, loadname);

	poutvert = (iqmvert_t *) Hunk_Alloc (GLMesh_CheckedHunkSize ((size_t)numverts, sizeof (*poutvert), "Mod_LoadIQMModel"));
	for (j = 0; j < numverts; j++)	//initialise verts, just in case.
		poutvert[j].rgba[0] = poutvert[j].rgba[1] = poutvert[j].rgba[2] = poutvert[j].rgba[3] = poutvert[j].weight[0] = 1;
	for (a = 0; a < LittleLong(pinheader->num_vertexarrays); a++)
	{
		const struct iqmvertexarray *va = (const struct iqmvertexarray*)((const byte*)buffer+LittleLong(pinheader->ofs_vertexarrays)) + a;
		switch(va->type)
		{
		case IQM_POSITION:		IQM_LoadVertexes_Float(poutvert->xyz,	3, numverts, buffer, va); break;
		case IQM_TEXCOORD:		IQM_LoadVertexes_Float(poutvert->st,	2, numverts, buffer, va); break;
		case IQM_NORMAL:		IQM_LoadVertexes_Float(poutvert->norm,	3, numverts, buffer, va); break;
		//case IQM_TANGENT:		IQM_LoadVertexes_Float(poutvert->tang,	4, numverts, buffer, va); break; //bitangent must be calced using a crossproduct and the fourth component (for direction). we don't need this (unless you want rtlights or bumpmaps)
		case IQM_COLOR:			IQM_LoadVertexes_Float(poutvert->rgba,	4, numverts, buffer, va); break;
		case IQM_BLENDINDEXES:	IQM_LoadVertexes_Index(poutvert->idx,	4, numverts, buffer, va); break;
		case IQM_BLENDWEIGHTS:	IQM_LoadVertexes_Float(poutvert->weight,4, numverts, buffer, va); break;
		default:
			continue; //no idea what it is. probably custom
		}
	}

	numposes = LittleLong (pinheader->num_frames);
	numjoints = LittleLong(pinheader->num_poses);
	if (numposes < 0 || numjoints < 0)
		Sys_Error ("%s has invalid skeletal counts", mod->name);
	if (numjoints > 256)
		Sys_Error ("%s has too many joints (%i > 256)", mod->name, numjoints);
	if (numjoints == LittleLong(pinheader->num_joints))
	{
		const unsigned short	*pinframedata = (const unsigned short*)((const byte*)buffer + LittleLong(pinheader->ofs_frames));
		const struct iqmpose	*pinajoint = (const struct iqmpose*)((const byte*)buffer + LittleLong(pinheader->ofs_poses)), *p;
		vec3_t pos, scale;
		vec4_t quat;
		outposes = Hunk_Alloc(GLMesh_CheckedHunkSize ((size_t)numposes * (size_t)numjoints, sizeof(*outposes), "Mod_LoadIQMModel"));
		for (a = 0; a < numposes; a++)
		{
			for (j = 0, p = pinajoint; j < numjoints; j++, p++)
			{
				unsigned int mask = LittleLong(p->mask);
				pos[0]   = LittleFloat(p->channeloffset[0]); if (mask &   1) pos[0]   += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[0]);
				pos[1]   = LittleFloat(p->channeloffset[1]); if (mask &   2) pos[1]   += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[1]);
				pos[2]   = LittleFloat(p->channeloffset[2]); if (mask &   4) pos[2]   += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[2]);
				quat[0]  = LittleFloat(p->channeloffset[3]); if (mask &   8) quat[0]  += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[3]);
				quat[1]  = LittleFloat(p->channeloffset[4]); if (mask &  16) quat[1]  += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[4]);
				quat[2]  = LittleFloat(p->channeloffset[5]); if (mask &  32) quat[2]  += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[5]);
				quat[3]  = LittleFloat(p->channeloffset[6]); if (mask &  64) quat[3]  += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[6]);
				scale[0] = LittleFloat(p->channeloffset[7]); if (mask & 128) scale[0] += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[7]);
				scale[1] = LittleFloat(p->channeloffset[8]); if (mask & 256) scale[1] += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[8]);
				scale[2] = LittleFloat(p->channeloffset[9]); if (mask & 512) scale[2] += (unsigned short)LittleShort(*pinframedata++) * LittleFloat(p->channelscale[9]);

				//fixme: should probably save the 10 values above and slerp, but its simpler to just save+lerp a matrix (although this does result in denormalisation when interpolating).
				GenMatrixPosQuat4Scale(pos, quat, scale, outposes[(a*numjoints+j)].mat);
			}
		}

	}
	else
	{	//panic! panic! something weird is going on!
		numposes = 0;
		numjoints = 0;
		outposes = NULL;
	}

	{
		const struct iqmjoint	*pinbjoint = (const struct iqmjoint*)((const byte*)buffer + LittleLong(pinheader->ofs_joints));
		bonepose_t basepose[256], rel;
		vec3_t pos, scale;
		vec4_t quat;
		outbones = Hunk_Alloc(GLMesh_CheckedHunkSize ((size_t)numjoints, sizeof(*outbones), "Mod_LoadIQMModel"));
		for (j = 0; j < numjoints; j++)
		{
			outbones[j].parent = LittleLong(pinbjoint[j].parent);
			if (outbones[j].parent < -1 || outbones[j].parent >= j)
				Sys_Error ("%s has invalid joint parent (%i)", mod->name, outbones[j].parent);
			q_strlcpy(outbones[j].name, pintext+LittleLong(pinbjoint[j].name), sizeof(outbones[j].name));

			pos[0]   = LittleFloat(pinbjoint[j].translate[0]);
			pos[1]   = LittleFloat(pinbjoint[j].translate[1]);
			pos[2]   = LittleFloat(pinbjoint[j].translate[2]);
			quat[0]  = LittleFloat(pinbjoint[j].rotate[0]);
			quat[1]  = LittleFloat(pinbjoint[j].rotate[1]);
			quat[2]  = LittleFloat(pinbjoint[j].rotate[2]);
			quat[3]  = LittleFloat(pinbjoint[j].rotate[3]);
			scale[0] = LittleFloat(pinbjoint[j].scale[0]);
			scale[1] = LittleFloat(pinbjoint[j].scale[1]);
			scale[2] = LittleFloat(pinbjoint[j].scale[2]);
			GenMatrixPosQuat4Scale(pos, quat, scale, rel.mat);
			//urgh, these are relative.
			if (outbones[j].parent < 0)
				memcpy(basepose[j].mat, rel.mat, sizeof(rel.mat));
			else
				R_ConcatTransforms((void*)basepose[outbones[j].parent].mat, (void*)rel.mat, (void*)basepose[j].mat);

			Matrix3x4_Invert_Simple(basepose[j].mat, outbones[j].inverse.mat);
			//and now we have the inversion matrix to use to undo the bone positions baked into the vertex data.
		}
	}

	mod->numframes = q_max(1,numanims);

	for (surf = 0, pinsurface = (const struct iqmmesh*)((const byte*)buffer + LittleLong(pinheader->ofs_meshes)); surf < numsurfs; surf++, pinsurface++)
	{
		aliashdr_t	*osurf = (aliashdr_t*)((byte*)outhdr + size*surf);
		int firsttri;

		if (surf+1 < numsurfs)
			osurf->nextsurface = size;
		else
			osurf->nextsurface = 0;

		osurf->poseverttype = PV_IQM;
		osurf->numverts_vbo = osurf->numverts = LittleLong(pinsurface->num_vertexes);

		firstvert = LittleLong(pinsurface->first_vertex);
		if (firstvert < 0 || osurf->numverts < 0 || firstvert > numverts || osurf->numverts > numverts - firstvert)
			Sys_Error ("%s has invalid mesh vertex range", mod->name);
		osurf->vertexes = (intptr_t)(poutvert + firstvert) - (intptr_t)osurf;
		osurf->numverts = LittleLong(pinsurface->num_vertexes);
		osurf->nummorphposes = 1;	//as a skeletal model, we do all our animations via bones rather than vertex morphs.

		osurf->numtris = LittleLong(pinsurface->num_triangles);
		if (osurf->numtris < 0 || (size_t)osurf->numtris > (size_t)INT_MAX / 3)
			Sys_Error ("%s has invalid mesh triangle count (%i)", mod->name, osurf->numtris);
		osurf->numindexes = osurf->numtris*3;
		poutindexes = (unsigned short *) Hunk_Alloc (GLMesh_CheckedHunkSize ((size_t)osurf->numindexes, sizeof (*poutindexes), "Mod_LoadIQMModel"));
		osurf->indexes = (intptr_t)poutindexes - (intptr_t)osurf;
		pintriangle = (const unsigned int*)((const byte*)buffer + LittleLong(pinheader->ofs_triangles));
		firsttri = LittleLong(pinsurface->first_triangle);
		if (firsttri < 0 || firsttri > numtriangles || osurf->numtris > numtriangles - firsttri || (size_t)firsttri > (size_t)INT_MAX / 3)
			Sys_Error ("%s has invalid mesh triangle range", mod->name);
		firstidx = firsttri*3;
		pintriangle += firstidx;
		for (j = 0; j < osurf->numindexes; j++)
		{
			int index = LittleLong(pintriangle[j]);
			if (index < firstvert || index >= firstvert + osurf->numverts)
				Sys_Error ("%s has invalid triangle index (%i)", mod->name, index);
			poutindexes[j] = (unsigned short)(index - firstvert);
		}

		pinframes = (const struct iqmanim*)((const byte*)buffer + LittleLong(pinheader->ofs_anims));
		for (a = 0; a < numanims; a++, pinframes++)
		{
			osurf->frames[a].firstpose = LittleLong(pinframes->first_frame);
			osurf->frames[a].numposes = LittleLong(pinframes->num_frames);
			osurf->frames[a].interval = LittleFloat(pinframes->framerate);
			if (!osurf->frames[a].interval)
				osurf->frames[a].interval = 20;
			osurf->frames[a].interval = 1.0/osurf->frames[a].interval;
			if (LittleLong(pinframes->flags) & IQM_LOOP)
				/*FIXME*/;

			q_strlcpy(osurf->frames[a].name, pintext+LittleLong(pinframes->name), sizeof(osurf->frames[ival].name));
			for (j = 0; j < 3; j++)
			{	//fixme...
				osurf->frames[a].bboxmin.v[j] = 0;
				osurf->frames[a].bboxmax.v[j] = 255;
			}
		}
		for (; a < 1; a++, pinframes++)
		{	//unanimated models need to pick their morphpose without warnings.
			osurf->frames[a].firstpose = 0;
			osurf->frames[a].numposes = 1;
			osurf->frames[a].interval = 0.1;

			q_strlcpy(osurf->frames[a].name, "", sizeof(osurf->frames[ival].name));
			for (j = 0; j < 3; j++)
			{	//fixme...
				osurf->frames[a].bboxmin.v[j] = 0;
				osurf->frames[a].bboxmax.v[j] = 255;
			}
		}
		osurf->numframes = a;
		if (numposes)
		{
			osurf->numboneposes = numposes;
			osurf->boneposedata = (intptr_t)outposes - (intptr_t)osurf;
		}
		osurf->numbones = numjoints;
		osurf->boneinfo = (intptr_t)outbones - (intptr_t)osurf;

		for (j = 0; j < 3; j++)
		{
			osurf->scale_origin[j] = 0;
			osurf->scale[j] = 1.0;
		}

		//skin size is irrelevant
		osurf->skinwidth = 1;
		osurf->skinheight = 1;

		//load the textures
		if (!isDedicated)
			Mod_LoadIQMSkin (mod, pinheader, osurf, surf, (unsigned int)numsurfs, pintext + LittleLong(pinsurface->material));
	}

//
// make sure pointer-relative alias data stayed inside one hunk segment before
// any consumer walks those offsets.
//
	end = Hunk_LowMark ();
	total = end - start;

	if (!Hunk_IsContiguous (start, end))
		Sys_Error ("Mod_LoadIQMModel: %s spans multiple hunk segments (try a larger -heapsize)", mod->name);

	GLMesh_LoadVertexBuffer (mod, outhdr);

	//small violation of the spec, but it seems like noone else uses it.
	mod->flags = LittleLong (pinheader->flags);


	mod->synctype = ST_FRAMETIME;	//keep IQM animations synced to when .frame is changed. framegroups are otherwise not very useful.
	mod->type = mod_alias;

	Mod_CalcAliasBounds (outhdr); //johnfitz

//
// move the complete, relocatable alias model to the cache
//
	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, outhdr, total);

	Hunk_FreeToLowMark (start);
}


/*
=================================================================
MD5 Models, for compat with the rerelease and NOT doom3.
=================================================================
md5mesh:
MD5Version 10
commandline ""
numJoints N
numMeshes N
joints {
	"name" ParentIdx ( Pos_X Y Z ) ( Quat_X Y Z )
}
mesh {
	shader "name"	//file-relative path, with _%02d_%02d postfixed for skin/framegroup support. unlike doom3.
	numverts N
	vert # ( S T ) FirstWeight count
	numtris N
	tri # A B C
	numweights N
	weight # BoneIdx Scale ( X Y Z )
}

md5anim:
MD5Version 10
commandline ""
numFrames N
numJoints N
frameRate FPS
numAnimatedComponents N	//bones*6ish
hierachy {
	"name" ParentIdx Flags DataStart
}
bounds {
	( X Y Z ) ( X Y Z )
}
baseframe {
	( pos_X Y Z ) ( quad_X Y Z )
}
frame # {
	RAW ...
}

We'll unpack the animation to separate framegroups (one-pose per, for consistency with most q1 models).
*/

static qboolean MD5_ParseCheck(const char *s, const void **buffer)
{
	if (strcmp(com_token, s))
		return false;
	*buffer = COM_Parse(*buffer);
	return true;
}
static size_t MD5_ParseUInt(const void **buffer)
{
	size_t i = SDL_strtoull(com_token, NULL, 0);
	*buffer = COM_Parse(*buffer);
	return i;
}
static long MD5_ParseSInt(const void **buffer)
{
	long i = SDL_strtol(com_token, NULL, 0);
	*buffer = COM_Parse(*buffer);
	return i;
}
static double MD5_ParseFloat(const void **buffer)
{
	double i = SDL_strtod(com_token, NULL);
	*buffer = COM_Parse(*buffer);
	return i;
}
#define MD5EXPECT(s) do{if (strcmp(com_token, s)) Sys_Error ("Mod_LoadMD5MeshModel(%s): Expected \"%s\"", fname, s); buffer = COM_Parse(buffer); }while(0)
#define MD5UINT() MD5_ParseUInt(&buffer)
#define MD5SINT() MD5_ParseSInt(&buffer)
#define MD5FLOAT() MD5_ParseFloat(&buffer)
#define MD5CHECK(s) MD5_ParseCheck(s, &buffer)

struct md5vertinfo_s
{
	size_t firstweight;
	unsigned int count;
};
struct md5weightinfo_s
{
	size_t bone;
	vec4_t pos;
};

void Matrix3x4_RM_Transform4(const float *matrix, const float *vector, float *product) // woods -- remove static #routline
{
	product[0] = matrix[0]*vector[0] + matrix[1]*vector[1] + matrix[2]*vector[2] + matrix[3]*vector[3];
	product[1] = matrix[4]*vector[0] + matrix[5]*vector[1] + matrix[6]*vector[2] + matrix[7]*vector[3];
	product[2] = matrix[8]*vector[0] + matrix[9]*vector[1] + matrix[10]*vector[2] + matrix[11]*vector[3];
}
static void MD5_BakeInfluences(const char *fname, bonepose_t *outposes, iqmvert_t *vert, struct md5vertinfo_s *vinfo, struct md5weightinfo_s *weight, size_t numverts, size_t numweights)
{
	size_t v, i, lowidx, k;
	struct md5weightinfo_s *w;
	vec3_t pos;
	float lowval, scale;
	unsigned int maxinfluences = 0;
	float scaleimprecision = 1;
	for (v = 0; v < numverts; v++, vert++, vinfo++)
	{
		//st were already loaded
		//norm will need to be calculated after we have xyz info
		vert->xyz[0] = vert->xyz[1] = vert->xyz[2] = 0;
		vert->idx[0] = vert->idx[1] = vert->idx[2] = vert->idx[3] = 0;
		vert->weight[0] = vert->weight[1] = vert->weight[2] = vert->weight[3] = 0;
		vert->rgba[0] = vert->rgba[1] = vert->rgba[2] = vert->rgba[3] = 1;	//for consistency with iqm, though irrelevant here

		if (!vinfo->count)
			Sys_Error ("%s: vertex has no weights", fname);
		if (vinfo->firstweight > numweights || vinfo->count > numweights - vinfo->firstweight)
			Sys_Error ("%s: weight index out of bounds", fname);
		if (maxinfluences < vinfo->count)
			maxinfluences = vinfo->count;
		w = weight + vinfo->firstweight;
		for (i = 0; i < vinfo->count; i++, w++)
		{
			Matrix3x4_RM_Transform4(outposes[w->bone].mat, w->pos, pos);
			VectorAdd(vert->xyz, pos, vert->xyz);

			if (i < countof(vert->weight))
			{
				vert->weight[i] = w->pos[3];
				vert->idx[i] = w->bone;
			}
			else
			{
				//obnoxious code to find the lowest of the current possible bone indexes.
				lowval = vert->weight[0];
				lowidx = 0;
				for (k = 1; k < countof(vert->weight); k++)
					if (vert->weight[k] < lowval)
					{
						lowval = vert->weight[k];
						lowidx = k;
					}
				if (vert->weight[lowidx] < w->pos[3])
				{	//found a lower/unset weight, replace it.
					vert->weight[lowidx] = w->pos[3];
					vert->idx[lowidx] = w->bone;
				}
			}
		}

		//normalize in case we dropped some weights.
		scale = vert->weight[0] + vert->weight[1] + vert->weight[2] + vert->weight[3];
		if (scale>0)
		{
			if (scaleimprecision < scale)
				scaleimprecision = scale;
			scale = 1/scale;
			for (k = 0; k < countof(vert->weight); k++)
				vert->weight[k] *= scale;
		}
		else	//something bad...
			vert->weight[0] = 1, vert->weight[1] = vert->weight[2] = vert->weight[3] = 0;
	}
	if (maxinfluences > countof(vert->weight))
		Con_DWarning("%s uses up to %u influences per vertex (weakest: %g)\n", fname, maxinfluences, scaleimprecision);
}
static unsigned MD5_HashVert(const vec3_t v)
{
	return COM_HashBlock(&v[0], 3 * sizeof(float));
}

static void MD5_ComputeNormals(iqmvert_t *vert, size_t numverts, unsigned short *indexes, size_t numindexes)
{
	size_t v, t, hashsize;
	int *hashmap;
	unsigned short *weld;
	vec3_t *normals;

	for (v = 0; v < numverts; v++)
		vert[v].norm[0] = vert[v].norm[1] = vert[v].norm[2] = 0;

	if (!numverts || !numindexes)
		return;

	hashsize = numverts * 2;
	hashmap = (int *) calloc(hashsize, sizeof(*hashmap));
	weld = (unsigned short *) malloc(numverts * sizeof(*weld));
	normals = (vec3_t *) calloc(numverts, sizeof(*normals));
	if (!hashmap || !weld || !normals)
		Sys_Error ("MD5_ComputeNormals: out of memory (%u verts/%u tris)", (unsigned int)numverts, (unsigned int)(numindexes / 3));

	for (v = 0; v < numverts; v++)
	{
		unsigned pos = MD5_HashVert(vert[v].xyz) % hashsize;
		unsigned end = pos;
		weld[v] = (unsigned short)v;

		do
		{
			if (!hashmap[pos])
			{
				hashmap[pos] = (int)v + 1;
				break;
			}

			t = (size_t)hashmap[pos] - 1;
			if (VectorCompare(vert[t].xyz, vert[v].xyz))
			{
				weld[v] = weld[t];
				break;
			}

			pos++;
			if (pos == hashsize)
				pos = 0;
		} while (pos != end);
	}

	for (t = 0; t < numindexes; t+=3)
	{
		vec3_t d1, d2, norm;
		unsigned short i0 = weld[indexes[t+0]];
		unsigned short i1 = weld[indexes[t+1]];
		unsigned short i2 = weld[indexes[t+2]];
		iqmvert_t *v0 = &vert[i0];
		iqmvert_t *v1 = &vert[i1];
		iqmvert_t *v2 = &vert[i2];

		VectorSubtract(v1->xyz, v0->xyz, d1);
		VectorSubtract(v2->xyz, v0->xyz, d2);
		CrossProduct(d1, d2, norm);

		VectorAdd(normals[i0], norm, normals[i0]);
		VectorAdd(normals[i1], norm, normals[i1]);
		VectorAdd(normals[i2], norm, normals[i2]);
	}

	for (v = 0; v < numverts; v++)
	{
		if (weld[v] == v)
		{
			VectorNormalize(normals[v]);
			VectorCopy(normals[v], vert[v].norm);
		}
		else
			VectorCopy(vert[weld[v]].norm, vert[v].norm);
	}

	free(normals);
	free(weld);
	free(hashmap);
}

static unsigned int MD5_HackyModelFlags(const char *name)
{
	unsigned int ret = 0;
	char oldmodel[MAX_QPATH];
	mdl_t *f;
	COM_StripExtension(name, oldmodel, sizeof(oldmodel));
	COM_AddExtension(oldmodel, ".mdl", sizeof(oldmodel));

	f = (mdl_t*)COM_LoadMallocFile(oldmodel, NULL);
	if (f)
	{
		if (com_filesize >= sizeof(*f) && LittleLong(f->ident) == IDPOLYHEADER && LittleLong(f->version) == ALIAS_VERSION)
			ret = f->flags;
		free(f);
	}
	return ret;
}

struct md5animctx_s
{
	void *animfile;
	const void *buffer;
	char fname[MAX_QPATH];
	size_t numposes;
	size_t numjoints;
	float frametime;
	bonepose_t *posedata;
};
static void MD5Anim_Discard(struct md5animctx_s *ctx)
{
	free(ctx->animfile);
	ctx->animfile = NULL;
	ctx->buffer = NULL;
	ctx->numposes = 0;
	ctx->numjoints = 0;
	ctx->posedata = NULL;
}
//This is split into two because aliashdr_t has silly trailing framegroup info.
static void MD5Anim_Begin(struct md5animctx_s *ctx, const char *fname, size_t numbones)
{
	//Load an md5anim into it, if we can.
	COM_StripExtension(fname, ctx->fname, sizeof(ctx->fname));
	COM_AddExtension(ctx->fname, ".md5anim", sizeof(ctx->fname));
	fname = ctx->fname;
	ctx->animfile = COM_LoadMallocFile(fname, NULL);
	ctx->numposes = 0;
	ctx->frametime = 0.1f;

	if (ctx->animfile)
	{
		const void *buffer = COM_Parse(ctx->animfile);
		MD5EXPECT("MD5Version");
		MD5EXPECT("10");
		if (MD5CHECK("commandline"))	buffer = COM_Parse(buffer);
		MD5EXPECT("numFrames");	ctx->numposes = MD5UINT();
		MD5EXPECT("numJoints");	ctx->numjoints = MD5UINT();
		MD5EXPECT("frameRate");
		{
			char *endptr;
			double framerate = SDL_strtod(com_token, &endptr);
			if (endptr == com_token || *endptr || !isfinite(framerate) || framerate <= 0)
			{
				Con_Warning ("%s has an invalid frame rate; ignoring animation\n", fname);
				MD5Anim_Discard(ctx);
				return;
			}
			ctx->frametime = (float)(1.0 / framerate);
		}

		if (!ctx->numposes)
		{
			Con_Warning ("%s has no poses; ignoring animation\n", fname);
			MD5Anim_Discard(ctx);
			return;
		}
		if (ctx->numposes > MAXALIASFRAMES)
		{
			Con_Warning ("%s has too many poses (%llu > %u); ignoring animation\n",
				fname, (unsigned long long)ctx->numposes, (unsigned int)MAXALIASFRAMES);
			MD5Anim_Discard(ctx);
			return;
		}
		if (ctx->numjoints != numbones)
		{
			Con_Warning ("%s has %llu bones, but the mesh has %llu; ignoring animation\n",
				fname, (unsigned long long)ctx->numjoints, (unsigned long long)numbones);
			MD5Anim_Discard(ctx);
			return;
		}

		ctx->buffer = buffer;
	}
}
static void MD5Anim_Load(struct md5animctx_s *ctx, boneinfo_t *bones, size_t numbones)
{
	const char *fname = ctx->fname;
	struct {unsigned int flags; size_t offset;} *ab;
	size_t rawcount;
	float *raw, *r;
	vec3_t *basepos;
	vec4_t *basequat;
	byte *framedone;
	bonepose_t *outposes;
	const void *buffer = COM_Parse(ctx->buffer);
	size_t framecount, j;

	if (!buffer)
	{
		MD5Anim_Discard(ctx);
		return;
	}

	MD5EXPECT("numAnimatedComponents");	rawcount = MD5UINT();

	if (ctx->numjoints != numbones)
	{
		Con_Warning ("%s bone count changed while loading; ignoring animation\n", fname);
		MD5Anim_Discard(ctx);
		return;
	}
	if (ctx->numposes > (size_t)INT_MAX)
		Sys_Error ("%s has too many poses", fname);

	if (rawcount > (size_t)INT_MAX / sizeof(*raw) - 6)
		Sys_Error ("%s: too many animated components", fname);
	raw = Z_Malloc(GLMesh_CheckedHunkSize(rawcount + 6, sizeof(*raw), "MD5Anim_Load"));
	ab = Z_Malloc(GLMesh_CheckedHunkSize(ctx->numjoints, sizeof(*ab), "MD5Anim_Load"));
	basepos = Z_Malloc(GLMesh_CheckedHunkSize(ctx->numjoints, sizeof(*basepos), "MD5Anim_Load"));
	basequat = Z_Malloc(GLMesh_CheckedHunkSize(ctx->numjoints, sizeof(*basequat), "MD5Anim_Load"));
	framedone = Z_Malloc(GLMesh_CheckedHunkSize(ctx->numposes, sizeof(*framedone), "MD5Anim_Load"));

	MD5EXPECT("hierarchy");
	MD5EXPECT("{");
	for (j = 0; j < ctx->numjoints; j++)
	{
		long parent;

		//validate stuff
		if (strcmp(bones[j].name, com_token))
		{
			Con_Warning ("%s: bone %u has a different name than the mesh; ignoring animation\n",
				fname, (unsigned int)j);
			goto ignore_animation;
		}
		buffer = COM_Parse(buffer);
		parent = MD5SINT();
		if (parent < -1 || (parent >= 0 && (size_t)parent >= j))
		{
			Con_Warning ("%s: bone %u has invalid parent %ld; treating it as a root bone\n",
				fname, (unsigned int)j, parent);
			parent = -1;
		}
		if (bones[j].parent != parent)
		{
			Con_Warning ("%s: bone %u has parent %ld, but the mesh uses %d; ignoring animation\n",
				fname, (unsigned int)j, parent, bones[j].parent);
			goto ignore_animation;
		}
		//new info
		{
			size_t flags = MD5UINT();
			if (flags & ~63u)
				Sys_Error ("%s: bone has unsupported flags", fname);
			ab[j].flags = (unsigned int)flags;
		}
		ab[j].offset = MD5UINT();
		{
			unsigned int components = 0, flags = ab[j].flags;
			while (flags)
			{
				components += flags & 1;
				flags >>= 1;
			}
			if (ab[j].offset > rawcount || components > rawcount - ab[j].offset)
				Sys_Error ("%s: bone has bad offset", fname);
		}
	}
	MD5EXPECT("}");

	//Only commit pose data to the hunk after the optional animation has been
	//verified against the mesh hierarchy. Ignored animations then need no hunk unwind.
	ctx->posedata = outposes = Hunk_Alloc(GLMesh_CheckedHunkSize(GLMesh_CheckedSize(ctx->numjoints, ctx->numposes, "MD5Anim_Load"), sizeof(*outposes), "MD5Anim_Load"));

	MD5EXPECT("bounds");
	MD5EXPECT("{");
	for (j = 0; j < ctx->numposes; j++)
	{
		MD5EXPECT("(");
		(void)MD5FLOAT();
		(void)MD5FLOAT();
		(void)MD5FLOAT();
		MD5EXPECT(")");

		MD5EXPECT("(");
		(void)MD5FLOAT();
		(void)MD5FLOAT();
		(void)MD5FLOAT();
		MD5EXPECT(")");
	}
	MD5EXPECT("}");

	MD5EXPECT("baseframe");
	MD5EXPECT("{");
	for (j = 0; j < ctx->numjoints; j++)
	{
		MD5EXPECT("(");
		basepos[j][0] = MD5FLOAT();
		basepos[j][1] = MD5FLOAT();
		basepos[j][2] = MD5FLOAT();
		MD5EXPECT(")");

		MD5EXPECT("(");
		basequat[j][0] = MD5FLOAT();
		basequat[j][1] = MD5FLOAT();
		basequat[j][2] = MD5FLOAT();
		MD5EXPECT(")");
	}
	MD5EXPECT("}");

	framecount = 0;
	while(MD5CHECK("frame"))
	{
		size_t idx = MD5UINT();
		if (idx >= ctx->numposes)
			Sys_Error ("%s: invalid pose index", fname);
		if (framedone[idx])
			Sys_Error ("%s: duplicate pose index", fname);
		framedone[idx] = true;
		framecount++;
		MD5EXPECT("{");
		for (j = 0; j < rawcount; j++)
			raw[j] = MD5FLOAT();
		MD5EXPECT("}");

		//okay, we have our raw info, unpack the actual bone info.
		for (j = 0; j < ctx->numjoints; j++)
		{
			vec3_t pos;
			static vec3_t scale = {1,1,1};
			vec4_t quat;
			VectorCopy(basepos[j], pos);
			quat[0] = basequat[j][0];
			quat[1] = basequat[j][1];
			quat[2] = basequat[j][2];
			r = raw + ab[j].offset;
			if (ab[j].flags & 1)	pos[0] = *r++;
			if (ab[j].flags & 2)	pos[1] = *r++;
			if (ab[j].flags & 4)	pos[2] = *r++;

			if (ab[j].flags & 8)	quat[0] = *r++;
			if (ab[j].flags & 16)	quat[1] = *r++;
			if (ab[j].flags & 32)	quat[2] = *r++;

			quat[3] = 1 - DotProduct(quat,quat);
			if (quat[3] < 0)
				quat[3] = 0;//we have no imagination.
			quat[3] = -sqrt(quat[3]);

			GenMatrixPosQuat4Scale(pos, quat, scale, outposes[idx*ctx->numjoints + j].mat);
		}
	}
	if (framecount != ctx->numposes)
		Sys_Error ("%s: missing pose frame", fname);
	Z_Free(framedone);
	Z_Free(basequat);
	Z_Free(basepos);
	Z_Free(raw);
	Z_Free(ab);
	free(ctx->animfile);
	ctx->animfile = NULL;
	ctx->buffer = NULL;
	return;

ignore_animation:
	Z_Free(framedone);
	Z_Free(basequat);
	Z_Free(basepos);
	Z_Free(raw);
	Z_Free(ab);
	MD5Anim_Discard(ctx);
}
void Mod_LoadMD5MeshModel (qmodel_t *mod, const void *buffer)
{
	const char				*fname = mod->name;
	unsigned short			*poutindexes;
	iqmvert_t				*poutvert;
	int						start, end, total;
	aliashdr_t				*outhdr, *surf;
	size_t					hdrsize;
	size_t					numhdrframes;

	bonepose_t				*outposes;
	boneinfo_t				*outbones;

	size_t					numjoints, j;
	size_t					nummeshes, m;
	char					texname[MAX_QPATH];
	struct md5vertinfo_s	*vinfo;
	struct md5weightinfo_s	*weight;
	size_t					numweights;
	size_t					invalidboneweights = 0;
	size_t					firstinvalidweight = 0;
	size_t					firstinvalidbone = 0;
	size_t					firstinvalidmesh = 0;

	struct md5animctx_s		anim = {NULL};

	start = Hunk_LowMark ();

	buffer = COM_Parse(buffer);

	MD5EXPECT("MD5Version");
	MD5EXPECT("10");
	if (MD5CHECK("commandline"))	buffer = COM_Parse(buffer);
	MD5EXPECT("numJoints");	numjoints = MD5UINT();
	MD5EXPECT("numMeshes");	nummeshes = MD5UINT();

	if (!numjoints)
		Sys_Error ("%s has no bones", mod->name);
	if (!nummeshes)
		Sys_Error ("%s has no meshes", mod->name);
	if (numjoints > 256)
		Sys_Error ("%s has too many bones", mod->name);

	if (strcmp(com_token, "joints")) Sys_Error ("Mod_LoadMD5MeshModel(%s): Expected \"%s\"", fname, "joints");
	MD5Anim_Begin(&anim, fname, numjoints);
	buffer = COM_Parse(buffer);

	hdrsize = sizeof(*outhdr) - sizeof(outhdr->frames);
	numhdrframes = q_max((size_t)1, anim.numposes);
	GLMesh_AddCheckedSize(&hdrsize, GLMesh_CheckedSize(numhdrframes, sizeof(outhdr->frames), "Mod_LoadMD5MeshModel"), "Mod_LoadMD5MeshModel");
	outhdr = Hunk_Alloc(GLMesh_CheckedHunkSize (nummeshes, hdrsize, "Mod_LoadMD5MeshModel"));
	outbones = Hunk_Alloc(GLMesh_CheckedHunkSize (numjoints, sizeof(*outbones), "Mod_LoadMD5MeshModel"));
	outposes = Z_Malloc(GLMesh_CheckedHunkSize (numjoints, sizeof(*outposes), "Mod_LoadMD5MeshModel"));

	MD5EXPECT("{");
	for (j = 0; j < numjoints; j++)
	{
		vec3_t pos;
		static vec3_t scale = {1,1,1};
		vec4_t quat;
		q_strlcpy(outbones[j].name, com_token, sizeof(outbones[j].name));	buffer = COM_Parse(buffer);
		{
			long parent = MD5SINT();
			if (parent < -1 || (parent >= 0 && (size_t)parent >= j))
			{
				Con_Warning ("%s: bone %u has invalid parent %ld; treating it as a root bone\n",
					fname, (unsigned int)j, parent);
				parent = -1;
			}
			outbones[j].parent = (int)parent;
		}
		MD5EXPECT("(");
		pos[0] = MD5FLOAT();
		pos[1] = MD5FLOAT();
		pos[2] = MD5FLOAT();
		MD5EXPECT(")");
		MD5EXPECT("(");
		quat[0] = MD5FLOAT();
		quat[1] = MD5FLOAT();
		quat[2] = MD5FLOAT();
		quat[3] = 1 - DotProduct(quat,quat);
		if (quat[3] < 0)
			quat[3] = 0;//we have no imagination.
		quat[3] = -sqrt(quat[3]);
		MD5EXPECT(")");

		GenMatrixPosQuat4Scale(pos, quat, scale, outposes[j].mat);
		Matrix3x4_Invert_Simple(outposes[j].mat, outbones[j].inverse.mat);	//absolute, so we can just invert now.
	}

	if (strcmp(com_token, "}")) Sys_Error ("Mod_LoadMD5MeshModel(%s): Expected \"%s\"", fname, "}");
	MD5Anim_Load(&anim, outbones, numjoints);
	buffer = COM_Parse(buffer);

	for (m = 0; m < nummeshes; m++)
	{
		MD5EXPECT("mesh");
		MD5EXPECT("{");

		surf = (aliashdr_t*)((byte*)outhdr + m*hdrsize);
		if (m+1 < nummeshes)
			surf->nextsurface = hdrsize;
		else
			surf->nextsurface = 0;

		surf->poseverttype = PV_IQM;
		for (j = 0; j < 3; j++)
		{
			surf->scale_origin[j] = 0;
			surf->scale[j] = 1.0;
		}

		surf->numbones = (int)numjoints;
		surf->boneinfo = (byte*)outbones-(byte*)surf;

		if (anim.numposes)
		{
			surf->boneposedata = (byte*)anim.posedata-(byte*)surf;
			surf->numboneposes = (int)anim.numposes;

			for (j = 0; j < anim.numposes; j++)
			{
				surf->frames[j].firstpose = (int)j;
				surf->frames[j].numposes = 1;
				surf->frames[j].interval = anim.frametime;
			}
			surf->numframes = (int)j;
		}
		else
		{
			surf->frames[0].firstpose = 0;
			surf->frames[0].numposes = 1;
			surf->frames[0].interval = 0.1f;
			surf->numframes = 1;
		}

		MD5EXPECT("shader");
		//MD5 violation: the skin is a single material. adding prefixes/postfixes here is the wrong thing to do.
		//but we do so anyway, because rerelease compat.
		for (surf->numskins = 0; surf->numskins < MAX_SKINS; surf->numskins++)
		{
			int fwidth, fheight;
			unsigned int f;
			enum srcformat fmt;
			qboolean malloced;
			void *data;
			int mark = Hunk_LowMark ();
			for (f = 0; f < countof(surf->textures[0]); f++)
			{
				q_snprintf(texname, sizeof(texname), "progs/%s_%02u_%02u", com_token, surf->numskins, f);

				data = Image_LoadImage (texname, &fwidth, &fheight, &fmt, &malloced);
				//now load whatever we found
				if (data) //load external image
				{
					surf->textures[surf->numskins][f].base = TexMgr_LoadImage (mod, texname, fwidth, fheight, fmt, data, texname, 0, TEXPREF_ALPHA|TEXPREF_NOBRIGHT|TEXPREF_MIPMAP );
					surf->textures[surf->numskins][f].lower = NULL;
					surf->textures[surf->numskins][f].upper = NULL;
					surf->textures[surf->numskins][f].luma = NULL;
					if (fmt == SRC_INDEXED)
					{	//8bit base texture. use it for fullbrights.
						size_t pixels = (size_t)fwidth * (size_t)fheight;
						for (j = 0; j < pixels; j++)
						{
							if (((byte*)data)[j] > 223)
							{
								surf->textures[surf->numskins][f].luma = TexMgr_LoadImage (mod, va("%s_luma", texname), fwidth, fheight, fmt, data, texname, 0, TEXPREF_ALPHA|TEXPREF_FULLBRIGHT|TEXPREF_MIPMAP );
								break;
							}
						}
					}
					else
					{	//we found a 32bit base texture.
						if (!surf->textures[surf->numskins][f].luma)
						{
							q_snprintf(texname, sizeof(texname), "progs/%s_%02u_%02u_glow", com_token, surf->numskins, f);
							surf->textures[surf->numskins][f].luma = TexMgr_LoadImage(mod, texname, surf->skinwidth, surf->skinheight, SRC_EXTERNAL, NULL, texname, 0, TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);
						}
						if (!surf->textures[surf->numskins][f].luma)
						{
							q_snprintf(texname, sizeof(texname), "progs/%s_%02u_%02u_luma", com_token, surf->numskins, f);
							surf->textures[surf->numskins][f].luma = TexMgr_LoadImage(mod, texname, surf->skinwidth, surf->skinheight, SRC_EXTERNAL, NULL, texname, 0, TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);
						}
					}

					//try to load pants/shirt mask textures (works with both indexed and 32bit base)
					q_snprintf(texname, sizeof(texname), "progs/%s_%02u_%02u_pants", com_token, surf->numskins, f);
					surf->textures[surf->numskins][f].lower = TexMgr_LoadImage(mod, texname, surf->skinwidth, surf->skinheight, SRC_EXTERNAL, NULL, texname, 0, TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);

					q_snprintf(texname, sizeof(texname), "progs/%s_%02u_%02u_shirt", com_token, surf->numskins, f);
					surf->textures[surf->numskins][f].upper = TexMgr_LoadImage(mod, texname, surf->skinwidth, surf->skinheight, SRC_EXTERNAL, NULL, texname, 0, TEXPREF_ALLOWMISSING|TEXPREF_MIPMAP);

					//now try to load glow/luma image from the same place
					if (malloced)
						free(data);
					Hunk_FreeToLowMark (mark);
				}
				else
					break;
			}
			if (f == 0)
				break;	//no images loaded...

			if (f == 3)
				Con_Warning("progs/%s_%02u_##: 3 skinframes found...\n", com_token, surf->numskins);
			for (j = f; j < countof(surf->textures[0]); j++)
				surf->textures[surf->numskins][j] = surf->textures[surf->numskins][j - f];
		}
		surf->skinwidth = surf->textures[0][0].base?surf->textures[0][0].base->width:1;
		surf->skinheight = surf->textures[0][0].base?surf->textures[0][0].base->height:1;
		buffer = COM_Parse(buffer);
		MD5EXPECT("numverts");
		{
			size_t numverts = MD5UINT();
			if (!numverts)
				Sys_Error ("%s: mesh has no vertices", fname);
			if (numverts > (size_t)INT_MAX)
				Sys_Error ("%s: too many vertices", fname);
			if (numverts > (size_t)USHRT_MAX + 1)
				Sys_Error ("%s: too many vertices for 16-bit indexes", fname);
			surf->numverts_vbo = surf->numverts = (int)numverts;
		}

		vinfo = Z_Malloc(GLMesh_CheckedHunkSize ((size_t)surf->numverts, sizeof(*vinfo), "Mod_LoadMD5MeshModel"));
		poutvert = Hunk_Alloc(GLMesh_CheckedHunkSize ((size_t)surf->numverts, sizeof(*poutvert), "Mod_LoadMD5MeshModel"));
		surf->vertexes = (byte*)poutvert-(byte*)surf;
		surf->nummorphposes = 1;
		{
			byte *vertdone = Z_Malloc(GLMesh_CheckedHunkSize ((size_t)surf->numverts, sizeof(*vertdone), "Mod_LoadMD5MeshModel"));
			while (MD5CHECK("vert"))
			{
				size_t idx = MD5UINT();
				if (idx >= (size_t)surf->numverts)
					Sys_Error ("%s: vertex index out of bounds", fname);
				if (vertdone[idx])
					Sys_Error ("%s: duplicate vertex index", fname);
				vertdone[idx] = true;
				MD5EXPECT("(");
				poutvert[idx].st[0] = MD5FLOAT();
				poutvert[idx].st[1] = MD5FLOAT();
				MD5EXPECT(")");
				vinfo[idx].firstweight = MD5UINT();
				{
					size_t count = MD5UINT();
					if (count > (size_t)UINT_MAX)
						Sys_Error ("%s: too many weights on vertex", fname);
					vinfo[idx].count = (unsigned int)count;
				}
			}
			for (j = 0; j < (size_t)surf->numverts; j++)
				if (!vertdone[j])
					Sys_Error ("%s: missing vertex", fname);
			Z_Free(vertdone);
		}
		MD5EXPECT("numtris");
		{
			size_t numtris = MD5UINT();
			if (!numtris)
				Sys_Error ("%s: mesh has no triangles", fname);
			if (numtris > (size_t)INT_MAX / 3)
				Sys_Error ("%s: too many triangles", fname);
			surf->numtris = (int)numtris;
			surf->numindexes = surf->numtris * 3;
		}
		poutindexes = Hunk_Alloc(GLMesh_CheckedHunkSize ((size_t)surf->numindexes, sizeof(*poutindexes), "Mod_LoadMD5MeshModel"));
		surf->indexes = (byte*)poutindexes-(byte*)surf;
		{
			byte *tridone = Z_Malloc(GLMesh_CheckedHunkSize ((size_t)surf->numtris, sizeof(*tridone), "Mod_LoadMD5MeshModel"));
			while (MD5CHECK("tri"))
			{
				size_t idx = MD5UINT();
				if (idx >= (size_t)surf->numtris)
					Sys_Error ("%s: triangle index out of bounds", fname);
				if (tridone[idx])
					Sys_Error ("%s: duplicate triangle index", fname);
				tridone[idx] = true;
				idx *= 3;
				for (j = 0; j < 3; j++)
				{
					size_t t = MD5UINT();
					if (t >= (size_t)surf->numverts)
						Sys_Error ("%s: vertex index out of bounds", fname);
					poutindexes[idx+j] = t;
				}
			}
			for (j = 0; j < (size_t)surf->numtris; j++)
				if (!tridone[j])
					Sys_Error ("%s: missing triangle", fname);
			Z_Free(tridone);
		}

		//md5 is a gpu-unfriendly interchange format. :(
		MD5EXPECT("numweights");
		numweights = MD5UINT();
		if (!numweights)
			Sys_Error ("%s: mesh has no weights", fname);
		weight = Z_Malloc(GLMesh_CheckedHunkSize (numweights, sizeof(*weight), "Mod_LoadMD5MeshModel"));
		{
			byte *weightdone = Z_Malloc(GLMesh_CheckedHunkSize (numweights, sizeof(*weightdone), "Mod_LoadMD5MeshModel"));
			while (MD5CHECK("weight"))
			{
				size_t idx = MD5UINT();
				if (idx >= numweights)
					Sys_Error ("%s: weight index out of bounds", fname);
				if (weightdone[idx])
					Sys_Error ("%s: duplicate weight index", fname);
				weightdone[idx] = true;

				weight[idx].bone = MD5UINT();
				if (weight[idx].bone >= numjoints)
				{
					if (!invalidboneweights)
					{
						firstinvalidweight = idx;
						firstinvalidbone = weight[idx].bone;
						firstinvalidmesh = m;
					}
					invalidboneweights++;
					weight[idx].bone = 0;
				}
				weight[idx].pos[3] = MD5FLOAT();
				MD5EXPECT("(");
				weight[idx].pos[0] = MD5FLOAT()*weight[idx].pos[3];
				weight[idx].pos[1] = MD5FLOAT()*weight[idx].pos[3];
				weight[idx].pos[2] = MD5FLOAT()*weight[idx].pos[3];
				MD5EXPECT(")");
			}
			for (j = 0; j < numweights; j++)
				if (!weightdone[j])
					Sys_Error ("%s: missing weight", fname);
			Z_Free(weightdone);
		}
		//so make it gpu-friendly.
		MD5_BakeInfluences(fname, outposes, poutvert, vinfo, weight, surf->numverts, numweights);
		//and now make up the normals that the format lacks. we'll still probably have issues from seams, but then so did qme, so at least its faithful... :P
		MD5_ComputeNormals(poutvert, surf->numverts, poutindexes, surf->numindexes);

		Z_Free(weight);
		Z_Free(vinfo);

		MD5EXPECT("}");
	}
	if (invalidboneweights)
	{
		Con_Warning ("%s: %llu weights reference invalid bones (first: mesh %llu, weight %llu, bone %llu); using bone 0\n",
			fname, (unsigned long long)invalidboneweights,
			(unsigned long long)firstinvalidmesh, (unsigned long long)firstinvalidweight,
			(unsigned long long)firstinvalidbone);
	}
	Z_Free(outposes);

//
// make sure pointer-relative alias data stayed inside one hunk segment before
// any consumer walks those offsets.
//
	end = Hunk_LowMark ();
	total = end - start;

	if (!Hunk_IsContiguous (start, end))
		Sys_Error ("Mod_LoadMD5MeshModel: %s spans multiple hunk segments (try a larger -heapsize)", mod->name);

	GLMesh_LoadVertexBuffer (mod, outhdr);

	//the md5 format does not have its own modelflags, yet we still need to know about trails and rotating etc
	mod->flags = MD5_HackyModelFlags(mod->name);

	mod->numframes = q_max(1, (int)anim.numposes);
	mod->synctype = ST_FRAMETIME;	//keep IQM animations synced to when .frame is changed. framegroups are otherwise not very useful.
	mod->type = mod_alias;

	Mod_CalcAliasBounds (outhdr); //johnfitz

//
// move the complete, relocatable alias model to the cache
//
	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, outhdr, total);

	Hunk_FreeToLowMark (start);
}
