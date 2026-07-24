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

/*
 * Exact collision-hull debug rendering.
 *
 * A clipnode plane is infinite.  To recover its finite boundary, this code
 * portalizes the clip BSP: node-plane windings are clipped by the portals
 * bounding each convex region, then split down to adjacent leaf pairs.
 * Only portals separating blocking and non-blocking contents are retained.
 *
 * Diff mode moves each oriented boundary back to surface space using the
 * selected hull's asymmetric support distance, indexes coplanar rendered
 * world faces, and subtracts their polygon coverage.  The cached remainder
 * is therefore collision-only geometry rather than a second, disconnected
 * attempt at a watertight deflated hull.
 *
 * Info mode casts through the crosshair, highlights the nearest visible hull
 * face, and reports its exact clip-plane and rendered-coverage metadata.
 */

#include "quakedef.h"

#include <limits.h>
#include <stdint.h>

#define HULLDEBUG_MAX_MODEL_BYTES	(256u * 1024u * 1024u)
#define HULLDEBUG_MAX_DEPTH		4096u
#define HULLDEBUG_MIN_BASE_EXTENT	65536.0
#define HULLDEBUG_MIN_WORK_STEPS		100000u
#define HULLDEBUG_MAX_WORK_STEPS		10000000u
#define HULLDEBUG_NORMAL_DOT_EPSILON	0.00001
#define HULLDEBUG_NORMAL_HASH_CELL	0.005

typedef struct
{
	double xyz[3];
} hullpoint_t;

typedef struct
{
	hullpoint_t *points;
	size_t numpoints;
} hullpoly_t;

typedef struct hullportal_s hullportal_t;
typedef struct hulltreenode_s hulltreenode_t;

struct hulltreenode_s
{
	int clipnode;
	int contents;
	int planenum;
	double normal[3];
	double dist;
	hulltreenode_t *children[2];
	hullportal_t *portals;
	hulltreenode_t *allnext;
};

struct hullportal_s
{
	hullpoly_t poly;
	size_t polybytes;
	double normal[3];	/* points from nodes[1] to nodes[0] */
	double dist;
	int planenum;
	qboolean bounds_portal;
	hulltreenode_t *nodes[2];
	hullportal_t *next[2];
	hullportal_t *allnext;
};

typedef struct
{
	double normal[3];	/* points out of the current cell */
	double dist;		/* current cell is normal.x <= dist */
	int sibling;
	int planenum;
} hullhalfspace_t;

typedef struct
{
	uint32_t firstvert;
	uint32_t numverts;
	uint32_t sourceface;
	uint32_t coverage_matches;
	int planenum;
	int outside_contents;
	vec3_t normal;
	float dist;
	float raw_area;
	float residual_area;
	vec3_t mins;
	vec3_t maxs;
} hullmeshface_t;

typedef struct
{
	vec3_t *verts;		/* face-local rings; duplication is intentional */
	hullmeshface_t *faces;
	vec3_t *diffverts;
	hullmeshface_t *difffaces;
	size_t numverts;
	size_t numfaces;
	size_t numdiffverts;
	size_t numdifffaces;
	size_t vertcapacity;
	size_t facecapacity;
	size_t diffvertcapacity;
	size_t difffacecapacity;
	size_t allocated_bytes;
	qboolean built;
	qboolean failed;
	qboolean point_fallback;
	qboolean diff_built;
	qboolean diff_failed;
} hullmesh_t;

typedef struct
{
	hullmesh_t meshes[3];
	size_t allocated_bytes;
} hullmodelcache_t;

typedef struct
{
	qmodel_t *model;
	hull_t *hull;
	hullmodelcache_t *cache;
	hullmesh_t *mesh;
	int hullnum;
	int firstnode;
	int lastnode;
	size_t numnodes;
	size_t maxdepth;
	size_t maxpoints;
	size_t maxsteps;
	size_t steps;
	size_t nodes_visited;
	size_t blocking_leaves;
	size_t portals_allocated;
	byte *active;
	hullpoint_t *scratch[2];
	hulltreenode_t *tree_nodes;
	hullportal_t *portals;
	size_t temp_bytes;
	size_t traversal_bytes;
	double base_extent;
	double draw_offset[3];
	qboolean failed;
	char failure[128];
} hullbuild_t;

typedef struct
{
	double xy[2];
} hullpoint2_t;

typedef struct hullpoly2_s
{
	hullpoint2_t *points;
	size_t numpoints;
	size_t capacity;
	struct hullpoly2_s *next;
} hullpoly2_t;

typedef struct
{
	int *buckets;
	int *planeheads;
	int *planenext;
	int *surfacenext;
	size_t numbuckets;
	size_t numplanes;
	size_t numsurfaces;
	size_t bucketbytes;
	size_t planeheadbytes;
	size_t planenextbytes;
	size_t surfacenextbytes;
	size_t indexedplanes;
	size_t indexedsurfaces;
} hullrenderindex_t;

typedef struct
{
	int frame;
	int hullnum;
	int planenum;
	int outside_contents;
	size_t display_face;
	size_t source_face;
	size_t display_faces;
	size_t display_verts;
	size_t allocated_bytes;
	uint32_t faceverts;
	uint32_t coverage_matches;
	vec3_t hullmins;
	vec3_t hullmaxs;
	vec3_t normal;
	vec3_t point;
	float display_dist;
	float origin_dist;
	float surface_dist;
	float surface_shift;
	float view_distance;
	float area;
	float coverage_percent;
	char model[MAX_QPATH];
	char classification[48];
	qboolean active;
	qboolean hit;
	qboolean diff;
	qboolean point_fallback;
	qboolean coverage_ready;
} hullinspect_t;

enum
{
	HULLFILTER_ALL = 0,	/* every retained boundary face */
	HULLFILTER_WALLS,	/* only near-vertical faces you can walk into */
	HULLFILTER_REACH,	/* walls near the player, at body height, facing it */
	HULLFILTER_BLOCK	/* reach walls too tall to step over: real blockers */
};

/* A face is a "wall" when its normal is steep enough that the player cannot
 * stand on it: floors (n.z >= this) and ceilings (n.z <= -this) are hidden.
 * 0.7 ~= cos 45 degrees, the usual walkable/blocking split. */
#define HULLDEBUG_WALL_MAX_NORMAL_Z	0.7f
/* Horizontal distance the reach filter treats as "close enough to bump". */
#define HULLDEBUG_REACH_RADIUS		160.0f
/* Quake's SV_FlyMove step height: a wall whose top is within this of the
 * player's feet is climbed, not blocked (matches STEPSIZE in sv_phys.c). */
#define HULLDEBUG_STEP_HEIGHT		18.0f
/* Block mode looks this far ahead along the path of travel... */
#define HULLDEBUG_BLOCK_FORWARD		1024.0f
/* ...within this lateral half-width of the travel line (player half-width
 * plus a margin), so only walls actually in the corridor ahead show. */
#define HULLDEBUG_BLOCK_HALFWIDTH	40.0f

typedef struct
{
	qboolean	active;
	vec3_t		origin;		/* player collision origin (feet), world space */
	float		zmin, zmax;	/* player body vertical span */
	float		radius2;	/* reach radius squared, horizontal */
	qboolean	directional;	/* block mode: only faces ahead in the path */
	vec3_t		dir;		/* horizontal movement/look direction, unit */
} hullreach_t;

static cvar_t r_showhull =
	{"r_showhull", "0", CVAR_NONE, 0, NULL, NULL, NULL, NULL, NULL};
static int r_showhull_mode = -1;
static qboolean r_showhull_diff;
static qboolean r_showhull_info;
static int r_showhull_filter = HULLFILTER_ALL;
static hullreach_t hull_debug_reach;
static hullmodelcache_t hull_debug_failed_cache;
static hullinspect_t hull_debug_inspect;

static void HullDebug_Fail (hullbuild_t *build, const char *reason)
{
	if (build->failed)
		return;

	build->failed = true;
	q_strlcpy (build->failure, reason, sizeof(build->failure));
}

static qboolean HullDebug_MultiplySize (size_t count, size_t size, size_t *result)
{
	if (count && size > (size_t)-1 / count)
		return false;
	*result = count * size;
	return true;
}

static qboolean HullDebug_IsBlocking (int contents)
{
	return contents == CONTENTS_SOLID || contents == CONTENTS_CLIP;
}

static double HullDebug_Dot (const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void HullDebug_Cross (const double a[3], const double b[3], double out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static double HullDebug_LengthSquared (const double v[3])
{
	return HullDebug_Dot (v, v);
}

static qboolean HullDebug_NodePlane (hullbuild_t *build, int node,
	double normal[3], double *dist, int *planenum)
{
	mclipnode_t *clipnode;
	mplane_t *plane;
	double length;

	if (node < build->firstnode || node > build->lastnode)
	{
		HullDebug_Fail (build, "clipnode index outside hull range");
		return false;
	}

	clipnode = build->hull->clipnodes + node;
	if (clipnode->planenum < 0 || clipnode->planenum >= build->model->numplanes)
	{
		HullDebug_Fail (build, "clipnode plane index outside model plane range");
		return false;
	}

	plane = build->hull->planes + clipnode->planenum;
	normal[0] = plane->normal[0];
	normal[1] = plane->normal[1];
	normal[2] = plane->normal[2];
	length = sqrt (HullDebug_LengthSquared(normal));
	if (!isfinite(length) || !isfinite(plane->dist))
	{
		HullDebug_Fail (build, "clipnode has a non-finite plane");
		return false;
	}
	if (!(length > 0.000000001))
	{
		HullDebug_Fail (build, "clipnode has a zero-length plane normal");
		return false;
	}

	normal[0] /= length;
	normal[1] /= length;
	normal[2] /= length;
	*dist = plane->dist / length;
	*planenum = clipnode->planenum;
	return true;
}

static size_t HullDebug_CleanPolygon (hullpoint_t *points, size_t count)
{
	const double duplicate_epsilon2 = DIST_EPSILON * DIST_EPSILON;
	size_t i, out;
	qboolean changed;

	if (count < 3)
		return 0;

	out = 0;
	for (i = 0; i < count; ++i)
	{
		double delta[3];
		size_t previous = out ? out - 1 : count - 1;

		delta[0] = points[i].xyz[0] - points[previous].xyz[0];
		delta[1] = points[i].xyz[1] - points[previous].xyz[1];
		delta[2] = points[i].xyz[2] - points[previous].xyz[2];
		if (HullDebug_LengthSquared(delta) <= duplicate_epsilon2)
			continue;
		points[out++] = points[i];
	}
	count = out;

	if (count > 2)
	{
		double delta[3];
		delta[0] = points[0].xyz[0] - points[count - 1].xyz[0];
		delta[1] = points[0].xyz[1] - points[count - 1].xyz[1];
		delta[2] = points[0].xyz[2] - points[count - 1].xyz[2];
		if (HullDebug_LengthSquared(delta) <= duplicate_epsilon2)
			--count;
	}

	do
	{
		changed = false;
		for (i = 0; count >= 3 && i < count; ++i)
		{
			size_t previous = (i + count - 1) % count;
			size_t next = (i + 1) % count;
			double edge1[3], edge2[3], cross[3];
			double scale;

			edge1[0] = points[i].xyz[0] - points[previous].xyz[0];
			edge1[1] = points[i].xyz[1] - points[previous].xyz[1];
			edge1[2] = points[i].xyz[2] - points[previous].xyz[2];
			edge2[0] = points[next].xyz[0] - points[i].xyz[0];
			edge2[1] = points[next].xyz[1] - points[i].xyz[1];
			edge2[2] = points[next].xyz[2] - points[i].xyz[2];
			HullDebug_Cross (edge1, edge2, cross);
			scale = HullDebug_LengthSquared(edge1) * HullDebug_LengthSquared(edge2);
			if (scale > 0.0 && HullDebug_LengthSquared(cross) > scale * 1e-12)
				continue;

			memmove (&points[i], &points[i + 1],
				(count - i - 1) * sizeof(*points));
			--count;
			changed = true;
			break;
		}
	} while (changed);

	return count >= 3 ? count : 0;
}

static double HullDebug_PolygonArea (const hullpoint_t *points, size_t count)
{
	double sum[3] = {0.0, 0.0, 0.0};
	size_t i;

	for (i = 0; i < count; ++i)
	{
		double cross[3];
		HullDebug_Cross (points[i].xyz, points[(i + 1) % count].xyz, cross);
		sum[0] += cross[0];
		sum[1] += cross[1];
		sum[2] += cross[2];
	}

	return 0.5 * sqrt (HullDebug_LengthSquared(sum));
}

static size_t HullDebug_ClipHalfspace (const hullpoint_t *input,
	size_t incount, hullpoint_t *output, size_t outcapacity,
	const double normal[3], double dist)
{
	size_t i, outcount = 0;

	if (!incount)
		return 0;

	for (i = 0; i < incount; ++i)
	{
		const hullpoint_t *a = &input[i];
		const hullpoint_t *b = &input[(i + 1) % incount];
		double da = HullDebug_Dot(a->xyz, normal) - dist;
		double db = HullDebug_Dot(b->xyz, normal) - dist;
		qboolean ainside = da <= ON_EPSILON;
		qboolean binside = db <= ON_EPSILON;

		if (ainside)
		{
			if (outcount >= outcapacity)
				return (size_t)-1;
			output[outcount++] = *a;
		}

		if (ainside != binside)
		{
			double denominator = da - db;
			double fraction;
			hullpoint_t point;

			if (fabs(denominator) < 1e-20)
				continue;
			fraction = da / denominator;
			point.xyz[0] = a->xyz[0] + fraction * (b->xyz[0] - a->xyz[0]);
			point.xyz[1] = a->xyz[1] + fraction * (b->xyz[1] - a->xyz[1]);
			point.xyz[2] = a->xyz[2] + fraction * (b->xyz[2] - a->xyz[2]);
			if (outcount >= outcapacity)
				return (size_t)-1;
			output[outcount++] = point;
		}
	}

	return outcount;
}

static void HullDebug_BaseWinding (const hullhalfspace_t *boundary,
	double extent, hullpoint_t points[4])
{
	double origin[3], axis[3] = {0.0, 0.0, 0.0};
	double right[3], up[3], length;
	int major;
	size_t i;
	static const double signs[4][2] =
	{
		{-1.0, -1.0},
		{ 1.0, -1.0},
		{ 1.0,  1.0},
		{-1.0,  1.0}
	};

	origin[0] = boundary->normal[0] * boundary->dist;
	origin[1] = boundary->normal[1] * boundary->dist;
	origin[2] = boundary->normal[2] * boundary->dist;

	major = 0;
	if (fabs(boundary->normal[1]) < fabs(boundary->normal[major]))
		major = 1;
	if (fabs(boundary->normal[2]) < fabs(boundary->normal[major]))
		major = 2;
	axis[major] = 1.0;

	HullDebug_Cross (axis, boundary->normal, right);
	length = sqrt (HullDebug_LengthSquared(right));
	right[0] /= length;
	right[1] /= length;
	right[2] /= length;
	HullDebug_Cross (boundary->normal, right, up);

	for (i = 0; i < 4; ++i)
	{
		points[i].xyz[0] = origin[0] +
			extent * (signs[i][0] * right[0] + signs[i][1] * up[0]);
		points[i].xyz[1] = origin[1] +
			extent * (signs[i][0] * right[1] + signs[i][1] * up[1]);
		points[i].xyz[2] = origin[2] +
			extent * (signs[i][0] * right[2] + signs[i][1] * up[2]);
	}
}

static qboolean HullDebug_Reserve (hullbuild_t *build, void **data,
	size_t *capacity, size_t needed, size_t elementsize)
{
	size_t oldbytes, newbytes, newcapacity;
	void *replacement;

	if (needed <= *capacity)
		return true;

	newcapacity = *capacity ? *capacity : 64;
	while (newcapacity < needed)
	{
		if (newcapacity > (size_t)-1 / 2)
		{
			HullDebug_Fail (build, "collision mesh capacity overflow");
			return false;
		}
		newcapacity *= 2;
	}

	if (!HullDebug_MultiplySize(*capacity, elementsize, &oldbytes) ||
	    !HullDebug_MultiplySize(newcapacity, elementsize, &newbytes))
	{
		HullDebug_Fail (build, "collision mesh byte-size overflow");
		return false;
	}
	{
		size_t used = build->cache->allocated_bytes;
		if (used <= HULLDEBUG_MAX_MODEL_BYTES - build->temp_bytes)
			used += build->temp_bytes;
		else
			used = HULLDEBUG_MAX_MODEL_BYTES;
		if (used <= HULLDEBUG_MAX_MODEL_BYTES - build->traversal_bytes)
			used += build->traversal_bytes;
		else
			used = HULLDEBUG_MAX_MODEL_BYTES;
		if (newbytes - oldbytes > HULLDEBUG_MAX_MODEL_BYTES - used)
		{
			HullDebug_Fail (build,
				"collision mesh exceeded the 256 MiB model limit");
			return false;
		}
	}

	replacement = realloc (*data, newbytes);
	if (!replacement)
	{
		HullDebug_Fail (build, "out of memory while growing collision mesh");
		return false;
	}

	*data = replacement;
	*capacity = newcapacity;
	build->mesh->allocated_bytes += newbytes - oldbytes;
	build->cache->allocated_bytes += newbytes - oldbytes;
	return true;
}

static void HullDebug_FreeMeshStorage (hullmodelcache_t *cache, hullmesh_t *mesh)
{
	free (mesh->verts);
	free (mesh->faces);
	free (mesh->diffverts);
	free (mesh->difffaces);
	if (cache->allocated_bytes >= mesh->allocated_bytes)
		cache->allocated_bytes -= mesh->allocated_bytes;
	else
		cache->allocated_bytes = 0;
	mesh->verts = NULL;
	mesh->faces = NULL;
	mesh->diffverts = NULL;
	mesh->difffaces = NULL;
	mesh->numverts = 0;
	mesh->numfaces = 0;
	mesh->numdiffverts = 0;
	mesh->numdifffaces = 0;
	mesh->vertcapacity = 0;
	mesh->facecapacity = 0;
	mesh->diffvertcapacity = 0;
	mesh->difffacecapacity = 0;
	mesh->allocated_bytes = 0;
}

static void HullDebug_FreeDiffStorage (hullmodelcache_t *cache,
	hullmesh_t *mesh)
{
	size_t bytes = mesh->diffvertcapacity * sizeof(*mesh->diffverts) +
		mesh->difffacecapacity * sizeof(*mesh->difffaces);

	free (mesh->diffverts);
	free (mesh->difffaces);
	if (mesh->allocated_bytes >= bytes)
		mesh->allocated_bytes -= bytes;
	else
		mesh->allocated_bytes = 0;
	if (cache->allocated_bytes >= bytes)
		cache->allocated_bytes -= bytes;
	else
		cache->allocated_bytes = 0;
	mesh->diffverts = NULL;
	mesh->difffaces = NULL;
	mesh->numdiffverts = 0;
	mesh->numdifffaces = 0;
	mesh->diffvertcapacity = 0;
	mesh->difffacecapacity = 0;
}

static qboolean HullDebug_AppendFace (hullbuild_t *build,
	const hullhalfspace_t *boundary, hullpoly_t *poly, int outside_contents)
{
	hullmeshface_t *face;
	size_t firstvert, i;
	double area, dist;

	poly->numpoints = HullDebug_CleanPolygon (poly->points, poly->numpoints);
	if (poly->numpoints < 3)
		return true;
	area = HullDebug_PolygonArea(poly->points, poly->numpoints);
	if (!isfinite(area) || area > FLT_MAX)
	{
		HullDebug_Fail (build,
			"collision portal produced an invalid polygon area");
		return false;
	}
	if (area <= ON_EPSILON * ON_EPSILON)
		return true;

	for (i = 0; i < poly->numpoints; ++i)
	{
		double pointdist = HullDebug_Dot(poly->points[i].xyz,
			boundary->normal);

		if (!isfinite(pointdist) ||
		    fabs(pointdist - boundary->dist) > ON_EPSILON * 2.0)
		{
			HullDebug_Fail (build,
				"collision portal did not lie on its boundary plane");
			return false;
		}
	}

	if (build->mesh->numfaces > UINT32_MAX ||
	    build->mesh->numverts > UINT32_MAX ||
	    poly->numpoints > UINT32_MAX - build->mesh->numverts)
	{
		HullDebug_Fail (build, "collision mesh exceeded 32-bit indices");
		return false;
	}

	if (!HullDebug_Reserve(build, (void **)&build->mesh->verts,
		&build->mesh->vertcapacity,
		build->mesh->numverts + poly->numpoints,
		sizeof(*build->mesh->verts)) ||
	    !HullDebug_Reserve(build, (void **)&build->mesh->faces,
		&build->mesh->facecapacity, build->mesh->numfaces + 1,
		sizeof(*build->mesh->faces)))
		return false;

	firstvert = build->mesh->numverts;
	face = &build->mesh->faces[build->mesh->numfaces];
	memset (face, 0, sizeof(*face));
	face->firstvert = (uint32_t)firstvert;
	face->numverts = (uint32_t)poly->numpoints;
	face->sourceface = (uint32_t)build->mesh->numfaces;
	face->planenum = boundary->planenum;
	face->outside_contents = outside_contents;
	face->normal[0] = (float)boundary->normal[0];
	face->normal[1] = (float)boundary->normal[1];
	face->normal[2] = (float)boundary->normal[2];
	dist = boundary->dist +
		boundary->normal[0] * build->draw_offset[0] +
		boundary->normal[1] * build->draw_offset[1] +
		boundary->normal[2] * build->draw_offset[2];
	face->dist = (float)dist;
	face->raw_area = (float)area;
	face->residual_area = (float)area;
	face->mins[0] = face->mins[1] = face->mins[2] = FLT_MAX;
	face->maxs[0] = face->maxs[1] = face->maxs[2] = -FLT_MAX;

	for (i = 0; i < poly->numpoints; ++i)
	{
		vec3_t *vertex = &build->mesh->verts[build->mesh->numverts++];
		int axis;

		(*vertex)[0] = (float)(poly->points[i].xyz[0] + build->draw_offset[0]);
		(*vertex)[1] = (float)(poly->points[i].xyz[1] + build->draw_offset[1]);
		(*vertex)[2] = (float)(poly->points[i].xyz[2] + build->draw_offset[2]);
		for (axis = 0; axis < 3; ++axis)
		{
			face->mins[axis] = q_min(face->mins[axis], (*vertex)[axis]);
			face->maxs[axis] = q_max(face->maxs[axis], (*vertex)[axis]);
		}
	}

	++build->mesh->numfaces;
	return true;
}

static qboolean HullDebug_CopyPoly (const hullpoint_t *points, size_t count,
	hullpoly_t *poly)
{
	size_t bytes;

	poly->points = NULL;
	poly->numpoints = 0;
	if (!HullDebug_MultiplySize(count, sizeof(*points), &bytes))
		return false;
	poly->points = (hullpoint_t *)malloc (bytes);
	if (!poly->points)
		return false;
	memcpy (poly->points, points, bytes);
	poly->numpoints = count;
	return true;
}

static void HullDebug_FreePoly (hullpoly_t *poly)
{
	free (poly->points);
	poly->points = NULL;
	poly->numpoints = 0;
}

static qboolean HullDebug_SplitPoly (hullbuild_t *build,
	const hullpoly_t *input, const double normal[3], double dist,
	hullpoly_t *front, hullpoly_t *back)
{
	size_t capacity = input->numpoints + 2;
	size_t bytes, i;

	memset (front, 0, sizeof(*front));
	memset (back, 0, sizeof(*back));
	if (capacity > build->maxpoints ||
	    !HullDebug_MultiplySize(capacity, sizeof(hullpoint_t), &bytes))
	{
		HullDebug_Fail (build, "collision polygon exceeded traversal point limit");
		return false;
	}

	front->points = (hullpoint_t *)malloc (bytes);
	back->points = (hullpoint_t *)malloc (bytes);
	if (!front->points || !back->points)
	{
		HullDebug_FreePoly (front);
		HullDebug_FreePoly (back);
		HullDebug_Fail (build, "out of memory while splitting collision polygon");
		return false;
	}

	for (i = 0; i < input->numpoints; ++i)
	{
		const hullpoint_t *a = &input->points[i];
		const hullpoint_t *b = &input->points[(i + 1) % input->numpoints];
		double da = HullDebug_Dot(a->xyz, normal) - dist;
		double db = HullDebug_Dot(b->xyz, normal) - dist;
		int aside = da > ON_EPSILON ? SIDE_FRONT :
			(da < -ON_EPSILON ? SIDE_BACK : SIDE_ON);
		int bside = db > ON_EPSILON ? SIDE_FRONT :
			(db < -ON_EPSILON ? SIDE_BACK : SIDE_ON);

		if (aside != SIDE_BACK)
			front->points[front->numpoints++] = *a;
		if (aside != SIDE_FRONT)
			back->points[back->numpoints++] = *a;

		if ((aside == SIDE_FRONT && bside == SIDE_BACK) ||
		    (aside == SIDE_BACK && bside == SIDE_FRONT))
		{
			double fraction = da / (da - db);
			hullpoint_t point;
			point.xyz[0] = a->xyz[0] +
				fraction * (b->xyz[0] - a->xyz[0]);
			point.xyz[1] = a->xyz[1] +
				fraction * (b->xyz[1] - a->xyz[1]);
			point.xyz[2] = a->xyz[2] +
				fraction * (b->xyz[2] - a->xyz[2]);
			front->points[front->numpoints++] = point;
			back->points[back->numpoints++] = point;
		}
	}

	front->numpoints = HullDebug_CleanPolygon (front->points, front->numpoints);
	back->numpoints = HullDebug_CleanPolygon (back->points, back->numpoints);
	return true;
}

static qboolean HullDebug_TakeStep (hullbuild_t *build)
{
	if (++build->steps <= build->maxsteps)
		return true;
	HullDebug_Fail (build, "collision BSP traversal exceeded its work limit");
	return false;
}

static qboolean HullDebug_TempReserve (hullbuild_t *build, size_t bytes)
{
	size_t used = build->cache->allocated_bytes;

	if (used <= HULLDEBUG_MAX_MODEL_BYTES - build->traversal_bytes)
		used += build->traversal_bytes;
	else
		used = HULLDEBUG_MAX_MODEL_BYTES;
	if (used <= HULLDEBUG_MAX_MODEL_BYTES - build->temp_bytes)
		used += build->temp_bytes;
	else
		used = HULLDEBUG_MAX_MODEL_BYTES;
	if (bytes > HULLDEBUG_MAX_MODEL_BYTES - used)
	{
		HullDebug_Fail (build,
			"collision portalizer exceeded the 256 MiB model limit");
		return false;
	}
	build->temp_bytes += bytes;
	return true;
}

static void HullDebug_TempRelease (hullbuild_t *build, size_t bytes)
{
	if (build->temp_bytes >= bytes)
		build->temp_bytes -= bytes;
	else
		build->temp_bytes = 0;
}

static void *HullDebug_TempAlloc (hullbuild_t *build, size_t count,
	size_t elementsize, size_t *bytes, const char *failure)
{
	void *memory;

	*bytes = 0;
	if (build->failed)
		return NULL;
	if (!HullDebug_MultiplySize(count, elementsize, bytes))
	{
		HullDebug_Fail (build, "collision-diff allocation overflow");
		return NULL;
	}
	if (!*bytes)
		return NULL;
	if (!HullDebug_TempReserve(build, *bytes))
		return NULL;
	memory = malloc (*bytes);
	if (!memory)
	{
		HullDebug_TempRelease (build, *bytes);
		*bytes = 0;
		HullDebug_Fail (build, failure);
		return NULL;
	}
	return memory;
}

static void HullDebug_TempFree (hullbuild_t *build, void *memory, size_t bytes)
{
	free (memory);
	HullDebug_TempRelease (build, bytes);
}

static hulltreenode_t *HullDebug_NewTreeNode (hullbuild_t *build)
{
	hulltreenode_t *node;

	if (!HullDebug_TempReserve(build, sizeof(*node)))
		return NULL;
	node = (hulltreenode_t *)calloc (1, sizeof(*node));
	if (!node)
	{
		HullDebug_TempRelease (build, sizeof(*node));
		HullDebug_Fail (build, "out of memory while allocating portal tree");
		return NULL;
	}
	node->allnext = build->tree_nodes;
	build->tree_nodes = node;
	return node;
}

static hullportal_t *HullDebug_NewPortal (hullbuild_t *build)
{
	hullportal_t *portal;

	if (!HullDebug_TempReserve(build, sizeof(*portal)))
		return NULL;
	portal = (hullportal_t *)calloc (1, sizeof(*portal));
	if (!portal)
	{
		HullDebug_TempRelease (build, sizeof(*portal));
		HullDebug_Fail (build, "out of memory while allocating collision portal");
		return NULL;
	}
	portal->allnext = build->portals;
	build->portals = portal;
	++build->portals_allocated;
	return portal;
}

static qboolean HullDebug_PortalTakePoly (hullbuild_t *build,
	hullportal_t *portal, hullpoly_t *poly)
{
	size_t bytes;

	if (!HullDebug_MultiplySize(poly->numpoints,
		sizeof(*poly->points), &bytes))
	{
		HullDebug_Fail (build, "collision portal byte-size overflow");
		return false;
	}
	if (bytes > portal->polybytes &&
	    !HullDebug_TempReserve(build, bytes - portal->polybytes))
		return false;

	free (portal->poly.points);
	if (portal->polybytes > bytes)
		HullDebug_TempRelease (build, portal->polybytes - bytes);
	portal->poly = *poly;
	portal->polybytes = bytes;
	poly->points = NULL;
	poly->numpoints = 0;
	return true;
}

static void HullDebug_FreePortalizer (hullbuild_t *build)
{
	hullportal_t *portal, *nextportal;
	hulltreenode_t *node, *nextnode;

	for (portal = build->portals; portal; portal = nextportal)
	{
		nextportal = portal->allnext;
		free (portal->poly.points);
		free (portal);
	}
	for (node = build->tree_nodes; node; node = nextnode)
	{
		nextnode = node->allnext;
		free (node);
	}
	build->portals = NULL;
	build->tree_nodes = NULL;
	build->temp_bytes = 0;
}

static void HullDebug_LinkPortal (hullportal_t *portal,
	hulltreenode_t *node, int side)
{
	portal->nodes[side] = node;
	portal->next[side] = node->portals;
	node->portals = portal;
}

static int HullDebug_PortalSide (hullbuild_t *build,
	const hullportal_t *portal, const hulltreenode_t *node)
{
	if (portal->nodes[0] == node)
		return 0;
	if (portal->nodes[1] == node)
		return 1;
	HullDebug_Fail (build, "portal is not linked to its owning BSP node");
	return -1;
}

static hulltreenode_t *HullDebug_BuildTree (hullbuild_t *build,
	int clipnode_index, size_t depth)
{
	hulltreenode_t *node;
	mclipnode_t *clipnode;
	size_t active_index;

	if (build->failed || !HullDebug_TakeStep(build))
		return NULL;
	node = HullDebug_NewTreeNode (build);
	if (!node)
		return NULL;
	node->clipnode = clipnode_index;

	if (clipnode_index < 0)
	{
		node->contents = clipnode_index;
		if (HullDebug_IsBlocking(clipnode_index))
			++build->blocking_leaves;
		return node;
	}
	if (depth >= build->maxdepth)
	{
		HullDebug_Fail (build, "collision BSP exceeded maximum traversal depth");
		return node;
	}
	if (!HullDebug_NodePlane(build, clipnode_index,
		node->normal, &node->dist, &node->planenum))
		return node;

	active_index = (size_t)(clipnode_index - build->firstnode);
	if (build->active[active_index])
	{
		HullDebug_Fail (build, "cycle detected in collision BSP");
		return node;
	}
	build->active[active_index] = 1;
	++build->nodes_visited;
	clipnode = build->hull->clipnodes + clipnode_index;
	node->children[0] = HullDebug_BuildTree (build,
		clipnode->children[0], depth + 1);
	node->children[1] = HullDebug_BuildTree (build,
		clipnode->children[1], depth + 1);
	build->active[active_index] = 0;
	return node;
}

static qboolean HullDebug_CreateBoundsPortals (hullbuild_t *build,
	hulltreenode_t *root, hulltreenode_t *outside)
{
	int axis, side;

	for (axis = 0; axis < 3; ++axis)
	{
		int u = (axis + 1) % 3;
		int v = (axis + 2) % 3;
		for (side = 0; side < 2; ++side)
		{
			hullpoint_t points[4];
			hullpoly_t poly;
			hullportal_t *portal;
			double value = side ? build->base_extent : -build->base_extent;
			size_t i;
			static const double signs[4][2] =
			{
				{-1.0, -1.0},
				{ 1.0, -1.0},
				{ 1.0,  1.0},
				{-1.0,  1.0}
			};

			memset (points, 0, sizeof(points));
			for (i = 0; i < 4; ++i)
			{
				points[i].xyz[axis] = value;
				points[i].xyz[u] = signs[i][0] * build->base_extent;
				points[i].xyz[v] = signs[i][1] * build->base_extent;
			}
			if (!HullDebug_CopyPoly(points, 4, &poly))
			{
				HullDebug_Fail (build,
					"out of memory while creating bounds portal");
				return false;
			}
			portal = HullDebug_NewPortal (build);
			if (!portal || !HullDebug_PortalTakePoly(build, portal, &poly))
			{
				HullDebug_FreePoly (&poly);
				return false;
			}
			portal->normal[axis] = 1.0;
			portal->dist = value;
			portal->planenum = -1;
			portal->bounds_portal = true;
			if (side)
			{
				HullDebug_LinkPortal (portal, outside, 0);
				HullDebug_LinkPortal (portal, root, 1);
			}
			else
			{
				HullDebug_LinkPortal (portal, root, 0);
				HullDebug_LinkPortal (portal, outside, 1);
			}
		}
	}
	return true;
}

static hullportal_t *HullDebug_MakeNodePortal (hullbuild_t *build,
	hulltreenode_t *node)
{
	hullhalfspace_t plane;
	hullpoint_t *input = build->scratch[0];
	hullpoint_t *output = build->scratch[1];
	hullportal_t *bounding, *portal;
	hullpoly_t poly;
	size_t count = 4;

	plane.normal[0] = node->normal[0];
	plane.normal[1] = node->normal[1];
	plane.normal[2] = node->normal[2];
	plane.dist = node->dist;
	plane.planenum = node->planenum;
	plane.sibling = node->children[1]->clipnode;
	HullDebug_BaseWinding (&plane, build->base_extent * 2.0, input);

	for (bounding = node->portals;
	     bounding && count >= 3 && !build->failed;)
	{
		hullpoint_t *swap;
		double normal[3], dist;
		size_t clipped;
		int portal_side = HullDebug_PortalSide (build, bounding, node);

		if (!HullDebug_TakeStep(build))
			return NULL;
		if (portal_side < 0)
			return NULL;
		if (portal_side == 0)
		{
			normal[0] = -bounding->normal[0];
			normal[1] = -bounding->normal[1];
			normal[2] = -bounding->normal[2];
			dist = -bounding->dist;
		}
		else
		{
			normal[0] = bounding->normal[0];
			normal[1] = bounding->normal[1];
			normal[2] = bounding->normal[2];
			dist = bounding->dist;
		}
		clipped = HullDebug_ClipHalfspace (input, count, output,
			build->maxpoints, normal, dist);
		if (clipped == (size_t)-1)
		{
			HullDebug_Fail (build,
				"collision portal exceeded traversal point limit");
			return NULL;
		}
		count = clipped;
		swap = input;
		input = output;
		output = swap;
		bounding = bounding->next[portal_side];
	}

	count = HullDebug_CleanPolygon (input, count);
	if (count < 3 ||
	    HullDebug_PolygonArea(input, count) <= ON_EPSILON * ON_EPSILON)
		return NULL;
	if (!HullDebug_CopyPoly(input, count, &poly))
	{
		HullDebug_Fail (build, "out of memory while copying collision portal");
		return NULL;
	}
	portal = HullDebug_NewPortal (build);
	if (!portal || !HullDebug_PortalTakePoly(build, portal, &poly))
	{
		HullDebug_FreePoly (&poly);
		return NULL;
	}
	portal->normal[0] = node->normal[0];
	portal->normal[1] = node->normal[1];
	portal->normal[2] = node->normal[2];
	portal->dist = node->dist;
	portal->planenum = node->planenum;
	return portal;
}

static void HullDebug_MovePortalToChild (hullportal_t *portal,
	hulltreenode_t *child, int portal_side)
{
	portal->nodes[portal_side] = child;
	portal->next[portal_side] = child->portals;
	child->portals = portal;
}

static void HullDebug_SplitNodePortals (hullbuild_t *build,
	hulltreenode_t *node)
{
	hullportal_t *portal, *next, *nodeportal;

	if (build->failed || node->clipnode < 0 ||
	    !HullDebug_TakeStep(build))
		return;

	nodeportal = HullDebug_MakeNodePortal (build, node);
	portal = node->portals;
	node->portals = NULL;
	while (portal && !build->failed)
	{
		int portal_side = HullDebug_PortalSide (build, portal, node);
		qboolean hasfront = false, hasback = false;
		size_t i;

		if (!HullDebug_TakeStep(build))
			break;
		if (portal_side < 0)
			break;
		next = portal->next[portal_side];
		for (i = 0; i < portal->poly.numpoints; ++i)
		{
			double distance =
				HullDebug_Dot(portal->poly.points[i].xyz, node->normal) -
				node->dist;
			if (distance > ON_EPSILON)
				hasfront = true;
			else if (distance < -ON_EPSILON)
				hasback = true;
		}

		if (!hasfront && !hasback)
		{
			double into_node[3];
			double direction;
			int child_side;

			into_node[0] = portal->normal[0] *
				(portal_side == 0 ? 1.0 : -1.0);
			into_node[1] = portal->normal[1] *
				(portal_side == 0 ? 1.0 : -1.0);
			into_node[2] = portal->normal[2] *
				(portal_side == 0 ? 1.0 : -1.0);
			direction = HullDebug_Dot(into_node, node->normal);
			child_side = direction >= 0.0 ? 0 : 1;
			HullDebug_MovePortalToChild (portal,
				node->children[child_side], portal_side);
		}
		else if (!hasback)
		{
			HullDebug_MovePortalToChild (portal,
				node->children[0], portal_side);
		}
		else if (!hasfront)
		{
			HullDebug_MovePortalToChild (portal,
				node->children[1], portal_side);
		}
		else
		{
			hullpoly_t front, back;
			hullportal_t *backportal;
			hulltreenode_t *other = portal->nodes[portal_side ^ 1];

			if (!HullDebug_SplitPoly(build, &portal->poly,
				node->normal, node->dist, &front, &back))
				break;
			if (front.numpoints < 3 || back.numpoints < 3)
			{
				if (front.numpoints >= 3)
				{
					HullDebug_PortalTakePoly (build, portal, &front);
					HullDebug_MovePortalToChild (portal,
						node->children[0], portal_side);
				}
				else if (back.numpoints >= 3)
				{
					HullDebug_PortalTakePoly (build, portal, &back);
					HullDebug_MovePortalToChild (portal,
						node->children[1], portal_side);
				}
				HullDebug_FreePoly (&front);
				HullDebug_FreePoly (&back);
				portal = next;
				continue;
			}

			backportal = HullDebug_NewPortal (build);
			if (!backportal)
			{
				HullDebug_FreePoly (&front);
				HullDebug_FreePoly (&back);
				break;
			}
			backportal->normal[0] = portal->normal[0];
			backportal->normal[1] = portal->normal[1];
			backportal->normal[2] = portal->normal[2];
			backportal->dist = portal->dist;
			backportal->planenum = portal->planenum;
			backportal->bounds_portal = portal->bounds_portal;
			if (!HullDebug_PortalTakePoly(build, backportal, &back) ||
			    !HullDebug_PortalTakePoly(build, portal, &front))
			{
				HullDebug_FreePoly (&front);
				HullDebug_FreePoly (&back);
				break;
			}

			HullDebug_MovePortalToChild (portal,
				node->children[0], portal_side);
			HullDebug_LinkPortal (backportal, other, portal_side ^ 1);
			HullDebug_LinkPortal (backportal,
				node->children[1], portal_side);
		}
		portal = next;
	}

	if (nodeportal && !build->failed)
	{
		HullDebug_LinkPortal (nodeportal, node->children[0], 0);
		HullDebug_LinkPortal (nodeportal, node->children[1], 1);
	}
	if (!build->failed)
		HullDebug_SplitNodePortals (build, node->children[0]);
	if (!build->failed)
		HullDebug_SplitNodePortals (build, node->children[1]);
}

static void HullDebug_EmitPortals (hullbuild_t *build)
{
	hullportal_t *portal;

	for (portal = build->portals; portal && !build->failed;
	     portal = portal->allnext)
	{
		hullhalfspace_t boundary;
		qboolean front_blocking, back_blocking;
		int outside_contents;

		if (portal->bounds_portal || !portal->nodes[0] ||
		    !portal->nodes[1])
			continue;
		if (portal->nodes[0]->clipnode >= 0 ||
		    portal->nodes[1]->clipnode >= 0)
		{
			HullDebug_Fail (build,
				"portalization left a portal attached to an internal node");
			break;
		}

		front_blocking = HullDebug_IsBlocking(portal->nodes[0]->contents);
		back_blocking = HullDebug_IsBlocking(portal->nodes[1]->contents);
		if (front_blocking == back_blocking)
			continue;

		boundary.normal[0] = portal->normal[0];
		boundary.normal[1] = portal->normal[1];
		boundary.normal[2] = portal->normal[2];
		boundary.dist = portal->dist;
		boundary.planenum = portal->planenum;
		boundary.sibling = 0;
		if (front_blocking)
		{
			boundary.normal[0] = -boundary.normal[0];
			boundary.normal[1] = -boundary.normal[1];
			boundary.normal[2] = -boundary.normal[2];
			boundary.dist = -boundary.dist;
			outside_contents = portal->nodes[1]->contents;
		}
		else
		{
			outside_contents = portal->nodes[0]->contents;
		}
		HullDebug_AppendFace (build, &boundary,
			&portal->poly, outside_contents);
	}
}

static qboolean HullDebug_BaseExtent (hullbuild_t *build, double *extent)
{
	const qmodel_t *model = build->model;
	double maximum = HULLDEBUG_MIN_BASE_EXTENT;
	double value;
	int i, axis;

	for (axis = 0; axis < 3; ++axis)
	{
		value = fabs(model->mins[axis]);
		if (!isfinite(value))
			goto invalid_bounds;
		maximum = q_max(maximum, value);
		value = fabs(model->maxs[axis]);
		if (!isfinite(value))
			goto invalid_bounds;
		maximum = q_max(maximum, value);
		value = fabs(model->clipmins[axis]);
		if (!isfinite(value))
			goto invalid_bounds;
		maximum = q_max(maximum, value);
		value = fabs(model->clipmaxs[axis]);
		if (!isfinite(value))
			goto invalid_bounds;
		maximum = q_max(maximum, value);
	}
	for (i = 0; i < model->numplanes; ++i)
	{
		value = fabs(model->planes[i].dist);
		if (!isfinite(value))
			goto invalid_bounds;
		maximum = q_max(maximum, value);
	}

	*extent = maximum * 4.0 + 1024.0;
	if (!isfinite(*extent))
		goto invalid_bounds;
	return true;

invalid_bounds:
	HullDebug_Fail (build, "model has non-finite collision bounds");
	return false;
}

static void HullDebug_GetFallbackOffset (const hullbuild_t *build,
	double offset[3])
{
	static const double canonical_mins[3][3] =
	{
		{  0.0,   0.0,   0.0},
		{-16.0, -16.0, -24.0},
		{-32.0, -32.0, -24.0}
	};
	int axis;

	offset[0] = offset[1] = offset[2] = 0.0;
	if (build->hullnum <= 0 ||
	    build->hull->clipnodes != build->model->hulls[0].clipnodes)
		return;

	for (axis = 0; axis < 3; ++axis)
		offset[axis] =
			build->hull->clip_mins[axis] -
			canonical_mins[build->hullnum][axis];
}

static void HullDebug_SetFallbackOffset (hullbuild_t *build)
{
	HullDebug_GetFallbackOffset (build, build->draw_offset);
	if (build->hullnum <= 0 ||
	    build->hull->clipnodes != build->model->hulls[0].clipnodes)
		return;

	build->mesh->point_fallback = true;
	Con_DPrintf ("r_showhull: %s hull %d uses point-hull fallback"
		" (origin offset %.0f %.0f %.0f)\n",
		build->model->name, build->hullnum,
		build->draw_offset[0], build->draw_offset[1],
		build->draw_offset[2]);
}

static qboolean HullDebug_CanonicalPlane (const double inputnormal[3],
	double inputdist, double normal[3], double *dist)
{
	double length;
	int axis;

	normal[0] = inputnormal[0];
	normal[1] = inputnormal[1];
	normal[2] = inputnormal[2];
	length = sqrt (HullDebug_LengthSquared(normal));
	if (!(length > 0.000000001) || !isfinite(length) ||
	    !isfinite(inputdist))
		return false;
	normal[0] /= length;
	normal[1] /= length;
	normal[2] /= length;
	*dist = inputdist / length;

	for (axis = 0; axis < 3; ++axis)
	{
		if (fabs(normal[axis]) <= 0.000000001)
			continue;
		if (normal[axis] < 0.0)
		{
			normal[0] = -normal[0];
			normal[1] = -normal[1];
			normal[2] = -normal[2];
			*dist = -*dist;
		}
		break;
	}
	return true;
}

static void HullDebug_NormalKey (const double normal[3], int key[3])
{
	int axis;

	for (axis = 0; axis < 3; ++axis)
		key[axis] = (int)floor(normal[axis] /
			HULLDEBUG_NORMAL_HASH_CELL);
}

static size_t HullDebug_HashNormalKey (const int key[3], size_t mask)
{
	uint32_t hash = 2166136261u;
	int axis;

	for (axis = 0; axis < 3; ++axis)
	{
		hash ^= (uint32_t)key[axis];
		hash *= 16777619u;
	}
	return (size_t)hash & mask;
}

static void HullDebug_FreeRenderIndex (hullbuild_t *build,
	hullrenderindex_t *index)
{
	HullDebug_TempFree (build, index->buckets, index->bucketbytes);
	HullDebug_TempFree (build, index->planeheads, index->planeheadbytes);
	HullDebug_TempFree (build, index->planenext, index->planenextbytes);
	HullDebug_TempFree (build, index->surfacenext, index->surfacenextbytes);
	memset (index, 0, sizeof(*index));
}

static qboolean HullDebug_BuildRenderIndex (hullbuild_t *build,
	hullrenderindex_t *index)
{
	qmodel_t *model = build->model;
	size_t bucketcount = 64;
	int firstsurface, endsurface, surfaceindex, planenum;

	memset (index, 0, sizeof(*index));
	if (model->numplanes <= 0 || model->numsurfaces <= 0 ||
	    !model->planes || !model->surfaces || !model->surfedges ||
	    !model->edges || !model->vertexes)
	{
		HullDebug_Fail (build,
			"world model has no rendered-surface geometry");
		return false;
	}
	if (model->firstmodelsurface < 0 || model->nummodelsurfaces < 0 ||
	    model->firstmodelsurface > model->numsurfaces ||
	    model->nummodelsurfaces >
		model->numsurfaces - model->firstmodelsurface)
	{
		HullDebug_Fail (build,
			"world surface range is outside the model");
		return false;
	}

	index->numplanes = (size_t)model->numplanes;
	index->numsurfaces = (size_t)model->numsurfaces;
	if (index->numplanes > (size_t)-1 / 2)
	{
		HullDebug_Fail (build,
			"rendered-plane hash capacity overflow");
		return false;
	}
	while (bucketcount < index->numplanes * 2)
	{
		if (bucketcount > (size_t)-1 / 2)
		{
			HullDebug_Fail (build,
				"rendered-plane hash capacity overflow");
			return false;
		}
		bucketcount *= 2;
	}
	index->numbuckets = bucketcount;
	index->buckets = (int *)HullDebug_TempAlloc (build, bucketcount,
		sizeof(*index->buckets), &index->bucketbytes,
		"out of memory while indexing rendered planes");
	index->planeheads = (int *)HullDebug_TempAlloc (build,
		index->numplanes, sizeof(*index->planeheads),
		&index->planeheadbytes,
		"out of memory while indexing rendered surfaces");
	index->planenext = (int *)HullDebug_TempAlloc (build,
		index->numplanes, sizeof(*index->planenext),
		&index->planenextbytes,
		"out of memory while linking rendered planes");
	index->surfacenext = (int *)HullDebug_TempAlloc (build,
		index->numsurfaces, sizeof(*index->surfacenext),
		&index->surfacenextbytes,
		"out of memory while linking rendered surfaces");
	if (build->failed)
	{
		HullDebug_FreeRenderIndex (build, index);
		return false;
	}
	memset (index->buckets, 0xff, index->bucketbytes);
	memset (index->planeheads, 0xff, index->planeheadbytes);
	memset (index->planenext, 0xff, index->planenextbytes);
	memset (index->surfacenext, 0xff, index->surfacenextbytes);

	firstsurface = model->firstmodelsurface;
	endsurface = firstsurface + model->nummodelsurfaces;
	for (surfaceindex = firstsurface;
	     surfaceindex < endsurface; ++surfaceindex)
	{
		msurface_t *surface = &model->surfaces[surfaceindex];

		if (!surface->plane || surface->numedges < 3 ||
		    (surface->flags & SURF_DRAWTURB))
			continue;
		planenum = (int)(surface->plane - model->planes);
		if (planenum < 0 || planenum >= model->numplanes)
		{
			HullDebug_Fail (build,
				"rendered surface plane is outside the model");
			break;
		}
		index->surfacenext[surfaceindex] = index->planeheads[planenum];
		index->planeheads[planenum] = surfaceindex;
		++index->indexedsurfaces;
	}

	for (planenum = 0; planenum < model->numplanes && !build->failed;
	     ++planenum)
	{
		double inputnormal[3], normal[3], dist;
		int key[3];
		size_t bucket;

		if (index->planeheads[planenum] < 0)
			continue;
		inputnormal[0] = model->planes[planenum].normal[0];
		inputnormal[1] = model->planes[planenum].normal[1];
		inputnormal[2] = model->planes[planenum].normal[2];
		if (!HullDebug_CanonicalPlane(inputnormal,
			model->planes[planenum].dist, normal, &dist))
		{
			HullDebug_Fail (build,
				"rendered surface has an invalid plane");
			break;
		}
		HullDebug_NormalKey (normal, key);
		bucket = HullDebug_HashNormalKey(key, bucketcount - 1);
		index->planenext[planenum] = index->buckets[bucket];
		index->buckets[bucket] = planenum;
		++index->indexedplanes;
	}

	if (build->failed)
	{
		HullDebug_FreeRenderIndex (build, index);
		return false;
	}
	return true;
}

static hullpoly2_t *HullDebug_NewPoly2 (hullbuild_t *build, size_t capacity)
{
	hullpoly2_t *poly;
	size_t structbytes, pointbytes;

	poly = (hullpoly2_t *)HullDebug_TempAlloc (build, 1, sizeof(*poly),
		&structbytes, "out of memory while allocating collision-diff polygon");
	if (!poly)
		return NULL;
	memset (poly, 0, sizeof(*poly));
	poly->points = (hullpoint2_t *)HullDebug_TempAlloc (build, capacity,
		sizeof(*poly->points), &pointbytes,
		"out of memory while allocating collision-diff winding");
	if (!poly->points)
	{
		HullDebug_TempFree (build, poly, structbytes);
		return NULL;
	}
	poly->capacity = capacity;
	return poly;
}

static void HullDebug_FreePoly2 (hullbuild_t *build, hullpoly2_t *poly)
{
	size_t pointbytes;

	if (!poly)
		return;
	if (!HullDebug_MultiplySize(poly->capacity,
		sizeof(*poly->points), &pointbytes))
		pointbytes = 0;
	HullDebug_TempFree (build, poly->points, pointbytes);
	HullDebug_TempFree (build, poly, sizeof(*poly));
}

static void HullDebug_FreePoly2List (hullbuild_t *build, hullpoly2_t *list)
{
	while (list)
	{
		hullpoly2_t *next = list->next;
		HullDebug_FreePoly2 (build, list);
		list = next;
	}
}

static double HullDebug_Distance2Squared (const hullpoint2_t *a,
	const hullpoint2_t *b)
{
	double x = a->xy[0] - b->xy[0];
	double y = a->xy[1] - b->xy[1];

	return x * x + y * y;
}

static size_t HullDebug_CleanPolygon2 (hullpoint2_t *points, size_t count)
{
	const double duplicate_epsilon2 = DIST_EPSILON * DIST_EPSILON;
	size_t i, out;
	qboolean changed;

	if (count < 3)
		return 0;

	out = 0;
	for (i = 0; i < count; ++i)
	{
		size_t previous = out ? out - 1 : count - 1;

		if (HullDebug_Distance2Squared(&points[i],
			&points[previous]) <= duplicate_epsilon2)
			continue;
		points[out++] = points[i];
	}
	count = out;
	if (count > 2 &&
	    HullDebug_Distance2Squared(&points[0],
		&points[count - 1]) <= duplicate_epsilon2)
		--count;

	do
	{
		changed = false;
		for (i = 0; count >= 3 && i < count; ++i)
		{
			size_t previous = (i + count - 1) % count;
			size_t next = (i + 1) % count;
			double ax = points[i].xy[0] - points[previous].xy[0];
			double ay = points[i].xy[1] - points[previous].xy[1];
			double bx = points[next].xy[0] - points[i].xy[0];
			double by = points[next].xy[1] - points[i].xy[1];
			double cross = ax * by - ay * bx;
			double scale = (ax * ax + ay * ay) * (bx * bx + by * by);

			if (scale > 0.0 && cross * cross > scale * 1e-12)
				continue;
			memmove (&points[i], &points[i + 1],
				(count - i - 1) * sizeof(*points));
			--count;
			changed = true;
			break;
		}
	} while (changed);

	return count >= 3 ? count : 0;
}

static double HullDebug_SignedArea2 (const hullpoint2_t *points, size_t count)
{
	double area = 0.0;
	size_t i;

	for (i = 0; i < count; ++i)
		area += points[i].xy[0] * points[(i + 1) % count].xy[1] -
			points[(i + 1) % count].xy[0] * points[i].xy[1];
	return area * 0.5;
}

static void HullDebug_ReversePolygon2 (hullpoint2_t *points, size_t count)
{
	size_t i;

	for (i = 0; i < count / 2; ++i)
	{
		hullpoint2_t swap = points[i];
		points[i] = points[count - i - 1];
		points[count - i - 1] = swap;
	}
}

static double HullDebug_EdgeDistance2 (const hullpoint2_t *a,
	const hullpoint2_t *b, const hullpoint2_t *point)
{
	return (b->xy[0] - a->xy[0]) * (point->xy[1] - a->xy[1]) -
		(b->xy[1] - a->xy[1]) * (point->xy[0] - a->xy[0]);
}

static double HullDebug_EdgeEpsilon2 (const hullpoint2_t *a,
	const hullpoint2_t *b)
{
	double x = b->xy[0] - a->xy[0];
	double y = b->xy[1] - a->xy[1];

	return ON_EPSILON * sqrt(x * x + y * y);
}

static hullpoly2_t *HullDebug_ClipPolygon2 (hullbuild_t *build,
	const hullpoly2_t *input, const hullpoint2_t *a,
	const hullpoint2_t *b, qboolean keepinside)
{
	hullpoly2_t *output;
	double edgeepsilon = HullDebug_EdgeEpsilon2(a, b);
	size_t i;

	if (input->numpoints > (size_t)-1 - 2)
	{
		HullDebug_Fail (build,
			"collision-diff polygon capacity overflow");
		return NULL;
	}
	output = HullDebug_NewPoly2 (build, input->numpoints + 2);
	if (!output)
		return NULL;

	for (i = 0; i < input->numpoints; ++i)
	{
		const hullpoint2_t *p = &input->points[i];
		const hullpoint2_t *q =
			&input->points[(i + 1) % input->numpoints];
		double dp = HullDebug_EdgeDistance2(a, b, p) + edgeepsilon;
		double dq = HullDebug_EdgeDistance2(a, b, q) + edgeepsilon;
		qboolean pinside = keepinside ?
			dp >= 0.0 : dp <= 0.0;
		qboolean qinside = keepinside ?
			dq >= 0.0 : dq <= 0.0;

		if (pinside)
			output->points[output->numpoints++] = *p;
		if (pinside != qinside)
		{
			double denominator = dp - dq;
			double fraction;
			hullpoint2_t point;

			if (fabs(denominator) < 1e-20)
				continue;
			fraction = dp / denominator;
			point.xy[0] = p->xy[0] +
				fraction * (q->xy[0] - p->xy[0]);
			point.xy[1] = p->xy[1] +
				fraction * (q->xy[1] - p->xy[1]);
			output->points[output->numpoints++] = point;
		}
	}

	output->numpoints = HullDebug_CleanPolygon2(output->points,
		output->numpoints);
	if (output->numpoints < 3 ||
	    fabs(HullDebug_SignedArea2(output->points,
		output->numpoints)) <= ON_EPSILON * ON_EPSILON)
	{
		HullDebug_FreePoly2 (build, output);
		return NULL;
	}
	return output;
}

static qboolean HullDebug_SubtractConvex (hullbuild_t *build,
	hullpoly2_t *subject, const hullpoly2_t *cutter,
	hullpoly2_t **outputhead, hullpoly2_t **outputtail)
{
	hullpoly2_t *remaining = subject;
	size_t edge;

	*outputhead = *outputtail = NULL;
	for (edge = 0; edge < cutter->numpoints && remaining; ++edge)
	{
		const hullpoint2_t *a = &cutter->points[edge];
		const hullpoint2_t *b =
			&cutter->points[(edge + 1) % cutter->numpoints];
		hullpoly2_t *outside, *inside;

		if (!HullDebug_TakeStep(build))
			break;
		outside = HullDebug_ClipPolygon2 (build, remaining,
			a, b, false);
		if (build->failed)
			break;
		inside = HullDebug_ClipPolygon2 (build, remaining,
			a, b, true);
		if (build->failed)
		{
			HullDebug_FreePoly2 (build, outside);
			break;
		}
		HullDebug_FreePoly2 (build, remaining);
		remaining = inside;
		if (outside)
		{
			if (*outputtail)
				(*outputtail)->next = outside;
			else
				*outputhead = outside;
			*outputtail = outside;
		}
	}
	HullDebug_FreePoly2 (build, remaining);
	if (build->failed)
	{
		HullDebug_FreePoly2List (build, *outputhead);
		*outputhead = *outputtail = NULL;
		return false;
	}
	return true;
}

static qboolean HullDebug_SubtractFromList (hullbuild_t *build,
	hullpoly2_t **fragments, const hullpoly2_t *cutter)
{
	hullpoly2_t *input = *fragments;
	hullpoly2_t *outputhead = NULL, *outputtail = NULL;

	*fragments = NULL;
	while (input && !build->failed)
	{
		hullpoly2_t *next = input->next;
		hullpoly2_t *pieces, *piecetail;

		input->next = NULL;
		if (!HullDebug_SubtractConvex(build, input, cutter,
			&pieces, &piecetail))
		{
			input = next;
			break;
		}
		if (pieces)
		{
			if (outputtail)
				outputtail->next = pieces;
			else
				outputhead = pieces;
			outputtail = piecetail;
		}
		input = next;
	}
	HullDebug_FreePoly2List (build, input);
	if (build->failed)
	{
		HullDebug_FreePoly2List (build, outputhead);
		return false;
	}
	*fragments = outputhead;
	return true;
}

static qboolean HullDebug_Polygon2IsConvex (const hullpoly2_t *poly)
{
	size_t i;

	for (i = 0; i < poly->numpoints; ++i)
	{
		const hullpoint2_t *a = &poly->points[i];
		const hullpoint2_t *b =
			&poly->points[(i + 1) % poly->numpoints];
		const hullpoint2_t *c =
			&poly->points[(i + 2) % poly->numpoints];

		if (HullDebug_EdgeDistance2(a, b, c) <
		    -HullDebug_EdgeEpsilon2(a, b))
			return false;
	}
	return true;
}

static qboolean HullDebug_PointInTriangle2 (const hullpoint2_t *point,
	const hullpoint2_t *a, const hullpoint2_t *b,
	const hullpoint2_t *c)
{
	return HullDebug_EdgeDistance2(a, b, point) >=
			-HullDebug_EdgeEpsilon2(a, b) &&
		HullDebug_EdgeDistance2(b, c, point) >=
			-HullDebug_EdgeEpsilon2(b, c) &&
		HullDebug_EdgeDistance2(c, a, point) >=
			-HullDebug_EdgeEpsilon2(c, a);
}

static qboolean HullDebug_SubtractRenderPolygon (hullbuild_t *build,
	hullpoly2_t **fragments, hullpoly2_t *cutter)
{
	if (HullDebug_Polygon2IsConvex(cutter))
		return HullDebug_SubtractFromList(build, fragments, cutter);

	while (cutter->numpoints > 3 && *fragments && !build->failed)
	{
		size_t ear;
		qboolean found = false;

		for (ear = 0; ear < cutter->numpoints; ++ear)
		{
			size_t previous =
				(ear + cutter->numpoints - 1) % cutter->numpoints;
			size_t next = (ear + 1) % cutter->numpoints;
			const hullpoint2_t *a = &cutter->points[previous];
			const hullpoint2_t *b = &cutter->points[ear];
			const hullpoint2_t *c = &cutter->points[next];
			hullpoint2_t trianglepoints[3];
			hullpoly2_t triangle;
			size_t test;

			if (HullDebug_EdgeDistance2(a, b, c) <=
			    HullDebug_EdgeEpsilon2(a, b))
				continue;
			for (test = 0; test < cutter->numpoints; ++test)
			{
				if (test == previous || test == ear || test == next)
					continue;
				if (HullDebug_PointInTriangle2(
					&cutter->points[test], a, b, c))
					break;
			}
			if (test != cutter->numpoints)
				continue;

			memset (&triangle, 0, sizeof(triangle));
			trianglepoints[0] = *a;
			trianglepoints[1] = *b;
			trianglepoints[2] = *c;
			triangle.points = trianglepoints;
			triangle.numpoints = 3;
			triangle.capacity = 3;
			if (!HullDebug_SubtractFromList(build,
				fragments, &triangle))
				return false;
			memmove (&cutter->points[ear],
				&cutter->points[ear + 1],
				(cutter->numpoints - ear - 1) *
					sizeof(*cutter->points));
			--cutter->numpoints;
			found = true;
			break;
		}
		if (!found)
		{
			HullDebug_Fail (build,
				"rendered BSP surface is not a simple polygon");
			return false;
		}
	}

	if (*fragments && cutter->numpoints == 3)
		return HullDebug_SubtractFromList(build, fragments, cutter);
	return !build->failed;
}

static void HullDebug_MakeBasis (const vec3_t normal,
	double right[3], double up[3])
{
	double axis[3] = {0.0, 0.0, 0.0};
	double planenormal[3], length;
	int minor = 0;

	planenormal[0] = normal[0];
	planenormal[1] = normal[1];
	planenormal[2] = normal[2];
	if (fabs(planenormal[1]) < fabs(planenormal[minor]))
		minor = 1;
	if (fabs(planenormal[2]) < fabs(planenormal[minor]))
		minor = 2;
	axis[minor] = 1.0;
	HullDebug_Cross (axis, planenormal, right);
	length = sqrt (HullDebug_LengthSquared(right));
	right[0] /= length;
	right[1] /= length;
	right[2] /= length;
	HullDebug_Cross (planenormal, right, up);
}

static double HullDebug_SurfaceDistance (hullbuild_t *build,
	const hullmeshface_t *face)
{
	double distance = face->dist;
	int axis;

	if (build->mesh->point_fallback)
	{
		double offset[3];

		HullDebug_GetFallbackOffset (build, offset);
		return distance -
			face->normal[0] * offset[0] -
			face->normal[1] * offset[1] -
			face->normal[2] * offset[2];
	}

	for (axis = 0; axis < 3; ++axis)
	{
		double extent = face->normal[axis] >= 0.0 ?
			build->hull->clip_mins[axis] :
			build->hull->clip_maxs[axis];
		distance += face->normal[axis] * extent;
	}
	return distance;
}

static hullpoly2_t *HullDebug_ProjectCollisionFace (hullbuild_t *build,
	const hullmeshface_t *face, double surfacedist,
	const double right[3], const double up[3],
	double mins[3], double maxs[3])
{
	hullpoly2_t *poly;
	double shift = surfacedist - face->dist;
	size_t i;
	int axis;

	poly = HullDebug_NewPoly2 (build, face->numverts);
	if (!poly)
		return NULL;
	for (axis = 0; axis < 3; ++axis)
	{
		mins[axis] = DBL_MAX;
		maxs[axis] = -DBL_MAX;
	}

	for (i = 0; i < face->numverts; ++i)
	{
		const vec3_t *raw =
			&build->mesh->verts[face->firstvert + i];
		double point[3];

		for (axis = 0; axis < 3; ++axis)
		{
			point[axis] = (*raw)[axis] + shift * face->normal[axis];
			mins[axis] = q_min(mins[axis], point[axis]);
			maxs[axis] = q_max(maxs[axis], point[axis]);
		}
		poly->points[i].xy[0] =
			point[0] * right[0] +
			point[1] * right[1] +
			point[2] * right[2];
		poly->points[i].xy[1] =
			point[0] * up[0] +
			point[1] * up[1] +
			point[2] * up[2];
	}
	poly->numpoints = HullDebug_CleanPolygon2(poly->points,
		face->numverts);
	if (poly->numpoints < 3 ||
	    fabs(HullDebug_SignedArea2(poly->points,
		poly->numpoints)) <= ON_EPSILON * ON_EPSILON)
	{
		HullDebug_FreePoly2 (build, poly);
		return NULL;
	}
	return poly;
}

static qboolean HullDebug_BoundsOverlap (const double mins[3],
	const double maxs[3], const msurface_t *surface)
{
	int axis;

	for (axis = 0; axis < 3; ++axis)
	{
		if (maxs[axis] < surface->mins[axis] - ON_EPSILON ||
		    mins[axis] > surface->maxs[axis] + ON_EPSILON)
			return false;
	}
	return true;
}

static hullpoly2_t *HullDebug_ProjectRenderSurface (hullbuild_t *build,
	const msurface_t *surface, const double right[3], const double up[3])
{
	qmodel_t *model = build->model;
	hullpoly2_t *poly;
	double area;
	int i;

	if (surface->firstedge < 0 || surface->numedges < 3 ||
	    surface->firstedge > model->numsurfedges ||
	    surface->numedges > model->numsurfedges - surface->firstedge)
	{
		HullDebug_Fail (build,
			"rendered surface edge range is outside the model");
		return NULL;
	}
	poly = HullDebug_NewPoly2 (build, (size_t)surface->numedges);
	if (!poly)
		return NULL;

	for (i = 0; i < surface->numedges; ++i)
	{
		int surfedge = model->surfedges[surface->firstedge + i];
		unsigned int vertexindex;
		const vec3_t *point;

		if (surfedge == INT_MIN || surfedge >= model->numedges ||
		    -surfedge >= model->numedges)
		{
			HullDebug_Fail (build,
				"rendered surface edge index is outside the model");
			break;
		}
		if (surfedge >= 0)
			vertexindex = model->edges[surfedge].v[0];
		else
			vertexindex = model->edges[-surfedge].v[1];
		if (vertexindex >= (unsigned int)model->numvertexes)
		{
			HullDebug_Fail (build,
				"rendered surface vertex index is outside the model");
			break;
		}
		point = &model->vertexes[vertexindex].position;
		poly->points[poly->numpoints].xy[0] =
			(*point)[0] * right[0] +
			(*point)[1] * right[1] +
			(*point)[2] * right[2];
		poly->points[poly->numpoints].xy[1] =
			(*point)[0] * up[0] +
			(*point)[1] * up[1] +
			(*point)[2] * up[2];
		++poly->numpoints;
	}
	if (build->failed)
	{
		HullDebug_FreePoly2 (build, poly);
		return NULL;
	}

	poly->numpoints = HullDebug_CleanPolygon2(poly->points,
		poly->numpoints);
	area = HullDebug_SignedArea2(poly->points, poly->numpoints);
	if (poly->numpoints < 3 ||
	    fabs(area) <= ON_EPSILON * ON_EPSILON)
	{
		HullDebug_FreePoly2 (build, poly);
		return NULL;
	}
	if (area < 0.0)
		HullDebug_ReversePolygon2 (poly->points, poly->numpoints);

	return poly;
}

static qboolean HullDebug_AppendDiffFace (hullbuild_t *build,
	const hullmeshface_t *rawface, const hullpoly2_t *poly,
	double surfacedist, const double right[3], const double up[3])
{
	hullmeshface_t *face;
	size_t firstvert, i;
	double area;

	area = fabs(HullDebug_SignedArea2(poly->points, poly->numpoints));
	if (!isfinite(area) || area > FLT_MAX)
	{
		HullDebug_Fail (build,
			"collision-diff produced an invalid polygon area");
		return false;
	}
	if (build->mesh->numdiffverts > UINT32_MAX ||
	    poly->numpoints > UINT32_MAX - build->mesh->numdiffverts)
	{
		HullDebug_Fail (build,
			"collision-diff mesh exceeded 32-bit vertex indices");
		return false;
	}
	if (!HullDebug_Reserve(build, (void **)&build->mesh->diffverts,
		&build->mesh->diffvertcapacity,
		build->mesh->numdiffverts + poly->numpoints,
		sizeof(*build->mesh->diffverts)) ||
	    !HullDebug_Reserve(build, (void **)&build->mesh->difffaces,
		&build->mesh->difffacecapacity,
		build->mesh->numdifffaces + 1,
			sizeof(*build->mesh->difffaces)))
		return false;

	firstvert = build->mesh->numdiffverts;
	face = &build->mesh->difffaces[build->mesh->numdifffaces];
	memset (face, 0, sizeof(*face));
	face->firstvert = (uint32_t)firstvert;
	face->numverts = (uint32_t)poly->numpoints;
	face->sourceface = rawface->sourceface;
	face->coverage_matches = rawface->coverage_matches;
	face->planenum = rawface->planenum;
	face->outside_contents = rawface->outside_contents;
	VectorCopy (rawface->normal, face->normal);
	face->dist = (float)surfacedist;
	face->raw_area = rawface->raw_area;
	face->residual_area = (float)area;
	face->mins[0] = face->mins[1] = face->mins[2] = FLT_MAX;
	face->maxs[0] = face->maxs[1] = face->maxs[2] = -FLT_MAX;

	for (i = 0; i < poly->numpoints; ++i)
	{
		vec3_t *vertex =
			&build->mesh->diffverts[build->mesh->numdiffverts++];
		int axis;

		for (axis = 0; axis < 3; ++axis)
		{
			(*vertex)[axis] = (float)(
				right[axis] * poly->points[i].xy[0] +
				up[axis] * poly->points[i].xy[1] +
				rawface->normal[axis] * surfacedist);
			face->mins[axis] =
				q_min(face->mins[axis], (*vertex)[axis]);
			face->maxs[axis] =
				q_max(face->maxs[axis], (*vertex)[axis]);
		}
	}
	++build->mesh->numdifffaces;
	return true;
}

static qboolean HullDebug_AllocateTraversal (hullbuild_t *build)
{
	size_t activebytes, scratchbytes, totalbytes;

	if (!HullDebug_MultiplySize(build->numnodes,
		sizeof(*build->active), &activebytes) ||
	    !HullDebug_MultiplySize(build->maxpoints,
		sizeof(*build->scratch[0]), &scratchbytes) ||
	    scratchbytes > ((size_t)-1 - activebytes) / 2)
	{
		HullDebug_Fail (build, "collision traversal allocation overflow");
		return false;
	}
	totalbytes = activebytes + scratchbytes * 2;
	if (totalbytes > HULLDEBUG_MAX_MODEL_BYTES)
	{
		HullDebug_Fail (build,
			"collision traversal exceeded the 256 MiB temporary limit");
		return false;
	}

	build->active = (byte *)calloc (build->numnodes, sizeof(*build->active));
	build->scratch[0] = (hullpoint_t *)malloc (scratchbytes);
	build->scratch[1] = (hullpoint_t *)malloc (scratchbytes);
	if ((build->numnodes && !build->active) ||
	    !build->scratch[0] || !build->scratch[1])
	{
		HullDebug_Fail (build,
			"out of memory while allocating collision traversal");
		return false;
	}
	build->traversal_bytes = totalbytes;
	return true;
}

static void HullDebug_FreeTraversal (hullbuild_t *build)
{
	free (build->active);
	free (build->scratch[0]);
	free (build->scratch[1]);
	build->active = NULL;
	build->scratch[0] = NULL;
	build->scratch[1] = NULL;
	build->traversal_bytes = 0;
}

static hullmodelcache_t *HullDebug_ModelCache (qmodel_t *model)
{
	hullmodelcache_t *cache;

	if (model->hull_debug_cache == &hull_debug_failed_cache)
		return NULL;
	if (model->hull_debug_cache)
		return (hullmodelcache_t *)model->hull_debug_cache;

	cache = (hullmodelcache_t *)calloc (1, sizeof(*cache));
	if (!cache)
	{
		Con_Warning ("r_showhull: unable to allocate cache for %s\n",
			model->name);
		model->hull_debug_cache = &hull_debug_failed_cache;
		return NULL;
	}
	model->hull_debug_cache = cache;
	return cache;
}

static hullmesh_t *HullDebug_Build (qmodel_t *model, int hullnum)
{
	hullmodelcache_t *cache;
	hullmesh_t *mesh;
	hullbuild_t build;
	hulltreenode_t *root = NULL, *outside = NULL;
	double start, elapsed;
	size_t scaledsteps;

	if (!model || model->type != mod_brush || hullnum < 0 || hullnum > 2)
		return NULL;

	cache = HullDebug_ModelCache (model);
	if (!cache)
		return NULL;
	mesh = &cache->meshes[hullnum];
	if (mesh->built)
		return mesh;
	if (mesh->failed)
		return NULL;

	memset (&build, 0, sizeof(build));
	build.model = model;
	build.hull = &model->hulls[hullnum];
	build.cache = cache;
	build.mesh = mesh;
	build.hullnum = hullnum;
	build.firstnode = build.hull->firstclipnode;
	build.lastnode = build.hull->lastclipnode;

	if (build.firstnode < 0)
	{
		mesh->built = true;
		return mesh;
	}
	if (!build.hull->clipnodes || !build.hull->planes ||
	    !model->planes || model->numplanes <= 0 ||
	    build.lastnode < build.firstnode)
	{
		mesh->failed = true;
		Con_Warning ("r_showhull: %s hull %d has no valid clip BSP\n",
			model->name, hullnum);
		return NULL;
	}

	build.numnodes = (size_t)(build.lastnode - build.firstnode) + 1;
	build.maxdepth = q_min(build.numnodes + 1,
		(size_t)HULLDEBUG_MAX_DEPTH);
	build.maxpoints = build.maxdepth + 8;
	if (build.numnodes > ((size_t)-1 - 1024) / 64)
		scaledsteps = HULLDEBUG_MAX_WORK_STEPS;
	else
		scaledsteps = build.numnodes * 64 + 1024;
	build.maxsteps = q_max((size_t)HULLDEBUG_MIN_WORK_STEPS,
		q_min(scaledsteps, (size_t)HULLDEBUG_MAX_WORK_STEPS));
	HullDebug_SetFallbackOffset (&build);

	start = Sys_DoubleTime ();
	if (HullDebug_BaseExtent(&build, &build.base_extent) &&
	    HullDebug_AllocateTraversal(&build))
	{
		root = HullDebug_BuildTree (&build, build.firstnode, 0);
		if (!build.failed)
			outside = HullDebug_NewTreeNode (&build);
		if (outside)
		{
			outside->clipnode = CONTENTS_SOLID;
			outside->contents = CONTENTS_SOLID;
		}
		if (root && outside &&
		    HullDebug_CreateBoundsPortals(&build, root, outside))
		{
			HullDebug_SplitNodePortals (&build, root);
			if (!build.failed)
				HullDebug_EmitPortals (&build);
		}
	}
	HullDebug_FreePortalizer (&build);
	HullDebug_FreeTraversal (&build);
	elapsed = Sys_DoubleTime () - start;

	if (build.failed)
	{
		HullDebug_FreeMeshStorage (cache, mesh);
		mesh->failed = true;
		Con_Warning ("r_showhull: disabled %s hull %d: %s\n",
			model->name, hullnum,
			build.failure[0] ? build.failure : "mesh build failed");
		return NULL;
	}

	mesh->built = true;
	Con_DPrintf ("r_showhull: built %s hull %d: %lu node visits,"
		" %lu blocking leaves, %lu portals, %lu faces, %lu vertices,"
		" %.2f MiB, %.1f ms\n",
		model->name, hullnum,
		(unsigned long)build.nodes_visited,
		(unsigned long)build.blocking_leaves,
		(unsigned long)build.portals_allocated,
		(unsigned long)mesh->numfaces,
		(unsigned long)mesh->numverts,
		mesh->allocated_bytes / (1024.0 * 1024.0),
		elapsed * 1000.0);
	return mesh;
}

static double HullDebug_PlaneMatchEpsilon (double a, double b)
{
	return ON_EPSILON +
		4.0 * FLT_EPSILON * q_max(fabs(a), fabs(b));
}

static qboolean HullDebug_SubtractRenderedSurfaces (hullbuild_t *build,
	const hullrenderindex_t *index, const hullmeshface_t *face,
	double surfacedist, const double right[3], const double up[3],
	const double mins[3], const double maxs[3],
	hullpoly2_t **fragments, size_t *matchedsurfaces)
{
	double inputnormal[3], targetnormal[3], targetdist;
	int basekey[3], x, y, z;

	inputnormal[0] = face->normal[0];
	inputnormal[1] = face->normal[1];
	inputnormal[2] = face->normal[2];
	if (!HullDebug_CanonicalPlane(inputnormal, surfacedist,
		targetnormal, &targetdist))
	{
		HullDebug_Fail (build,
			"collision-diff face has an invalid surface plane");
		return false;
	}
	HullDebug_NormalKey (targetnormal, basekey);

	for (x = -1; x <= 1 && *fragments && !build->failed; ++x)
	for (y = -1; y <= 1 && *fragments && !build->failed; ++y)
	for (z = -1; z <= 1 && *fragments && !build->failed; ++z)
	{
		int querykey[3] =
			{basekey[0] + x, basekey[1] + y, basekey[2] + z};
		size_t bucket = HullDebug_HashNormalKey(querykey,
			index->numbuckets - 1);
		int planenum;

		for (planenum = index->buckets[bucket];
		     planenum >= 0 && *fragments && !build->failed;
		     planenum = index->planenext[planenum])
		{
			mplane_t *plane = &build->model->planes[planenum];
			double planenormalinput[3], planenormal[3], planedist;
			int planekey[3], surfaceindex;

			planenormalinput[0] = plane->normal[0];
			planenormalinput[1] = plane->normal[1];
			planenormalinput[2] = plane->normal[2];
			if (!HullDebug_CanonicalPlane(planenormalinput,
				plane->dist, planenormal, &planedist))
			{
				HullDebug_Fail (build,
					"rendered plane index contains an invalid plane");
				break;
			}
			HullDebug_NormalKey (planenormal, planekey);
			if (planekey[0] != querykey[0] ||
			    planekey[1] != querykey[1] ||
			    planekey[2] != querykey[2])
				continue;
			if (HullDebug_Dot(targetnormal, planenormal) <
			    1.0 - HULLDEBUG_NORMAL_DOT_EPSILON ||
			    fabs(targetdist - planedist) >
			    HullDebug_PlaneMatchEpsilon(targetdist, planedist))
				continue;

			for (surfaceindex = index->planeheads[planenum];
			     surfaceindex >= 0 && *fragments && !build->failed;
			     surfaceindex = index->surfacenext[surfaceindex])
			{
				msurface_t *surface =
					&build->model->surfaces[surfaceindex];
				hullpoly2_t *cutter;

				if (!HullDebug_BoundsOverlap(mins, maxs, surface))
					continue;
				if (!HullDebug_TakeStep(build))
					break;
				cutter = HullDebug_ProjectRenderSurface (build,
					surface, right, up);
				if (!cutter)
					continue;
				++*matchedsurfaces;
				HullDebug_SubtractRenderPolygon (build,
					fragments, cutter);
				HullDebug_FreePoly2 (build, cutter);
			}
		}
	}
	return !build->failed;
}

static qboolean HullDebug_BuildDiff (qmodel_t *model, int hullnum,
	hullmesh_t *mesh)
{
	hullmodelcache_t *cache =
		(hullmodelcache_t *)model->hull_debug_cache;
	hullrenderindex_t index;
	hullbuild_t build;
	size_t faceindex, matchedsurfaces = 0, beforebytes;
	size_t indexedplanes = 0, indexedsurfaces = 0;
	double start, elapsed;

	if (mesh->diff_built)
		return true;
	if (mesh->diff_failed)
		return false;

	memset (&build, 0, sizeof(build));
	memset (&index, 0, sizeof(index));
	build.model = model;
	build.hull = &model->hulls[hullnum];
	build.cache = cache;
	build.mesh = mesh;
	build.hullnum = hullnum;
	build.maxsteps = HULLDEBUG_MAX_WORK_STEPS;
	beforebytes = mesh->allocated_bytes;
	start = Sys_DoubleTime ();

	if (HullDebug_BuildRenderIndex(&build, &index))
	{
		for (faceindex = 0;
		     faceindex < mesh->numfaces && !build.failed; ++faceindex)
		{
			hullmeshface_t *face = &mesh->faces[faceindex];
			hullpoly2_t *fragments, *fragment;
			size_t facematches = 0;
			double right[3], up[3], mins[3], maxs[3];
			double residualarea = 0.0, surfacedist;

			if (!HullDebug_TakeStep(&build))
				break;
			face->coverage_matches = 0;
			face->residual_area = face->raw_area;
			surfacedist = HullDebug_SurfaceDistance (&build, face);
			if (!isfinite(surfacedist))
			{
				HullDebug_Fail (&build,
					"collision-diff produced an invalid surface plane");
				break;
			}
			HullDebug_MakeBasis (face->normal, right, up);
			fragments = HullDebug_ProjectCollisionFace (&build,
				face, surfacedist, right, up, mins, maxs);
			if (!fragments)
				continue;
			if (!HullDebug_SubtractRenderedSurfaces(&build,
				&index, face, surfacedist, right, up,
				mins, maxs, &fragments, &facematches))
			{
				HullDebug_FreePoly2List (&build, fragments);
				break;
			}
			matchedsurfaces += facematches;
			face->coverage_matches = facematches > UINT32_MAX ?
				UINT32_MAX : (uint32_t)facematches;
			for (fragment = fragments; fragment;
			     fragment = fragment->next)
				residualarea += fabs(HullDebug_SignedArea2(
					fragment->points, fragment->numpoints));
			if (!isfinite(residualarea) || residualarea > FLT_MAX)
			{
				HullDebug_FreePoly2List (&build, fragments);
				HullDebug_Fail (&build,
					"collision-diff produced an invalid residual area");
				break;
			}
			face->residual_area = (float)residualarea;
			for (fragment = fragments; fragment && !build.failed;
			     fragment = fragment->next)
				HullDebug_AppendDiffFace (&build, face, fragment,
					surfacedist, right, up);
			HullDebug_FreePoly2List (&build, fragments);
		}
	}
	indexedplanes = index.indexedplanes;
	indexedsurfaces = index.indexedsurfaces;
	HullDebug_FreeRenderIndex (&build, &index);
	elapsed = Sys_DoubleTime () - start;

	if (build.failed)
	{
		HullDebug_FreeDiffStorage (cache, mesh);
		mesh->diff_failed = true;
		Con_Warning ("r_showhull: disabled %s hull %d diff: %s\n",
			model->name, hullnum,
			build.failure[0] ? build.failure : "diff build failed");
		return false;
	}

	mesh->diff_built = true;
	Con_DPrintf ("r_showhull: built %s hull %d diff: %lu rendered planes,"
		" %lu rendered surfaces, %lu coverage matches, %lu residual faces,"
		" %lu vertices, %.2f MiB, %.1f ms\n",
		model->name, hullnum,
		(unsigned long)indexedplanes,
		(unsigned long)indexedsurfaces,
		(unsigned long)matchedsurfaces,
		(unsigned long)mesh->numdifffaces,
		(unsigned long)mesh->numdiffverts,
		(mesh->allocated_bytes - beforebytes) / (1024.0 * 1024.0),
		elapsed * 1000.0);
	return true;
}

static char *HullDebug_TrimToken (char *text)
{
	char *end;

	while (*text && isspace((unsigned char)*text))
		++text;
	end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return text;
}

static void HullDebug_CvarChanged (cvar_t *cvar)
{
	char value[64], *hullvalue, *cursor, *comma;

	r_showhull_mode = -1;
	r_showhull_diff = false;
	r_showhull_info = false;
	r_showhull_filter = HULLFILTER_ALL;
	memset (&hull_debug_inspect, 0, sizeof(hull_debug_inspect));
	if (strlen(cvar->string) >= sizeof(value))
		goto invalid;
	q_strlcpy (value, cvar->string, sizeof(value));
	hullvalue = HullDebug_TrimToken(value);
	comma = strchr (hullvalue, ',');
	if (comma)
	{
		*comma++ = '\0';
		hullvalue = HullDebug_TrimToken(hullvalue);
	}
	for (cursor = comma; cursor; )
	{
		char *option, *next = strchr (cursor, ',');

		if (next)
			*next++ = '\0';
		option = HullDebug_TrimToken(cursor);
		if (!*option)
			goto invalid;
		if (!q_strcasecmp(option, "diff"))
		{
			if (r_showhull_diff)
				goto invalid;
			r_showhull_diff = true;
		}
		else if (!q_strcasecmp(option, "info"))
		{
			if (r_showhull_info)
				goto invalid;
			r_showhull_info = true;
		}
		else if (!q_strcasecmp(option, "walls"))
		{
			if (r_showhull_filter != HULLFILTER_ALL)
				goto invalid;
			r_showhull_filter = HULLFILTER_WALLS;
		}
		else if (!q_strcasecmp(option, "reach"))
		{
			if (r_showhull_filter != HULLFILTER_ALL)
				goto invalid;
			r_showhull_filter = HULLFILTER_REACH;
		}
		else if (!q_strcasecmp(option, "block"))
		{
			if (r_showhull_filter != HULLFILTER_ALL)
				goto invalid;
			r_showhull_filter = HULLFILTER_BLOCK;
		}
		else
			goto invalid;
		cursor = next;
	}

	if (!q_strcasecmp(hullvalue, "0") ||
	    !q_strcasecmp(hullvalue, "off"))
	{
		if (r_showhull_diff || r_showhull_info ||
		    r_showhull_filter != HULLFILTER_ALL)
			goto invalid;
		r_showhull_mode = -1;
	}
	else if (!q_strcasecmp(hullvalue, "h0"))
		r_showhull_mode = 0;
	else if (!q_strcasecmp(hullvalue, "h1") ||
		 !q_strcasecmp(hullvalue, "1"))
		r_showhull_mode = 1;
	else if (!q_strcasecmp(hullvalue, "h2") ||
		 !q_strcasecmp(hullvalue, "2"))
		r_showhull_mode = 2;
	else
		goto invalid;
	return;

invalid:
	r_showhull_mode = -1;
	r_showhull_diff = false;
	r_showhull_info = false;
	r_showhull_filter = HULLFILTER_ALL;
	Con_Warning ("r_showhull: expected 0 or h0/h1/h2 with optional"
		" comma-separated diff/info/walls/reach/block flags; got \"%s\"\n",
		cvar->string);
	if (strcmp(cvar->string, "0"))
		Cvar_SetQuick (cvar, "0");
}

static void HullDebug_Completion (cvar_t *cvar, const char *partial)
{
	(void)cvar;

	Con_AddToTabList ("0", partial, "off", NULL);
	Con_AddToTabList ("h0", partial, "point-sized collision boundary", NULL);
	Con_AddToTabList ("h1", partial, "normal-player origin boundary", NULL);
	Con_AddToTabList ("h2", partial, "large-monster origin boundary", NULL);
	Con_AddToTabList ("h0,diff", partial,
		"point collision not explained by rendered surfaces", NULL);
	Con_AddToTabList ("h1,diff", partial,
		"player collision not explained by rendered surfaces", NULL);
	Con_AddToTabList ("h2,diff", partial,
		"monster collision not explained by rendered surfaces", NULL);
	Con_AddToTabList ("h0,info", partial,
		"point collision boundary with crosshair inspector", NULL);
	Con_AddToTabList ("h1,info", partial,
		"player collision boundary with crosshair inspector", NULL);
	Con_AddToTabList ("h2,info", partial,
		"monster collision boundary with crosshair inspector", NULL);
	Con_AddToTabList ("h0,diff,info", partial,
		"point collision residuals with crosshair inspector", NULL);
	Con_AddToTabList ("h1,diff,info", partial,
		"player collision residuals with crosshair inspector", NULL);
	Con_AddToTabList ("h2,diff,info", partial,
		"monster collision residuals with crosshair inspector", NULL);
	Con_AddToTabList ("h1,walls", partial,
		"only walls you can walk into (hide floors/ceilings)", NULL);
	Con_AddToTabList ("h1,diff,walls", partial,
		"collision-only walls (incl. invisible clip brushes)", NULL);
	Con_AddToTabList ("h1,reach", partial,
		"walls near you, at body height, that could block you", NULL);
	Con_AddToTabList ("h1,diff,reach", partial,
		"collision-only walls within reach of the player", NULL);
	Con_AddToTabList ("h1,block", partial,
		"walls that actually stop you (too tall to step over)", NULL);
	Con_AddToTabList ("h1,diff,block", partial,
		"collision-only walls that actually stop your movement", NULL);
}

static void HullDebug_Help (cvar_t *cvar)
{
	(void)cvar;

	Con_Printf ("\n");
	Con_Printf ("usage:\n");
	Con_Printf ("  r_showhull 0   disable collision-hull rendering\n");
	Con_Printf ("  r_showhull h0  show the point-collision boundary\n");
	Con_Printf ("  r_showhull h1  show the normal-player origin boundary\n");
	Con_Printf ("  r_showhull h2  show the large-monster origin boundary\n");
	Con_Printf ("  r_showhull hN,diff\n");
	Con_Printf ("                  show collision-only residuals for hull N\n");
	Con_Printf ("  r_showhull hN,info\n");
	Con_Printf ("                  inspect the raw hull face under the crosshair\n");
	Con_Printf ("  r_showhull hN,diff,info\n");
	Con_Printf ("                  inspect collision-only residual faces\n");
	Con_Printf ("  r_showhull hN,walls\n");
	Con_Printf ("                  hide floors/ceilings; show only walls\n");
	Con_Printf ("  r_showhull hN,reach\n");
	Con_Printf ("                  walls near you, at body height, facing you\n");
	Con_Printf ("  r_showhull hN,block\n");
	Con_Printf ("                  reach walls too tall to step over (STEPSIZE)\n");
	Con_Printf ("\n");
	Con_Printf ("The cyan, depth-tested wireframe is the exact boundary seen by\n");
	Con_Printf ("the selected hull's moving origin. Values 1 and 2 alias h1 and h2.\n");
	Con_Printf ("Diff mode projects the boundary back to surface space, subtracts\n");
	Con_Printf ("coplanar rendered world polygons, and draws the remaining collision.\n");
	Con_Printf ("Info mode highlights one crosshair-selected face and reports its\n");
	Con_Printf ("plane, contents, surface shift, and rendered-surface coverage.\n");
	Con_Printf ("\n");
}

void R_HullDebug_Init (void)
{
	Cvar_RegisterVariable (&r_showhull);
	Cvar_SetCallback (&r_showhull, HullDebug_CvarChanged);
	Cvar_SetCompletion (&r_showhull, HullDebug_Completion);
	Cvar_SetHelp (&r_showhull, HullDebug_Help);
	HullDebug_CvarChanged (&r_showhull);
}

static const char *HullDebug_ContentsName (int contents)
{
	switch (contents)
	{
	case CONTENTS_EMPTY:	return "EMPTY";
	case CONTENTS_SOLID:	return "SOLID";
	case CONTENTS_WATER:	return "WATER";
	case CONTENTS_SLIME:	return "SLIME";
	case CONTENTS_LAVA:	return "LAVA";
	case CONTENTS_SKY:	return "SKY";
	case CONTENTS_ORIGIN:	return "ORIGIN";
	case CONTENTS_CLIP:	return "CLIP";
	case CONTENTS_CURRENT_0:	return "CURRENT_0";
	case CONTENTS_CURRENT_90:	return "CURRENT_90";
	case CONTENTS_CURRENT_180: return "CURRENT_180";
	case CONTENTS_CURRENT_270: return "CURRENT_270";
	case CONTENTS_CURRENT_UP:	return "CURRENT_UP";
	case CONTENTS_CURRENT_DOWN: return "CURRENT_DOWN";
	case CONTENTS_LADDER:	return "LADDER";
	default:		return "UNKNOWN";
	}
}

static qboolean HullDebug_PointInMeshFace (const hullmeshface_t *face,
	const vec3_t *verts, const vec3_t point)
{
	double orientation = 0.0;
	size_t edge;
	int axis;

	for (axis = 0; axis < 3; ++axis)
	{
		if (point[axis] < face->mins[axis] - ON_EPSILON ||
		    point[axis] > face->maxs[axis] + ON_EPSILON)
			return false;
	}

	for (edge = 0; edge < face->numverts; ++edge)
	{
		const vec3_t *a = &verts[face->firstvert + edge];
		const vec3_t *b = &verts[
			face->firstvert + (edge + 1) % face->numverts];
		double edgedir[3], topoint[3], cross[3], side, tolerance;

		edgedir[0] = (*b)[0] - (*a)[0];
		edgedir[1] = (*b)[1] - (*a)[1];
		edgedir[2] = (*b)[2] - (*a)[2];
		topoint[0] = point[0] - (*a)[0];
		topoint[1] = point[1] - (*a)[1];
		topoint[2] = point[2] - (*a)[2];
		HullDebug_Cross (edgedir, topoint, cross);
		side = cross[0] * face->normal[0] +
			cross[1] * face->normal[1] +
			cross[2] * face->normal[2];
		tolerance = ON_EPSILON *
			sqrt(HullDebug_LengthSquared(edgedir));
		if (fabs(side) <= tolerance)
			continue;
		if (orientation == 0.0)
			orientation = side;
		else if ((orientation < 0.0) != (side < 0.0))
			return false;
	}
	return true;
}

static float HullDebug_MeshFaceArea (const hullmeshface_t *face,
	const vec3_t *verts)
{
	const vec3_t *origin;
	double area = 0.0;
	size_t triangle;

	if (face->numverts < 3)
		return 0.0f;
	origin = &verts[face->firstvert];
	for (triangle = 1; triangle + 1 < face->numverts; ++triangle)
	{
		const vec3_t *b = &verts[face->firstvert + triangle];
		const vec3_t *c = &verts[face->firstvert + triangle + 1];
		double edge1[3], edge2[3], cross[3];

		edge1[0] = (*b)[0] - (*origin)[0];
		edge1[1] = (*b)[1] - (*origin)[1];
		edge1[2] = (*b)[2] - (*origin)[2];
		edge2[0] = (*c)[0] - (*origin)[0];
		edge2[1] = (*c)[1] - (*origin)[1];
		edge2[2] = (*c)[2] - (*origin)[2];
		HullDebug_Cross (edge1, edge2, cross);
		area += 0.5 * sqrt(HullDebug_LengthSquared(cross));
	}
	return isfinite(area) && area <= FLT_MAX ? (float)area : 0.0f;
}

static qboolean HullDebug_PointInResidual (const hullmesh_t *mesh,
	uint32_t sourceface, const vec3_t point)
{
	size_t i;

	for (i = 0; i < mesh->numdifffaces; ++i)
	{
		const hullmeshface_t *face = &mesh->difffaces[i];

		if (face->sourceface != sourceface)
			continue;
		if (HullDebug_PointInMeshFace(face, mesh->diffverts, point))
			return true;
	}
	return false;
}

static void HullDebug_UpdateInspector (qmodel_t *model, int hullnum,
	hullmesh_t *mesh)
{
	hullinspect_t *inspect = &hull_debug_inspect;
	const hullmeshface_t *faces, *selected = NULL, *rawface;
	const vec3_t *verts;
	hullbuild_t surfacebuild;
	vec3_t raydelta, rayend, visualimpact, visualnormal;
	vec3_t hitpoint, surfacepoint;
	double bestdistance, surfacedistance, visualdistance;
	size_t numfaces, numverts, faceindex, selectedindex = 0;
	qboolean point_is_residual = false;
	float raylength;
	int axis;

	memset (inspect, 0, sizeof(*inspect));
	inspect->frame = r_framecount;
	inspect->active = true;
	inspect->hullnum = hullnum;
	inspect->diff = r_showhull_diff;
	inspect->point_fallback = mesh->point_fallback;
	inspect->coverage_ready = mesh->diff_built;
	inspect->allocated_bytes = mesh->allocated_bytes;
	q_strlcpy (inspect->model, model->name, sizeof(inspect->model));
	VectorCopy (model->hulls[hullnum].clip_mins, inspect->hullmins);
	VectorCopy (model->hulls[hullnum].clip_maxs, inspect->hullmaxs);

	if (r_showhull_diff)
	{
		faces = mesh->difffaces;
		verts = mesh->diffverts;
		numfaces = mesh->numdifffaces;
		numverts = mesh->numdiffverts;
	}
	else
	{
		faces = mesh->faces;
		verts = mesh->verts;
		numfaces = mesh->numfaces;
		numverts = mesh->numverts;
	}
	inspect->display_faces = numfaces;
	inspect->display_verts = numverts;
	if (!numfaces)
		return;

	raylength = gl_farclip.value;
	if (!isfinite(raylength) || raylength <= 1.0f)
		raylength = 65536.0f;
	raylength = q_min(raylength, 16777216.0f);
	VectorScale (vpn, raylength, raydelta);
	VectorAdd (r_refdef.vieworg, raydelta, rayend);
	CL_TraceLine (r_refdef.vieworg, rayend, visualimpact,
		visualnormal, NULL);
	VectorSubtract (visualimpact, r_refdef.vieworg, raydelta);
	visualdistance = sqrt(DotProduct(raydelta, raydelta)) + 1.0;
	if (!isfinite(visualdistance))
		visualdistance = raylength;
	bestdistance = visualdistance;

	for (faceindex = 0; faceindex < numfaces; ++faceindex)
	{
		const hullmeshface_t *face = &faces[faceindex];
		double denominator = DotProduct(vpn, face->normal);
		double distance;

		if (R_CullBox(face->mins, face->maxs) ||
		    fabs(denominator) < 0.000001)
			continue;
		distance = (face->dist -
			DotProduct(r_refdef.vieworg, face->normal)) / denominator;
		if (!isfinite(distance) ||
		    distance <= 0.01 || distance >= bestdistance)
			continue;
		VectorMA (r_refdef.vieworg, (float)distance, vpn, hitpoint);
		if (!HullDebug_PointInMeshFace(face, verts, hitpoint))
			continue;
		selected = face;
		selectedindex = faceindex;
		bestdistance = distance;
		VectorCopy (hitpoint, inspect->point);
	}

	if (!selected)
		return;
	if (selected->sourceface >= mesh->numfaces)
		return;
	rawface = &mesh->faces[selected->sourceface];

	inspect->hit = true;
	inspect->display_face = selectedindex;
	inspect->source_face = selected->sourceface;
	inspect->planenum = selected->planenum;
	inspect->outside_contents = selected->outside_contents;
	inspect->faceverts = selected->numverts;
	inspect->coverage_matches = rawface->coverage_matches;
	VectorCopy (selected->normal, inspect->normal);
	inspect->display_dist = selected->dist;
	inspect->origin_dist = rawface->dist;
	inspect->view_distance = (float)bestdistance;
	inspect->area = HullDebug_MeshFaceArea(selected, verts);

	memset (&surfacebuild, 0, sizeof(surfacebuild));
	surfacebuild.model = model;
	surfacebuild.hull = &model->hulls[hullnum];
	surfacebuild.mesh = mesh;
	surfacebuild.hullnum = hullnum;
	surfacedistance = HullDebug_SurfaceDistance(&surfacebuild, rawface);
	if (!isfinite(surfacedistance) || surfacedistance > FLT_MAX ||
	    surfacedistance < -FLT_MAX)
	{
		inspect->hit = false;
		return;
	}
	inspect->surface_dist = (float)surfacedistance;
	inspect->surface_shift =
		inspect->surface_dist - inspect->origin_dist;
	for (axis = 0; axis < 3; ++axis)
		surfacepoint[axis] = inspect->point[axis] +
			inspect->normal[axis] *
			(inspect->surface_dist - inspect->display_dist);

	if (rawface->raw_area > ON_EPSILON * ON_EPSILON)
	{
		inspect->coverage_percent = 100.0f *
			(1.0f - rawface->residual_area / rawface->raw_area);
		inspect->coverage_percent =
			CLAMP(0.0f, inspect->coverage_percent, 100.0f);
	}
	if (mesh->diff_built)
		point_is_residual = HullDebug_PointInResidual(mesh,
			selected->sourceface, surfacepoint);

	if (r_showhull_diff || point_is_residual)
		q_strlcpy (inspect->classification,
			"COLLISION-ONLY AT CURSOR",
			sizeof(inspect->classification));
	else if (!mesh->diff_built)
		q_strlcpy (inspect->classification,
			"RENDER COVERAGE UNAVAILABLE",
			sizeof(inspect->classification));
	else
		q_strlcpy (inspect->classification,
			"RENDERED SURFACE COVERAGE",
			sizeof(inspect->classification));
}

static void HullDebug_DrawInspection (const hullmesh_t *mesh)
{
	const hullmeshface_t *face;
	const vec3_t *verts;
	size_t triangle, edge;
	float marker = 3.0f;

	if (!r_showhull_info || !hull_debug_inspect.hit ||
	    hull_debug_inspect.frame != r_framecount)
		return;
	if (hull_debug_inspect.diff)
	{
		if (hull_debug_inspect.display_face >= mesh->numdifffaces)
			return;
		face = &mesh->difffaces[hull_debug_inspect.display_face];
		verts = mesh->diffverts;
	}
	else
	{
		if (hull_debug_inspect.display_face >= mesh->numfaces)
			return;
		face = &mesh->faces[hull_debug_inspect.display_face];
		verts = mesh->verts;
	}

	glDisable (GL_TEXTURE_2D);
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_CULL_FACE);
	glEnable (GL_DEPTH_TEST);
	glDepthMask (GL_FALSE);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glEnable (GL_POLYGON_OFFSET_FILL);
	glPolygonOffset (-2.0f, -2.0f);
	glColor4f (1.0f, 0.95f, 0.1f, 0.35f);
	glBegin (GL_TRIANGLES);
	for (triangle = 1; triangle + 1 < face->numverts; ++triangle)
	{
		glVertex3fv (verts[face->firstvert]);
		glVertex3fv (verts[face->firstvert + triangle]);
		glVertex3fv (verts[face->firstvert + triangle + 1]);
	}
	glEnd ();

	glDisable (GL_POLYGON_OFFSET_FILL);
	glDisable (GL_BLEND);
	glDepthRange (0.0, 0.9996);
	glLineWidth (3.0f);
	glColor4f (1.0f, 1.0f, 0.15f, 1.0f);
	glBegin (GL_LINES);
	for (edge = 0; edge < face->numverts; ++edge)
	{
		glVertex3fv (verts[face->firstvert + edge]);
		glVertex3fv (verts[
			face->firstvert + (edge + 1) % face->numverts]);
	}
	glVertex3f (hull_debug_inspect.point[0] - marker,
		hull_debug_inspect.point[1], hull_debug_inspect.point[2]);
	glVertex3f (hull_debug_inspect.point[0] + marker,
		hull_debug_inspect.point[1], hull_debug_inspect.point[2]);
	glVertex3f (hull_debug_inspect.point[0],
		hull_debug_inspect.point[1] - marker,
		hull_debug_inspect.point[2]);
	glVertex3f (hull_debug_inspect.point[0],
		hull_debug_inspect.point[1] + marker,
		hull_debug_inspect.point[2]);
	glVertex3f (hull_debug_inspect.point[0],
		hull_debug_inspect.point[1],
		hull_debug_inspect.point[2] - marker);
	glVertex3f (hull_debug_inspect.point[0],
		hull_debug_inspect.point[1],
		hull_debug_inspect.point[2] + marker);
	glEnd ();
	glLineWidth (1.0f);
}

/*
 * Populate hull_debug_reach for the current player and selected hull.  The
 * hull's clip box is the player's collision extent, so origin+clip_mins/maxs
 * is exactly the body volume the reach filter tests against.
 */
static void HullDebug_SetupReach (qmodel_t *model, int hullnum)
{
	const entity_t *player;
	float feet;

	hull_debug_reach.active = false;
	if (r_showhull_filter != HULLFILTER_REACH &&
	    r_showhull_filter != HULLFILTER_BLOCK)
		return;
	if (!model || hullnum < 0 || hullnum > 2)
		return;
	if (cl.viewentity <= 0 || cl.viewentity >= cl.max_edicts)
		return;

	player = &cl.entities[cl.viewentity];
	VectorCopy (player->origin, hull_debug_reach.origin);
	feet = player->origin[2] + model->hulls[hullnum].clip_mins[2];
	/* Block mode ignores anything the player would simply step up onto. */
	if (r_showhull_filter == HULLFILTER_BLOCK)
		feet += HULLDEBUG_STEP_HEIGHT;
	hull_debug_reach.zmin = feet;
	hull_debug_reach.zmax = player->origin[2] + model->hulls[hullnum].clip_maxs[2];
	hull_debug_reach.radius2 = HULLDEBUG_REACH_RADIUS * HULLDEBUG_REACH_RADIUS;

	/* Block mode keeps only walls in the path the player is travelling: use
	 * the horizontal velocity when moving, otherwise the way the view faces. */
	if (r_showhull_filter == HULLFILTER_BLOCK)
	{
		vec3_t dir;
		float lensq;

		dir[0] = cl.velocity[0];
		dir[1] = cl.velocity[1];
		dir[2] = 0.0f;
		lensq = dir[0] * dir[0] + dir[1] * dir[1];
		if (lensq < 100.0f)	/* < 10 u/s: standing still, fall back to gaze */
		{
			dir[0] = vpn[0];
			dir[1] = vpn[1];
			dir[2] = 0.0f;
			lensq = dir[0] * dir[0] + dir[1] * dir[1];
		}
		if (lensq > 1e-6f)
		{
			float inv = 1.0f / sqrtf (lensq);
			hull_debug_reach.dir[0] = dir[0] * inv;
			hull_debug_reach.dir[1] = dir[1] * inv;
			hull_debug_reach.dir[2] = 0.0f;
			hull_debug_reach.directional = true;
		}
	}

	hull_debug_reach.active = true;
}

/* True if a boundary face should be drawn under the active filter. */
static qboolean HullDebug_FacePassesFilter (const hullmeshface_t *face)
{
	const hullreach_t *reach;
	float nearest[2];
	float dx, dy;
	int i;

	if (r_showhull_filter == HULLFILTER_ALL)
		return true;

	/* Walls only: drop floors and ceilings for both walls and reach. */
	if (fabs(face->normal[2]) >= HULLDEBUG_WALL_MAX_NORMAL_Z)
		return false;
	if (r_showhull_filter == HULLFILTER_WALLS)
		return true;

	/* Reach: near the player, overlapping body height, facing the player. */
	reach = &hull_debug_reach;
	if (!reach->active)
		return false;
	if (face->maxs[2] < reach->zmin || face->mins[2] > reach->zmax)
		return false;
	for (i = 0; i < 2; ++i)
		nearest[i] = CLAMP (face->mins[i], reach->origin[i], face->maxs[i]);
	dx = nearest[0] - reach->origin[0];
	dy = nearest[1] - reach->origin[1];
	/* Face normal points toward empty space; the player must be in front of
	 * it (not behind a wall it can never reach). */
	if (DotProduct (reach->origin, face->normal) - face->dist < -DIST_EPSILON)
		return false;
	if (reach->directional)
	{
		/* Block mode: a forward beam along the path of travel, not a bubble.
		 * "along" is distance ahead; "side" is lateral offset from the line. */
		float along = dx * reach->dir[0] + dy * reach->dir[1];
		float side = dx * -reach->dir[1] + dy * reach->dir[0];

		if (along <= 0.0f || along > HULLDEBUG_BLOCK_FORWARD)
			return false;
		if (fabs(side) > HULLDEBUG_BLOCK_HALFWIDTH)
			return false;
		/* Must be faced roughly head-on (its normal opposes travel), so
		 * walls you slide along instead of hitting are dropped. */
		if (reach->dir[0] * face->normal[0] +
		    reach->dir[1] * face->normal[1] > -0.25f)
			return false;
	}
	else if (dx * dx + dy * dy > reach->radius2)
	{
		/* Reach mode: a plain proximity bubble around the player. */
		return false;
	}
	return true;
}

static void HullDebug_DrawOriginMesh (const hullmesh_t *mesh)
{
	size_t i;

	glPushAttrib (GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
		GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_LINE_BIT |
		GL_POLYGON_BIT | GL_VIEWPORT_BIT);
	glEnable (GL_DEPTH_TEST);
	glDepthMask (GL_FALSE);
	glDepthRange (0.0, 0.9999);
	glDisable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
	glColor4f (0.1f, 1.0f, 1.0f, 1.0f);

	glBegin (GL_LINES);
	for (i = 0; i < mesh->numfaces; ++i)
	{
		hullmeshface_t *face = &mesh->faces[i];
		size_t edge;

		if (R_CullBox(face->mins, face->maxs))
			continue;
		if (!HullDebug_FacePassesFilter(face))
			continue;
		for (edge = 0; edge < face->numverts; ++edge)
		{
			const vec3_t *a = &mesh->verts[face->firstvert + edge];
			const vec3_t *b = &mesh->verts[
				face->firstvert + (edge + 1) % face->numverts];
			glVertex3fv (*a);
			glVertex3fv (*b);
		}
	}
	glEnd ();

	HullDebug_DrawInspection (mesh);
	glPopAttrib ();
}

static void HullDebug_DrawDiffMesh (const hullmesh_t *mesh)
{
	size_t i;

	glPushAttrib (GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
		GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_LINE_BIT |
		GL_POLYGON_BIT | GL_VIEWPORT_BIT);
	glEnable (GL_DEPTH_TEST);
	glDepthMask (GL_FALSE);
	glDisable (GL_TEXTURE_2D);
	glDisable (GL_CULL_FACE);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable (GL_POLYGON_OFFSET_FILL);
	glPolygonOffset (-1.0f, -1.0f);
	glColor4f (1.0f, 0.55f, 0.05f, 0.30f);

	glBegin (GL_TRIANGLES);
	for (i = 0; i < mesh->numdifffaces; ++i)
	{
		hullmeshface_t *face = &mesh->difffaces[i];
		size_t triangle;

		if (R_CullBox(face->mins, face->maxs))
			continue;
		if (!HullDebug_FacePassesFilter(face))
			continue;
		for (triangle = 1; triangle + 1 < face->numverts; ++triangle)
		{
			glVertex3fv (mesh->diffverts[face->firstvert]);
			glVertex3fv (mesh->diffverts[
				face->firstvert + triangle]);
			glVertex3fv (mesh->diffverts[
				face->firstvert + triangle + 1]);
		}
	}
	glEnd ();

	glDisable (GL_POLYGON_OFFSET_FILL);
	glDisable (GL_BLEND);
	glDepthRange (0.0, 0.9998);
	glColor4f (1.0f, 0.18f, 0.02f, 1.0f);
	glBegin (GL_LINES);
	for (i = 0; i < mesh->numdifffaces; ++i)
	{
		hullmeshface_t *face = &mesh->difffaces[i];
		size_t edge;

		if (R_CullBox(face->mins, face->maxs))
			continue;
		if (!HullDebug_FacePassesFilter(face))
			continue;
		for (edge = 0; edge < face->numverts; ++edge)
		{
			const vec3_t *a =
				&mesh->diffverts[face->firstvert + edge];
			const vec3_t *b = &mesh->diffverts[
				face->firstvert + (edge + 1) % face->numverts];
			glVertex3fv (*a);
			glVertex3fv (*b);
		}
	}
	glEnd ();

	HullDebug_DrawInspection (mesh);
	glPopAttrib ();
}

void R_HullDebug_Draw (void)
{
	hullmesh_t *mesh;

	memset (&hull_debug_inspect, 0, sizeof(hull_debug_inspect));
	if (r_showhull_mode < 0)
		return;
	if (cl.maxclients > 1 || !r_refdef.drawworld ||
	    !cl.worldmodel || cl.worldmodel->type != mod_brush)
		return;

	mesh = HullDebug_Build (cl.worldmodel, r_showhull_mode);
	if (!mesh)
		return;
	if (r_showhull_diff)
	{
		if (!HullDebug_BuildDiff(cl.worldmodel,
			r_showhull_mode, mesh))
			return;
	}
	else if (r_showhull_info && !mesh->diff_built &&
		 !mesh->diff_failed)
		HullDebug_BuildDiff (cl.worldmodel, r_showhull_mode, mesh);

	if (r_showhull_info)
		HullDebug_UpdateInspector (cl.worldmodel,
			r_showhull_mode, mesh);
	HullDebug_SetupReach (cl.worldmodel, r_showhull_mode);
	if (r_showhull_diff)
	{
		if (mesh->numdifffaces)
			HullDebug_DrawDiffMesh (mesh);
	}
	else if (mesh->numfaces)
		HullDebug_DrawOriginMesh (mesh);
	Sbar_Changed ();
}

void SCR_HullDebug_DrawInfo (void)
{
	extern float canvas_scaling;
	hullinspect_t *inspect = &hull_debug_inspect;
	static qboolean colors_initialized;
	static plcolour_t background, heading, result;
	char lines[16][96];
	float scale, vwidth, vheight;
	int count = 0, longest = 0, maxchars;
	int panel_x, panel_y, panel_w, panel_h;
	int i;

	if (!r_showhull_info || !inspect->active ||
	    inspect->frame != r_framecount)
		return;

	if (!colors_initialized)
	{
		background = CL_PLColours_Parse("0x000000");
		heading = CL_PLColours_Parse("0xffa040");
		result = CL_PLColours_Parse("0xffff40");
		colors_initialized = true;
	}

	q_snprintf (lines[count++], sizeof(lines[0]), "HULL %d / %s%s",
		inspect->hullnum,
		inspect->diff ? "DIFF / SURFACE SPACE" : "ORIGIN SPACE",
		inspect->point_fallback ? "  FALLBACK" : "");
	q_snprintf (lines[count++], sizeof(lines[0]), "map: %s",
		inspect->model);
	q_snprintf (lines[count++], sizeof(lines[0]),
		"bounds: %.0f %.0f %.0f / %.0f %.0f %.0f",
		inspect->hullmins[0], inspect->hullmins[1],
		inspect->hullmins[2], inspect->hullmaxs[0],
		inspect->hullmaxs[1], inspect->hullmaxs[2]);
	q_snprintf (lines[count++], sizeof(lines[0]),
		"mesh: %lu faces  %lu verts  cache: %.2f MiB",
		(unsigned long)inspect->display_faces,
		(unsigned long)inspect->display_verts,
		inspect->allocated_bytes / (1024.0 * 1024.0));

	if (!inspect->hit)
	{
		if (inspect->diff && !inspect->display_faces)
			q_strlcpy (lines[count++], "NO COLLISION-ONLY RESIDUAL FACES",
				sizeof(lines[0]));
		else
			q_strlcpy (lines[count++], "AIM AT A VISIBLE HULL FACE",
				sizeof(lines[0]));
	}
	else
	{
		q_snprintf (lines[count++], sizeof(lines[0]),
			"face: %lu  source: %lu  plane: %d",
			(unsigned long)inspect->display_face,
			(unsigned long)inspect->source_face,
			inspect->planenum);
		q_snprintf (lines[count++], sizeof(lines[0]),
			"normal: %.3f %.3f %.3f",
			inspect->normal[0], inspect->normal[1],
			inspect->normal[2]);
		q_snprintf (lines[count++], sizeof(lines[0]),
			"display d: %.2f  view: %.1f",
			inspect->display_dist, inspect->view_distance);
		q_snprintf (lines[count++], sizeof(lines[0]),
			"origin d: %.2f  surface d: %.2f",
			inspect->origin_dist, inspect->surface_dist);
		q_snprintf (lines[count++], sizeof(lines[0]),
			"surface shift: %+.2f", inspect->surface_shift);
		q_snprintf (lines[count++], sizeof(lines[0]),
			"outside: %s  verts: %u  area: %.1f",
			HullDebug_ContentsName(inspect->outside_contents),
			(unsigned int)inspect->faceverts, inspect->area);
		if (inspect->coverage_ready)
			q_snprintf (lines[count++], sizeof(lines[0]),
				"render candidates: %u  covered: %.1f%%",
				(unsigned int)inspect->coverage_matches,
				inspect->coverage_percent);
		else
			q_strlcpy (lines[count++], "render coverage: unavailable",
				sizeof(lines[0]));
		q_snprintf (lines[count++], sizeof(lines[0]),
			"point: %.1f %.1f %.1f",
			inspect->point[0], inspect->point[1],
			inspect->point[2]);
		q_strlcpy (lines[count++], inspect->classification,
			sizeof(lines[0]));
	}

	GL_SetCanvas (CANVAS_AUTOID);
	scale = canvas_scaling;
	vwidth = glwidth / scale;
	vheight = glheight / scale;
	maxchars = CLAMP(18, (int)vwidth / 8 - 4, 63);
	for (i = 0; i < count; ++i)
	{
		int length;

		lines[i][maxchars] = '\0';
		length = (int)strlen(lines[i]);
		longest = q_max(longest, length);
	}
	panel_w = longest * 8 + 16;
	panel_h = count * 8 + 16;
	panel_x = 8;
	panel_y = (int)vheight - panel_h - 8 -
		(int)(sb_lines / scale);
	panel_y = q_max(panel_y, 8);

	Draw_Fill_Plus_Radius (panel_x, panel_y, panel_w, panel_h,
		background, 0.78f, true, DRAW_CORNERS_ALL, 5.0f);
	for (i = 0; i < count; ++i)
	{
		if (i == 0)
			Draw_StringRGBA (panel_x + 8, panel_y + 8 + i * 8,
				lines[i], heading, 1.0f);
		else if (i == count - 1)
			Draw_StringRGBA (panel_x + 8, panel_y + 8 + i * 8,
				lines[i], result, 1.0f);
		else
			Draw_String (panel_x + 8, panel_y + 8 + i * 8,
				lines[i]);
	}
	GL_Set2D ();
}

void R_HullDebug_Cleanup (qmodel_t *model)
{
	hullmodelcache_t *cache;
	int hullnum;

	if (!model || !model->hull_debug_cache)
		return;
	if (model->hull_debug_cache == &hull_debug_failed_cache)
	{
		model->hull_debug_cache = NULL;
		return;
	}

	cache = (hullmodelcache_t *)model->hull_debug_cache;
	for (hullnum = 0; hullnum < 3; ++hullnum)
		HullDebug_FreeMeshStorage (cache, &cache->meshes[hullnum]);
	free (cache);
	model->hull_debug_cache = NULL;
	memset (&hull_debug_inspect, 0, sizeof(hull_debug_inspect));
}
