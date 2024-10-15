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
// r_light.c

#include "quakedef.h"

extern cvar_t r_flatlightstyles; //johnfitz

//Spike - made this a general function
void CL_UpdateLightstyle(unsigned int idx, const char *str)
{
	int total;
	int j;
	if (idx < MAX_LIGHTSTYLES)
	{
		q_strlcpy (cl_lightstyle[idx].map, str, MAX_STYLESTRING);
		cl_lightstyle[idx].length = Q_strlen(cl_lightstyle[idx].map);
		//johnfitz -- save extra info
		if (cl_lightstyle[idx].length)
		{
			total = 0;
			cl_lightstyle[idx].peak = 'a';
			for (j=0; j<cl_lightstyle[idx].length; j++)
			{
				total += cl_lightstyle[idx].map[j] - 'a';
				cl_lightstyle[idx].peak = q_max(cl_lightstyle[idx].peak, cl_lightstyle[idx].map[j]);
			}
			cl_lightstyle[idx].average = total / cl_lightstyle[idx].length + 'a';
		}
		else
			cl_lightstyle[idx].average = cl_lightstyle[idx].peak = 'm';
		//johnfitz
	}
}

/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (void)
{
	int			i,j,k;

//
// light animations
// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = (int)(cl.time*10);
	for (j=0 ; j<MAX_LIGHTSTYLES ; j++)
	{
		if (!cl_lightstyle[j].length)
		{
			d_lightstylevalue[j] = 256;
			continue;
		}
		//johnfitz -- r_flatlightstyles
		if (r_flatlightstyles.value == 2)
			k = cl_lightstyle[j].peak - 'a';
		else if (r_flatlightstyles.value == 1)
			k = cl_lightstyle[j].average - 'a';
		else
		{
			k = i % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
		}
		d_lightstylevalue[j] = k*22;
		//johnfitz
	}
}

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING (gl_flashblend 1)

=============================================================================
*/

void AddLightBlend (float r, float g, float b, float a2)
{
	float	a;

	v_blend[3] = a = v_blend[3] + a2*(1-v_blend[3]);

	a2 = a2/a;

	v_blend[0] = v_blend[1]*(1-a2) + r*a2;
	v_blend[1] = v_blend[1]*(1-a2) + g*a2;
	v_blend[2] = v_blend[2]*(1-a2) + b*a2;
}

void R_RenderDlight (dlight_t *light)
{
	int		i, j;
	float	a;
	vec3_t	v;
	float	rad;

	rad = light->radius * 0.35;

	VectorSubtract (light->origin, r_origin, v);
	if (VectorLength (v) < rad)
	{	// view is inside the dlight
		if (light->color[0]==1 && light->color[1]==1 && light->color[2]==1)
			AddLightBlend (1.0, 0.5, 0.0, light->radius * 0.0003);
		else
			AddLightBlend (light->color[0], light->color[1], light->color[2], light->radius * 0.0003);
		return;
	}

	glBegin (GL_TRIANGLE_FAN);
	if (light->color[0]==1 && light->color[1]==1 && light->color[2]==1)	//if its default full-white, show it with an orange tint instead to replicate expected QS behaviour without breaking coloured dlights.
		glColor3f (0.2f, 0.1f, 0.0);
	else
		glColor3f (light->color[0]*.2f, light->color[1]*.2f, light->color[2]*.2f);
	for (i=0 ; i<3 ; i++)
		v[i] = light->origin[i] - vpn[i]*rad;
	glVertex3fv (v);
	glColor3f (0,0,0);
	for (i=16 ; i>=0 ; i--)
	{
		a = i/16.0 * M_PI*2;
		for (j=0 ; j<3 ; j++)
			v[j] = light->origin[j] + vright[j]*cos(a)*rad
				+ vup[j]*sin(a)*rad;
		glVertex3fv (v);
	}
	glEnd ();
}

/*
=============
R_RenderDlights
=============
*/
void R_RenderDlights (void)
{
	int		i;
	dlight_t	*l;

	if (!gl_flashblend.value)
		return;

	glDepthMask (0);
	glDisable (GL_TEXTURE_2D);
	glShadeModel (GL_SMOOTH);
	glEnable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE);

	l = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->die < cl.time || (l->spawn > cl.mtime[0] && cls.demoplayback) || !l->radius) // woods (iw) #democontrols
			continue;
		R_RenderDlight (l);
	}

	glColor3f (1,1,1);
	glDisable (GL_BLEND);
	glEnable (GL_TEXTURE_2D);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask (1);
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights -- johnfitz -- rewritten to use LordHavoc's lighting speedup
=============
*/
void R_MarkLights (dlight_t *light, vec3_t lightorg, int framecount, int num, mnode_t *node)
{
	mplane_t	*splitplane;
	msurface_t	*surf;
	vec3_t		impact;
	float		dist, l, maxdist;
	unsigned int i;
	int			 j, s, t;

start:

	if (node->contents < 0)
		return;

	splitplane = node->plane;
	if (splitplane->type < 3)
		dist = lightorg[splitplane->type] - splitplane->dist;
	else
		dist = DotProduct (lightorg, splitplane->normal) - splitplane->dist;

	if (dist > light->radius)
	{
		node = node->children[0];
		goto start;
	}
	if (dist < -light->radius)
	{
		node = node->children[1];
		goto start;
	}

	maxdist = light->radius*light->radius;

	if (!cl.worldmodel) // woods
	{
		Con_DPrintf("cl.worldmodel is null in R_MarkLights\n");
		return;
	}

// mark the polygons
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		for (j=0 ; j<3 ; j++)
			impact[j] = lightorg[j] - surf->plane->normal[j]*dist;
		// clamp center of light to corner and check brightness
		l = DotProduct (impact, surf->lmvecs[0]) + surf->lmvecs[0][3];
		s = l;if (s < 0) s = 0;else if (s > surf->extents[0]) s = surf->extents[0];
		s = l - s;
		l = DotProduct (impact, surf->lmvecs[1]) + surf->lmvecs[1][3];
		t = l;if (t < 0) t = 0;else if (t > surf->extents[1]) t = surf->extents[1];
		t = l - t;
		// compare to minimum light
		if ((s*s+t*t+dist*dist) < maxdist)
		{
			if (surf->dlightframe != framecount) // not dynamic until now
			{
				surf->dlightbits[num >> 5] = 1U << (num & 31);
				surf->dlightframe = framecount;
			}
			else // already dynamic
				surf->dlightbits[num >> 5] |= 1U << (num & 31);
		}
	}

	if (node->children[0]->contents >= 0)
		R_MarkLights (light, lightorg, framecount, num, node->children[0]);
	if (node->children[1]->contents >= 0)
		R_MarkLights (light, lightorg, framecount, num, node->children[1]);
}

/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

	if (gl_flashblend.value)
		return;
	if (!r_refdef.drawworld)
		return;
	l = cl_dlights;

	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->spawn > cl.mtime[0] && cls.demoplayback) // woods (iw) #democontrols
		{
			l->die = 0.f;
			continue;
		}

		if (l->die < cl.time || !l->radius)
			continue;
		R_MarkLights (l, l->origin, r_framecount, i, cl.worldmodel->nodes);
	}
}


/*
=============================================================================

BSPX LIGHTGRID LOADING + SAMPLING

=============================================================================
*/
extern char	loadname[];	// for hunk tags. yuck yuck yuck.
extern cvar_t	mod_lightgrid;	//for testing/debugging
typedef struct
{
	vec3_t gridscale;
	unsigned int count[3];
	vec3_t mins;
	unsigned int styles;

	unsigned int rootnode;

	unsigned int numnodes;
	struct bspxlgnode_s
	{	//this uses an octtree to trim samples.
		int mid[3];
		unsigned int child[8];
#define LGNODE_LEAF		(1u<<31)
#define LGNODE_MISSING	(1u<<30)
	} *nodes;
	unsigned int numleafs;
	struct bspxlgleaf_s
	{
		int mins[3];
		int size[3];
		struct bspxlgsamp_s
		{
			struct
			{
				byte style;
				byte rgb[3];
			} map[4];
		} *rgbvalues;
	} *leafs;
} bspxlightgrid_t;
struct rctx_s {byte *data; int ofs, size;};
static byte ReadByte(struct rctx_s *ctx)
{
	if (ctx->ofs >= ctx->size)
	{
		ctx->ofs++;
		return 0;
	}
	return ctx->data[ctx->ofs++];
}
static int ReadInt(struct rctx_s *ctx)
{
	int r = (int)ReadByte(ctx)<<0;
		r|= (int)ReadByte(ctx)<<8;
		r|= (int)ReadByte(ctx)<<16;
		r|= (int)ReadByte(ctx)<<24;
	return r;
}
static float ReadFloat(struct rctx_s *ctx)
{
	union {float f; int i;} u;
	u.i = ReadInt(ctx);
	return u.f;
}
void BSPX_LightGridLoad(qmodel_t *model, void *lgdata, size_t lgsize)
{
	vec3_t step, mins;
	int size[3];
	bspxlightgrid_t *grid;
	unsigned int numstyles, numnodes, numleafs, rootnode;
	unsigned int nodestart, leafsamps = 0, i, j, k, s;
	struct bspxlgsamp_s *samp;
	struct rctx_s ctx = {0};
	ctx.data = lgdata;
	ctx.size = lgsize;
	model->lightgrid = NULL;
	if (!ctx.data)
		return;

	for (j = 0; j < 3; j++)
		step[j] = ReadFloat(&ctx);
	for (j = 0; j < 3; j++)
		size[j] = ReadInt(&ctx);
	for (j = 0; j < 3; j++)
		mins[j] = ReadFloat(&ctx);

	numstyles = ReadByte(&ctx);	//urgh, misaligned the entire thing
	rootnode = ReadInt(&ctx);
	numnodes = ReadInt(&ctx);
	nodestart = ctx.ofs;
	ctx.ofs += (3+8)*4*numnodes;
	numleafs = ReadInt(&ctx);
	for (i = 0; i < numleafs; i++)
	{
		unsigned int lsz[3];
		ctx.ofs += 3*4;
		for (j = 0; j < 3; j++)
			lsz[j] = ReadInt(&ctx);
		j = lsz[0]*lsz[1]*lsz[2];
		leafsamps += j;
		while (j --> 0)
		{	//this loop is annonying, memcpy dreams...
			s = ReadByte(&ctx);
			if (s == 255)
				continue;
			ctx.ofs += s*4;
		}
	}

	grid = Hunk_AllocName(sizeof(*grid) + sizeof(*grid->leafs)*numleafs + sizeof(*grid->nodes)*numnodes + sizeof(struct bspxlgsamp_s)*leafsamps, loadname);
//	memset(grid, 0xcc, sizeof(*grid) + sizeof(*grid->leafs)*numleafs + sizeof(*grid->nodes)*numnodes + sizeof(struct bspxlgsamp_s)*leafsamps);
	grid->leafs = (void*)(grid+1);
	grid->nodes = (void*)(grid->leafs + numleafs);
	samp = (void*)(grid->nodes+numnodes);

	for (j = 0; j < 3; j++)
		grid->gridscale[j] = 1/step[j];	//prefer it as a multiply
	VectorCopy(mins, grid->mins);
	VectorCopy(size, grid->count);
	grid->numnodes = numnodes;
	grid->numleafs = numleafs;
	grid->rootnode = rootnode;
	(void)numstyles;

	//rewind to the nodes. *sigh*
	ctx.ofs = nodestart;
	for (i = 0; i < numnodes; i++)
	{
		for (j = 0; j < 3; j++)
			grid->nodes[i].mid[j] = ReadInt(&ctx);
		for (j = 0; j < 8; j++)
			grid->nodes[i].child[j] = ReadInt(&ctx);
	}
	ctx.ofs += 4;
	for (i = 0; i < numleafs; i++)
	{
		for (j = 0; j < 3; j++)
			grid->leafs[i].mins[j] = ReadInt(&ctx);
		for (j = 0; j < 3; j++)
			grid->leafs[i].size[j] = ReadInt(&ctx);

		grid->leafs[i].rgbvalues = samp;

		j = grid->leafs[i].size[0]*grid->leafs[i].size[1]*grid->leafs[i].size[2];
		while (j --> 0)
		{
			s = ReadByte(&ctx);
			if (s == 0xff)
				memset(samp, 0xff, sizeof(*samp));
			else
			{
				for (k = 0; k < s; k++)
				{
					if (k >= 4)
						ReadInt(&ctx);
					else
					{
						samp->map[k].style = ReadByte(&ctx);
						samp->map[k].rgb[0] = ReadByte(&ctx);
						samp->map[k].rgb[1] = ReadByte(&ctx);
						samp->map[k].rgb[2] = ReadByte(&ctx);
					}
				}
				for (; k < 4; k++)
				{
					samp->map[k].style = (byte)~0u;
					samp->map[k].rgb[0] =
					samp->map[k].rgb[1] =
					samp->map[k].rgb[2] = 0;
				}
			}
			samp++;
		}
	}

	if (ctx.ofs != ctx.size)
		grid = NULL;

	model->lightgrid = (void*)grid;
}
static int BSPX_LightGridSingleValue(bspxlightgrid_t *grid, int x, int y, int z, float w, vec3_t res_diffuse)
{
	int i;
	unsigned int node;
	struct bspxlgsamp_s *samp;
	float lev;

	node = grid->rootnode;
	while (!(node & LGNODE_LEAF))
	{
		struct bspxlgnode_s *n;
		if (node & LGNODE_MISSING)
			return 0;	//failure
		n = grid->nodes + node;
		node = n->child[
				((x>=n->mid[0])<<2)|
				((y>=n->mid[1])<<1)|
				((z>=n->mid[2])<<0)];
	}

	{
		struct bspxlgleaf_s *leaf = &grid->leafs[node & ~LGNODE_LEAF];
		x -= leaf->mins[0];
		y -= leaf->mins[1];
		z -= leaf->mins[2];
		if (x >= leaf->size[0] ||
			y >= leaf->size[1] ||
			z >= leaf->size[2])
			return 0;	//sample we're after is out of bounds...

		i = x + leaf->size[0]*(y + leaf->size[1]*z);
		samp = leaf->rgbvalues + i;

		w *= (1/256.0);

		//no hdr support
		for (i = 0; i < countof(samp->map); i++)
		{
			if (samp->map[i].style == ((byte)(~0u)))
				break;	//no more
			lev = d_lightstylevalue[samp->map[i].style]*w;
			res_diffuse[0] += samp->map[i].rgb[0] * lev;
			res_diffuse[1] += samp->map[i].rgb[1] * lev;
			res_diffuse[2] += samp->map[i].rgb[2] * lev;
		}
	}
	return 1;
}
static void BSPX_LightGridValue(bspxlightgrid_t *grid, const vec3_t point, vec3_t res_diffuse)
{
	int i, tile[3];
	float s, w, frac[3];

	res_diffuse[0] = res_diffuse[1] = res_diffuse[2] = 0; //assume worst

	for (i = 0; i < 3; i++)
	{
		tile[i] = floor((point[i] - grid->mins[i]) * grid->gridscale[i]);
		frac[i] = (point[i] - grid->mins[i]) * grid->gridscale[i] - tile[i];
	}

	for (i = 0, s = 0; i < 8; i++)
	{
		w =	((i&1)?frac[0]:1-frac[0])
		  * ((i&2)?frac[1]:1-frac[1])
		  * ((i&4)?frac[2]:1-frac[2]);
		s += w*BSPX_LightGridSingleValue(grid,	tile[0]+!!(i&1),
												tile[1]+!!(i&2),
												tile[2]+!!(i&4), w, res_diffuse);
	}
	if (s)
		VectorScale(res_diffuse, 1.0/s, res_diffuse);	//average the successful ones
}

/*
=============================================================================

LEGACY LIGHT SAMPLING

=============================================================================
*/

mplane_t		*lightplane;
vec3_t			lightspot;
vec3_t			lightcolor; //johnfitz -- lit support via lordhavoc

/*
=============
RecursiveLightPoint -- johnfitz -- replaced entire function for lit support via lordhavoc
=============
*/
int RecursiveLightPoint (vec3_t color, mnode_t *node, vec3_t rayorg, vec3_t start, vec3_t end, float *maxdist)
{
	float		front, back, frac;
	vec3_t		mid;

loc0:
	if (node->contents < 0)
		return false;		// didn't hit anything

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
//		return RecursiveLightPoint (color, node->children[front < 0], rayorg, start, end, maxdist);
	{
		node = node->children[front < 0];
		goto loc0;
	}

	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;

// go down front side
	if (RecursiveLightPoint (color, node->children[front < 0], rayorg, start, mid, maxdist))
		return true;	// hit something
	else
	{
		unsigned int i;
		int ds, dt;
		msurface_t *surf;
	// check for impact on this node
		VectorCopy (mid, lightspot);
		lightplane = node->plane;

		surf = cl.worldmodel->surfaces + node->firstsurface;
		for (i = 0;i < node->numsurfaces;i++, surf++)
		{
			float sfront, sback, dist;
			vec3_t raydelta;
			double dsfrac, dtfrac;

			if (surf->flags & SURF_DRAWTILED)
				continue;	// no lightmaps

		// ericw -- added double casts to force 64-bit precision.
		// Without them the zombie at the start of jam3_ericw.bsp was
		// incorrectly being lit up in SSE builds.
			dsfrac = DoublePrecisionDotProduct (mid, surf->lmvecs[0]) + surf->lmvecs[0][3];
			dtfrac = DoublePrecisionDotProduct (mid, surf->lmvecs[1]) + surf->lmvecs[1][3];
			if (dsfrac < 0 || dtfrac < 0)
				continue;

			if (dsfrac > surf->extents[0] || dtfrac > surf->extents[1])
				continue;
			ds = dsfrac;
			dt = dtfrac;
			dsfrac -= ds;
			dtfrac -= dt;

			if (surf->plane->type < 3)
			{
				sfront = rayorg[surf->plane->type] - surf->plane->dist;
				sback = end[surf->plane->type] - surf->plane->dist;
			}
			else
			{
				sfront = DotProduct(rayorg, surf->plane->normal) - surf->plane->dist;
				sback = DotProduct(end, surf->plane->normal) - surf->plane->dist;
			}
			VectorSubtract(end, rayorg, raydelta);
			dist = sfront / (sfront - sback) * VectorLength(raydelta);

			if (!surf->samples)
			{
				// We hit a surface that is flagged as lightmapped, but doesn't have actual lightmap info.
				// Instead of just returning black, we'll keep looking for nearby surfaces that do have valid samples.
				// This fixes occasional pitch-black models in otherwise well-lit areas in DOTM (e.g. mge1m1, mge4m1)
				// caused by overlapping surfaces with mixed lighting data.
				const float nearby = 8.f;
				dist += nearby;
				*maxdist = q_min(*maxdist, dist);
				continue;
			}

			if (dist < *maxdist)
			{
				// LordHavoc: enhanced to interpolate lighting
				int maps, line3, r00 = 0, g00 = 0, b00 = 0, r01 = 0, g01 = 0, b01 = 0, r10 = 0, g10 = 0, b10 = 0, r11 = 0, g11 = 0, b11 = 0;
				float scale, e;

				if (cl.worldmodel->flags & MOD_HDRLIGHTING)
				{
					static const float rgb9e5tab[32] = {	//multipliers for the 9-bit mantissa, according to the biased mantissa
						//aka: pow(2, biasedexponent - bias-bits) where bias is 15 and bits is 9
						1.0/(1<<24),	1.0/(1<<23),	1.0/(1<<22),	1.0/(1<<21),	1.0/(1<<20),	1.0/(1<<19),	1.0/(1<<18),	1.0/(1<<17),
						1.0/(1<<16),	1.0/(1<<15),	1.0/(1<<14),	1.0/(1<<13),	1.0/(1<<12),	1.0/(1<<11),	1.0/(1<<10),	1.0/(1<<9),
						1.0/(1<<8),		1.0/(1<<7),		1.0/(1<<6),		1.0/(1<<5),		1.0/(1<<4),		1.0/(1<<3),		1.0/(1<<2),		1.0/(1<<1),
						1.0,			1.0*(1<<1),		1.0*(1<<2),		1.0*(1<<3),		1.0*(1<<4),		1.0*(1<<5),		1.0*(1<<6),		1.0*(1<<7),
					};
					uint32_t *lightmap = (uint32_t*)surf->samples + (dt * (surf->extents[0]+1) + ds);
					line3 = (surf->extents[0]+1);
					for (maps = 0;maps < MAXLIGHTMAPS && surf->styles[maps] != INVALID_LIGHTSTYLE;maps++)
					{
						scale = (1<<7) * (float) d_lightstylevalue[surf->styles[maps]] * 1.0f / 256.0f;
						e = rgb9e5tab[lightmap[      0]>>27] * scale;r00 += ((lightmap[      0]>> 0)&0x1ff) * e;g00 += ((lightmap[      0]>> 9)&0x1ff) * e;b00 += ((lightmap[      0]>> 9)&0x1ff) * e;
						e = rgb9e5tab[lightmap[      1]>>27] * scale;r01 += ((lightmap[      1]>> 0)&0x1ff) * e;g01 += ((lightmap[      1]>> 9)&0x1ff) * e;b01 += ((lightmap[      1]>> 9)&0x1ff) * e;
						e = rgb9e5tab[lightmap[line3+0]>>27] * scale;r10 += ((lightmap[line3+0]>> 0)&0x1ff) * e;g10 += ((lightmap[line3+0]>> 9)&0x1ff) * e;b10 += ((lightmap[line3+0]>> 9)&0x1ff) * e;
						e = rgb9e5tab[lightmap[line3+1]>>27] * scale;r11 += ((lightmap[line3+1]>> 0)&0x1ff) * e;g11 += ((lightmap[line3+1]>> 9)&0x1ff) * e;b11 += ((lightmap[line3+1]>> 9)&0x1ff) * e;
						lightmap += (surf->extents[0]+1) * (surf->extents[1]+1);
					}
				}
				else
				{
					byte *lightmap = (byte*)surf->samples + (dt * (surf->extents[0]+1) + ds)*3; // LordHavoc: *3 for color
					line3 = (surf->extents[0]+1)*3;
					for (maps = 0;maps < MAXLIGHTMAPS && surf->styles[maps] != INVALID_LIGHTSTYLE;maps++)
					{
						scale = (float) d_lightstylevalue[surf->styles[maps]] * 1.0f / 256.0f;
						r00 += (float) lightmap[      0] * scale;g00 += (float) lightmap[      1] * scale;b00 += (float) lightmap[2] * scale;
						r01 += (float) lightmap[      3] * scale;g01 += (float) lightmap[      4] * scale;b01 += (float) lightmap[5] * scale;
						r10 += (float) lightmap[line3+0] * scale;g10 += (float) lightmap[line3+1] * scale;b10 += (float) lightmap[line3+2] * scale;
						r11 += (float) lightmap[line3+3] * scale;g11 += (float) lightmap[line3+4] * scale;b11 += (float) lightmap[line3+5] * scale;
						lightmap += (surf->extents[0]+1) * (surf->extents[1]+1)*3; // LordHavoc: *3 for colored lighting
					}
				}

				color[0] += (float) ((int) ((((((r11-r10) * dsfrac) + r10)-(((r01-r00) * dsfrac) + r00)) * dtfrac) + (((r01-r00) * dsfrac) + r00)));
				color[1] += (float) ((int) ((((((g11-g10) * dsfrac) + g10)-(((g01-g00) * dsfrac) + g00)) * dtfrac) + (((g01-g00) * dsfrac) + g00)));
				color[2] += (float) ((int) ((((((b11-b10) * dsfrac) + b10)-(((b01-b00) * dsfrac) + b00)) * dtfrac) + (((b01-b00) * dsfrac) + b00)));
			}
			return true; // success
		}

	// go down back side
		return RecursiveLightPoint (color, node->children[front >= 0], rayorg, mid, end, maxdist);
	}
}

/*
=============
R_LightPoint -- johnfitz -- replaced entire function for lit support via lordhavoc
=============
*/
int R_LightPoint (vec3_t p)
{
	vec3_t		end;
	float		maxdist = 8192.f; //johnfitz -- was 2048

	if (!cl.worldmodel->lightdata)
	{
		lightcolor[0] = lightcolor[1] = lightcolor[2] = 255;
		return 255;
	}

	if (cl.worldmodel->lightgrid && mod_lightgrid.value)
		BSPX_LightGridValue(cl.worldmodel->lightgrid, p, lightcolor);
	else
	{
		end[0] = p[0];
		end[1] = p[1];
		end[2] = p[2] - maxdist;

		lightcolor[0] = lightcolor[1] = lightcolor[2] = 0;
		RecursiveLightPoint (lightcolor, cl.worldmodel->nodes, p, p, end, &maxdist);
	}
	return ((lightcolor[0] + lightcolor[1] + lightcolor[2]) * (1.0f / 3.0f));
}
