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
// zone.c

#include "quakedef.h"

#define	DYNAMIC_SIZE	(4 * 1024 * 1024) // ericw -- was 512KB (64-bit) / 384KB (32-bit)

#define	ZONEID	0x1d4a11
#define MINFRAGMENT	64

typedef struct memblock_s
{
	int	size;		// including the header and possibly tiny fragments
	int	tag;		// a tag of 0 is a free block
	int	id;		// should be ZONEID
	int	pad;		// pad to 64 bit boundary
	struct	memblock_s	*next, *prev;
} memblock_t;

typedef struct
{
	int		size;		// total bytes malloced, including header
	memblock_t	blocklist;	// start / end cap for linked list
	memblock_t	*rover;
} memzone_t;

/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

static memzone_t	*mainzone;


/*
========================
Z_Free
========================
*/
void Z_Free (void *ptr)
{
	memblock_t	*block, *other;

	if (!ptr)
		return;	//ignore this like libc would
//		Sys_Error ("Z_Free: NULL pointer");

	block = (memblock_t *) ( (byte *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
	{
		Con_Printf("Z_Free: freed a pointer without ZONEID\n"); // woods -- FrikaC temporary? fix for server crashes
		return;
	}
	if (block->tag == 0)
		Sys_Error ("Z_Free: freed a freed pointer");

	block->tag = 0;		// mark as free

	other = block->prev;
	if (!other->tag)
	{	// merge with previous free block
		other->size += block->size;
		other->next = block->next;
		other->next->prev = other;
		if (block == mainzone->rover)
			mainzone->rover = other;
		block = other;
	}

	other = block->next;
	if (!other->tag)
	{	// merge the next free block onto the end
		block->size += other->size;
		block->next = other->next;
		block->next->prev = block;
		if (other == mainzone->rover)
			mainzone->rover = block;
	}
}


static void *Z_TagMalloc (int size, int tag)
{
	int		extra;
	memblock_t	*start, *rover, *newblock, *base;

	if (!tag)
		Sys_Error ("Z_TagMalloc: tried to use a 0 tag");

//
// scan through the block list looking for the first free block
// of sufficient size
//
	size += sizeof(memblock_t);	// account for size of block header
	size += 4;					// space for memory trash tester
	size = (size + 7) & ~7;		// align to 8-byte boundary

	base = rover = mainzone->rover;
	start = base->prev;

	do
	{
		if (rover == start)	// scaned all the way around the list
			return NULL;
		if (rover->tag)
			base = rover = rover->next;
		else
			rover = rover->next;
	} while (base->tag || base->size < size);

//
// found a block big enough
//
	extra = base->size - size;
	if (extra >  MINFRAGMENT)
	{	// there will be a free fragment after the allocated block
		newblock = (memblock_t *) ((byte *)base + size );
		newblock->size = extra;
		newblock->tag = 0;			// free block
		newblock->prev = base;
		newblock->id = ZONEID;
		newblock->next = base->next;
		newblock->next->prev = newblock;
		base->next = newblock;
		base->size = size;
	}

	base->tag = tag;				// no longer a free block

	mainzone->rover = base->next;	// next allocation will start looking here

	base->id = ZONEID;

// marker for memory trash testing
	*(int *)((byte *)base + base->size - 4) = ZONEID;

	return (void *) ((byte *)base + sizeof(memblock_t));
}

/*
========================
Z_CheckHeap
========================
*/
static void Z_CheckHeap (void)
{
	memblock_t	*block;

	for (block = mainzone->blocklist.next ; ; block = block->next)
	{
		if (block->next == &mainzone->blocklist)
			break;			// all blocks have been hit
		if ( (byte *)block + block->size != (byte *)block->next)
			Sys_Error ("Z_CheckHeap: block size does not touch the next block");
		if ( block->next->prev != block)
			Sys_Error ("Z_CheckHeap: next block doesn't have proper back link");
		if (!block->tag && !block->next->tag)
			Sys_Error ("Z_CheckHeap: two consecutive free blocks");
	}
}

/*
===================
Q_malloc -- woods for #iplog

Use it instead of malloc so that if memory allocation fails,
the program exits with a message saying there's not enough memory
instead of crashing after trying to use a NULL pointer
===================
*/
void* Q_malloc(size_t size)
{
	void* p;

	if (!(p = malloc(size)))
		Sys_Error("Not enough memory free; check disk space");

	return p;
}

/*
========================
Z_Malloc
========================
*/
void *Z_Malloc (int size)
{
	void	*buf;

	Z_CheckHeap ();	// DEBUG
	buf = Z_TagMalloc (size, 1);
	if (!buf)
		Sys_Error ("Z_Malloc: failed on allocation of %i bytes",size);
	Q_memset (buf, 0, size);

	return buf;
}

/*
========================
Z_Realloc
========================
*/
void *Z_Realloc(void *ptr, int size)
{
	int old_size;
	void *old_ptr;
	memblock_t *block;

	if (!ptr)
		return Z_Malloc (size);

	block = (memblock_t *) ((byte *) ptr - sizeof (memblock_t));
	if (block->id != ZONEID)
		Sys_Error ("Z_Realloc: realloced a pointer without ZONEID");
	if (block->tag == 0)
		Sys_Error ("Z_Realloc: realloced a freed pointer");

	old_size = block->size;
	old_size -= (4 + (int)sizeof(memblock_t));	/* see Z_TagMalloc() */
	old_ptr = ptr;

	Z_Free (ptr);
	ptr = Z_TagMalloc (size, 1);
	if (!ptr)
		Sys_Error ("Z_Realloc: failed on allocation of %i bytes", size);

	//Spike -- fix a bug where alignment resulted in no 0-initialisation
	block = (memblock_t *) ((byte *) ptr - sizeof (memblock_t));
	size = block->size;
	size -= (4 + (int)sizeof(memblock_t));	/* see Z_TagMalloc() */
	//Spike -- end fix

	if (ptr != old_ptr)
		memmove (ptr, old_ptr, q_min(old_size, size));
	if (old_size < size)
		memset ((byte *)ptr + old_size, 0, size - old_size);

	return ptr;
}

char *Z_Strdup (const char *s)
{
	size_t sz = strlen(s) + 1;
	char *ptr;

	if (sz > (size_t)INT_MAX)
		Sys_Error ("Z_Strdup: string too long");
	ptr = (char *) Z_Malloc ((int)sz);
	memcpy (ptr, s, sz);
	return ptr;
}


/*
========================
Z_Print
========================
*/
void Z_Print (memzone_t *zone)
{
	memblock_t	*block;

	Con_Printf ("zone size: %i  location: %p\n",mainzone->size,mainzone);

	for (block = zone->blocklist.next ; ; block = block->next)
	{
		Con_Printf ("block:%p    size:%7i    tag:%3i\n",
			block, block->size, block->tag);

		if (block->next == &zone->blocklist)
			break;			// all blocks have been hit
		if ( (byte *)block + block->size != (byte *)block->next)
			Con_Printf ("ERROR: block size does not touch the next block\n");
		if ( block->next->prev != block)
			Con_Printf ("ERROR: next block doesn't have proper back link\n");
		if (!block->tag && !block->next->tag)
			Con_Printf ("ERROR: two consecutive free blocks\n");
	}
}


//============================================================================

#define	HUNK_SENTINEL	0x1df001ed

#define HUNKNAME_LEN	24
typedef struct
{
	int		sentinel;
	int		size;		// including sizeof(hunk_t), -1 = not allocated
	char	name[HUNKNAME_LEN];
} hunk_t;

typedef struct hunkseg_s
{
	int					base;
	int					size;
	int					used;
	int					pad; // pad to power of 2
} hunkseg_t;

#define MAX_SEGMENTS	8
#define HUNK_MAX_SIZE	(INT_MAX & ~15)
#define SEG_MEM(seg)	((byte *) ((seg) + 1))
#define LASTSEG			(hunk_segments[hunk_numsegments-1])

static int				hunk_low_used;
static int				hunk_numsegments;
static hunkseg_t		*hunk_segments[MAX_SEGMENTS];

typedef enum
{
	HF_UNINIT			= 0,
	HF_CLEAR			= 1 << 0,
} hunkflags_t;


/*
===================
Hunk_Size
===================
*/
static int Hunk_Size (void)
{
	return LASTSEG->base + LASTSEG->size;
}


/*
===================
Hunk_GetName
===================
*/
static const char *Hunk_GetName (const hunk_t *hunk)
{
	return hunk->name[0] ? hunk->name : "unknown";
}

/*
==============
Hunk_Check

Run consistency and sentinel trashing checks
==============
*/
void Hunk_Check (void)
{
	int i, ofs;

	for (i = 0; i < hunk_numsegments && hunk_segments[i]->base < hunk_low_used; i++)
	{
		const hunkseg_t *seg = hunk_segments[i];
		for (ofs = 0; ofs < seg->used; )
		{
			const hunk_t *h = (const hunk_t *) (SEG_MEM (seg) + ofs);
			if (h->sentinel != HUNK_SENTINEL)
				Sys_Error ("Hunk_Check: trashed sentinel");
			if (h->size < (int) sizeof(hunk_t) || h->size > seg->size - ofs)
				Sys_Error ("Hunk_Check: bad size");
			ofs += h->size;
		}
	}
}

/*
==============
Hunk_Print

If "all" is specified, every single allocation is printed.
Otherwise, allocations with the same name will be totaled up before printing.
==============
*/
void Hunk_Print (qboolean all)
{
	int i, count, sum, totalblocks;

	count = 0;
	sum = 0;
	totalblocks = 0;

	Con_SafePrintf ("\n");

	// print segments if more than 1
	if (hunk_numsegments > 1)
	{
		Con_SafePrintf ("             Segments\n");
		Con_SafePrintf ("---------------------------------\n");
		Con_SafePrintf ("id :     offset :       size\n");
		Con_SafePrintf ("---------------------------------\n");
		for (i = 0; i < hunk_numsegments; i++)
			Con_SafePrintf ("%2i : %10i : %10i\n", i, hunk_segments[i]->base, hunk_segments[i]->size);
		Con_SafePrintf ("---------------------------------\n");
		Con_SafePrintf ("\n");
		Con_SafePrintf ("           Allocations\n");
		Con_SafePrintf ("---------------------------------\n");
	}

	if (all)
		Con_SafePrintf ("    offset :       size : name\n");
	else
		Con_SafePrintf ("allocs :       size : name\n");
	Con_SafePrintf ("---------------------------------\n");

	for (i = 0; i < hunk_numsegments; i++)
	{
		const hunkseg_t *seg = hunk_segments[i];
		if (seg->base < hunk_low_used)
		{
			int ofs;

			for (ofs = 0; ofs < seg->used; )
			{
				const hunk_t *h, *next;

				h = (const hunk_t *) (SEG_MEM (seg) + ofs);

				//
				// run consistency checks
				//
				if (h->sentinel != HUNK_SENTINEL)
					Sys_Error ("Hunk_Check: trashed sentinel");
				if (h->size < (int) sizeof(hunk_t) || h->size > seg->size - ofs)
					Sys_Error ("Hunk_Check: bad size");

				// if this is the last block in the segment, then the next block is either
				// the first block of the next segment, or NULL if this is the last segment
				if (ofs + h->size == seg->used)
					next = (i != hunk_numsegments - 1 && hunk_segments[i + 1]->used > 0) ? (const hunk_t *) SEG_MEM (hunk_segments[i + 1]) : NULL;
				else // at least 1 more block in the current segment
					next = (const hunk_t *) ((byte *)h + h->size);

				count++;
				totalblocks++;
				sum += h->size;

				//
				// print the single block
				//
				if (all)
					Con_SafePrintf ("%10i : %10i : %s\n", seg->base + ofs, h->size, Hunk_GetName (h));

				//
				// print the total
				//
				if (!next || strncmp (Hunk_GetName (h), Hunk_GetName (next), HUNKNAME_LEN - 1) != 0)
				{
					if (!all)
						Con_SafePrintf ("%6i : %10i : %s\n", count, sum, Hunk_GetName (h));
					count = 0;
					sum = 0;
				}

				ofs += h->size;
			}
		}
	}

	Con_SafePrintf ("---------------------------------\n");

	if (all)
	{
		Con_SafePrintf ("%10s   %10i   USED (%d allocs)\n", "", hunk_low_used, totalblocks);
		Con_SafePrintf ("%10s   %10i   REMAINING\n", "", Hunk_Size () - hunk_low_used);
		Con_SafePrintf ("%10s   %10i   TOTAL\n", "", Hunk_Size ());
	}
	else
	{
		Con_SafePrintf ("%6i : %10i : USED\n", totalblocks, hunk_low_used);
		Con_SafePrintf ("%6s : %10i : REMAINING\n", "", Hunk_Size () - hunk_low_used);
		Con_SafePrintf ("%6s : %10i : TOTAL\n", "", Hunk_Size ());
	}
}

/*
===================
Hunk_Print_f -- johnfitz -- console command to call hunk_print
===================
*/
void Hunk_Print_f (void)
{
	Hunk_Print (false);
}

/*
===================
Hunk_SegForOfs
===================
*/
static int Hunk_SegForOfs (int ofs)
{
	int i;

	if (ofs == Hunk_Size ())
		return hunk_numsegments - 1;

	for (i = hunk_numsegments - 1; i >= 0; i--)
	{
		const hunkseg_t *seg = hunk_segments[i];
		if (seg->base <= ofs && ofs < seg->base + seg->size)
			return i;
	}

	Sys_Error ("Hunk_SegForOfs: bad offset %d (max: %d)", ofs, Hunk_Size ());

	return -1;
}

/*
===================
Hunk_AllocInternal
===================
*/
static void *Hunk_AllocInternal (int size, const char *name, hunkflags_t flags)
{
	hunkseg_t	*seg;
	hunk_t		*h;
	int			i;

#ifdef PARANOID
	Hunk_Check ();
#endif

	if (size < 0)
		Sys_Error ("Hunk_Alloc: bad size: %i", size);

	if (size == 0)
		return NULL;

	if (size > INT_MAX - (int) sizeof(hunk_t) - 15)
		Sys_Error ("Hunk_Alloc: bad size: %i", size);

	size = sizeof(hunk_t) + ((size+15)&~15);

	i = Hunk_SegForOfs (hunk_low_used);

	// skip segments that can't handle this request (adjusting hunk_low_used)
	while (i < hunk_numsegments)
	{
		int used = hunk_low_used - hunk_segments[i]->base;
		if (used < 0 || used > hunk_segments[i]->size)
			Sys_Error ("Hunk_Alloc: bad hunk mark");
		if (size <= hunk_segments[i]->size - used)
			break;

		hunk_low_used = hunk_segments[i]->base + hunk_segments[i]->size;
		i++;
	}

	// add new segment if we've reached the end
	if (i == hunk_numsegments)
	{
		int newbase, newsize;

		if (hunk_numsegments == MAX_SEGMENTS)
			Sys_Error ("Hunk_Alloc: segment overflow");

		if (LASTSEG->base > HUNK_MAX_SIZE - LASTSEG->size)
			Sys_Error ("Hunk_Alloc: segment overflow");

		newbase = LASTSEG->base + LASTSEG->size;
		newsize = LASTSEG->size > INT_MAX / 2 ? size : LASTSEG->size * 2;
		newsize = q_max (newsize, size);
		if (newbase > HUNK_MAX_SIZE - size)
			Sys_Error ("Hunk_Alloc: hunk exhausted");
		newsize = q_min (newsize, HUNK_MAX_SIZE - newbase);

		Sys_Printf ("Allocating new hunk segment: %.2lf MiB\n", newsize / 1048576.0);

		seg = (hunkseg_t *) malloc (sizeof (hunkseg_t) + newsize);
		if (!seg)
		{
			Sys_Error ("Hunk_Alloc: failed on %i bytes", size);
			return NULL;
		}

		seg->base = newbase;
		seg->size = newsize;
		seg->used = 0;

		hunk_segments[hunk_numsegments++] = seg;
		hunk_low_used = seg->base;
	}

	seg = hunk_segments[i];
	h = (hunk_t *) (SEG_MEM (seg) + hunk_low_used - seg->base);
	hunk_low_used += size;
	seg->used = hunk_low_used - seg->base;

	if (flags & HF_CLEAR)
		memset (h, 0, size);

	h->size = size;
	h->sentinel = HUNK_SENTINEL;
	if (name)
		q_strlcpy (h->name, name, HUNKNAME_LEN);
	else
		h->name[0] = '\0';

	return (void *)(h+1);
}

/*
===================
Hunk_AllocName
===================
*/
void *Hunk_AllocName (int size, const char *name)
{
	return Hunk_AllocInternal (size, name, HF_CLEAR);
}

/*
===================
Hunk_AllocNameNoFill
===================
*/
void *Hunk_AllocNameNoFill (int size, const char *name)
{
	return Hunk_AllocInternal (size, name, HF_UNINIT);
}

/*
===================
Hunk_Alloc
===================
*/
void *Hunk_Alloc (int size)
{
	return Hunk_AllocName (size, NULL);
}

/*
===================
Hunk_AllocNoFill
===================
*/
void *Hunk_AllocNoFill (int size)
{
	return Hunk_AllocNameNoFill (size, NULL);
}

int	Hunk_LowMark (void)
{
	return hunk_low_used;
}

/*
===================
Hunk_IsContiguous

Returns true if the byte range [from, to) lies within a single hunk segment.
Used by callers (e.g. alias model loaders) that walk allocations as if they
were contiguous in memory via base+offset arithmetic; that assumption breaks
when a Hunk_Alloc triggers segment growth mid-build.
===================
*/
qboolean Hunk_IsContiguous (int from, int to)
{
	if (from < 0 || to < from || to > hunk_low_used)
		return false;
	if (from == to)
		return true;
	return Hunk_SegForOfs (from) == Hunk_SegForOfs (to - 1);
}

/* Returns true only while the complete range is backed by live Hunk storage below mark. */
qboolean Hunk_IsRangeBeforeMark (const void *ptr, size_t size, int mark)
{
	int i;

	if (!ptr || !size || mark < 0 || mark > hunk_low_used)
		return false;

	for (i = hunk_numsegments - 1; i >= 0; i--)
	{
		const hunkseg_t *seg = hunk_segments[i];
		const byte *begin = SEG_MEM (seg);
		const byte *end = begin + seg->used;
		size_t offset;

		if (!PTR_IN_RANGE (ptr, begin, end))
			continue;
		offset = (size_t)((const byte *)ptr - begin);
		if (size > (size_t)seg->used - offset)
			return false;
		return seg->base <= mark && offset + size <= (size_t)(mark - seg->base);
	}

	return false;
}

void Hunk_FreeToLowMark (int mark)
{
	int i;

	if (mark < 0 || mark > hunk_low_used)
		Sys_Error ("Hunk_FreeToLowMark: bad mark %i", mark);

	hunk_low_used = mark;
	for (i = Hunk_SegForOfs (hunk_low_used); i < hunk_numsegments; i++)
		hunk_segments[i]->used = q_max (0, hunk_low_used - hunk_segments[i]->base);
}

char *Hunk_Strdup (const char *s, const char *name)
{
	size_t sz = strlen(s) + 1;
	char *ptr;

	if (sz > (size_t)INT_MAX)
		Sys_Error ("Hunk_Strdup: string too long");
	ptr = (char *) Hunk_AllocNameNoFill ((int)sz, name);
	memcpy (ptr, s, sz);
	return ptr;
}

/*
===============================================================================

CACHE MEMORY

===============================================================================
*/

#define CACHENAME_LEN	32
#define CACHE_INITIAL_BUDGET_MB	64u
#define CACHE_RECENT_FRAMES	2u

typedef struct cache_system_s
{
	int			size;		// including this header
	cache_user_t		*user;
	char			name[CACHENAME_LEN];
	struct cache_system_s	*prev, *next;
	struct cache_system_s	*lru_prev, *lru_next;	// for LRU flushing
	unsigned int		last_used_frame;
	qmodel_t		*textureowner;
} cache_system_t;

// Cached model payloads contain SIMD-friendly data and must stay 16-byte aligned.
COMPILE_TIME_ASSERT (cache_header_alignment, sizeof(cache_system_t) % 16 == 0);

static cache_system_t	cache_head;
static size_t		cache_bytes;
static size_t		cache_peak_bytes;
static size_t		cache_budget;
static size_t		cache_max_budget;
static unsigned int	cache_evictions;
static qboolean		cache_limit_warned;
static qboolean		cache_initialized;

static size_t Cache_MaxBudget (void)
{
	size_t max_mb = 1024;

#if defined(USE_SDL2)
	{
		const int system_ram_mb = SDL_GetSystemRAM ();
		if (system_ram_mb > 0)
			max_mb = (size_t)system_ram_mb / 4;
	}
#endif

	if (sizeof(void *) <= 4)
		max_mb = q_min (max_mb, (size_t)512);
	else
		max_mb = q_min (max_mb, (size_t)2048);
	max_mb = q_max (max_mb, (size_t)256);

	return max_mb * 1024 * 1024;
}

void Cache_UnlinkLRU (cache_system_t *cs)
{
	if (!cs->lru_next || !cs->lru_prev)
		Sys_Error ("Cache_UnlinkLRU: NULL link");

	cs->lru_next->lru_prev = cs->lru_prev;
	cs->lru_prev->lru_next = cs->lru_next;

	cs->lru_prev = cs->lru_next = NULL;
}

void Cache_MakeLRU (cache_system_t *cs)
{
	if (cs->lru_next || cs->lru_prev)
		Sys_Error ("Cache_MakeLRU: active link");

	cache_head.lru_next->lru_prev = cs;
	cs->lru_next = cache_head.lru_next;
	cs->lru_prev = &cache_head;
	cache_head.lru_next = cs;
}

static qboolean Cache_WasUsedRecently (const cache_system_t *cs)
{
	return (unsigned int)host_framecount - cs->last_used_frame <= CACHE_RECENT_FRAMES;
}

static qboolean Cache_GrowBudget (size_t required)
{
	size_t new_budget = cache_budget;

	while (new_budget < required && new_budget < cache_max_budget)
	{
		if (new_budget > cache_max_budget / 2)
			new_budget = cache_max_budget;
		else
			new_budget *= 2;
	}

	if (new_budget <= cache_budget)
		return false;

	cache_budget = new_budget;
	Sys_Printf ("Growing data cache budget to %.1f MiB\n",
		cache_budget / 1048576.0);
	return true;
}

/*
 * Cache users may keep the returned pointer for the rest of the current host
 * frame.  Never reclaim such an entry under budget or malloc pressure: model
 * eviction also releases its GL textures, so doing so could invalidate both
 * CPU and renderer state that is still in flight.
 */
static cache_system_t *Cache_FindEvictableLRU (void)
{
	cache_system_t *cs;

	for (cs = cache_head.lru_prev; cs != &cache_head; cs = cs->lru_prev)
	{
		if (cs->last_used_frame != (unsigned int)host_framecount)
			return cs;
	}

	return NULL;
}

static qboolean Cache_EvictLRU (void)
{
	cache_system_t *cs = Cache_FindEvictableLRU ();

	if (!cs)
		return false;

	cache_evictions++;
	Cache_Free (cs->user);
	return true;
}

static void Cache_MakeRoom (size_t size)
{
	size_t required;

	if (cache_bytes > (size_t)-1 - size)
		Sys_Error ("Cache_Alloc: size overflow");
	required = cache_bytes + size;

	while (required > cache_budget)
	{
		cache_system_t *victim = cache_head.lru_prev;

		if (victim == &cache_head || Cache_WasUsedRecently (victim))
		{
			if (Cache_GrowBudget (required))
				continue;
		}

		if (!Cache_EvictLRU ())
		{
			/* The adaptive ceiling is soft while every entry is in use this frame. */
			if (!cache_limit_warned)
			{
				cache_limit_warned = true;
				Con_Warning ("Data cache active working set exceeded %.1f MiB; temporarily exceeding budget\n",
					cache_max_budget / 1048576.0);
			}
			break;
		}
		required = cache_bytes + size;
	}
}

/*
============
Cache_Flush

Throw everything out, so new data will be demand cached
============
*/
void Cache_Flush (void)
{
	if (!cache_initialized)
		return;

	while (cache_head.next != &cache_head)
	{
		cache_system_t *cs = cache_head.next;
		Cache_Free (cs->user);
	}
}

void Cache_Shutdown (void)
{
	if (!cache_initialized)
		return;

	Cache_Flush ();
	cache_initialized = false;
}

void Cache_Flush_f (cvar_t* var) // woods #loadskins
{
	Cache_Flush();
}

/*
============
Cache_Print

============
*/
void Cache_Print (void)
{
	cache_system_t	*cd;

	for (cd = cache_head.next ; cd != &cache_head ; cd = cd->next)
	{
		Con_Printf ("%8i : %s\n", cd->size, cd->name);
	}
}

/*
============
Cache_Report

============
*/
void Cache_Report (void)
{
	Con_DPrintf ("%.1f MiB data cache used (%.1f MiB peak, %.1f MiB budget, %u evictions)\n",
		cache_bytes / 1048576.0, cache_peak_bytes / 1048576.0,
		cache_budget / 1048576.0, cache_evictions);
}

/*
============
Cache_Init

============
*/
void Cache_Init (void)
{
	if (cache_initialized)
		Sys_Error ("Cache_Init: already initialized");

	memset (&cache_head, 0, sizeof(cache_head));
	cache_head.next = cache_head.prev = &cache_head;
	cache_head.lru_next = cache_head.lru_prev = &cache_head;
	cache_bytes = cache_peak_bytes = 0;
	cache_evictions = 0;
	cache_limit_warned = false;
	cache_budget = (size_t)CACHE_INITIAL_BUDGET_MB * 1024 * 1024;
	cache_max_budget = Cache_MaxBudget ();
	cache_budget = q_min (cache_budget, cache_max_budget);
	cache_initialized = true;

	Cmd_AddCommand ("flush", Cache_Flush);
}

/*
==============
Cache_Free

Frees the memory and removes it from the LRU list
==============
*/
void Cache_Free (cache_user_t *c)
{
	cache_system_t	*cs;

	if (!c->data)
		Sys_Error ("Cache_Free: not allocated");

	cs = ((cache_system_t *)c->data) - 1;

	cs->prev->next = cs->next;
	cs->next->prev = cs->prev;
	cs->next = cs->prev = NULL;

	c->data = NULL;

	Cache_UnlinkLRU (cs);
	if (cache_bytes < (size_t)cs->size)
		Sys_Error ("Cache_Free: byte count underflow");
	cache_bytes -= (size_t)cs->size;

	if (cs->textureowner)
		TexMgr_FreeTexturesForOwner (cs->textureowner);

	free (cs);
}



/*
==============
Cache_Check
==============
*/
void *Cache_Check (cache_user_t *c)
{
	cache_system_t	*cs;

	if (!c->data)
		return NULL;

	cs = ((cache_system_t *)c->data) - 1;

// move to head of LRU
	Cache_UnlinkLRU (cs);
	Cache_MakeLRU (cs);
	cs->last_used_frame = (unsigned int)host_framecount;

	return c->data;
}


/*
==============
Cache_Alloc
==============
*/
void *Cache_Alloc (cache_user_t *c, int size, const char *name, qmodel_t *textureowner)
{
	cache_system_t	*cs;

	if (c->data)
		Sys_Error ("Cache_Alloc: already allocated");

	if (size <= 0)
		Sys_Error ("Cache_Alloc: size %i", size);

	if (size > INT_MAX - (int) sizeof(cache_system_t) - 15)
		Sys_Error ("Cache_Alloc: size %i", size);

	size = (size + sizeof(cache_system_t) + 15) & ~15;
	Cache_MakeRoom ((size_t)size);

	cs = (cache_system_t *)malloc ((size_t)size);
	while (!cs && Cache_EvictLRU ())
		cs = (cache_system_t *)malloc ((size_t)size);
	if (!cs)
		Sys_Error ("Cache_Alloc: out of memory on %i bytes", size);

	memset (cs, 0, sizeof(*cs));
	cs->size = size;
	cs->user = c;
	cs->textureowner = textureowner;
	cs->last_used_frame = (unsigned int)host_framecount;
	q_strlcpy (cs->name, name, CACHENAME_LEN);
	c->data = (void *)(cs + 1);

	cs->next = cache_head.next;
	cs->prev = &cache_head;
	cache_head.next->prev = cs;
	cache_head.next = cs;
	Cache_MakeLRU (cs);

	cache_bytes += (size_t)size;
	cache_peak_bytes = q_max (cache_peak_bytes, cache_bytes);

	return c->data;
}

//============================================================================


static void Memory_InitZone (memzone_t *zone, int size)
{
	memblock_t	*block;

// set the entire zone to one free block

	zone->blocklist.next = zone->blocklist.prev = block =
		(memblock_t *)( (byte *)zone + sizeof(memzone_t) );
	zone->blocklist.tag = 1;	// in use block
	zone->blocklist.id = 0;
	zone->blocklist.size = 0;
	zone->rover = block;

	block->prev = block->next = &zone->blocklist;
	block->tag = 0;			// free block
	block->id = ZONEID;
	block->size = size - sizeof(memzone_t);
}

/*
========================
Memory_Init
========================
*/
void Memory_Init (void *buf, int size)
{
	int p;
	int zonesize = DYNAMIC_SIZE;

	hunk_segments[0] = (hunkseg_t *) buf;
	hunk_segments[0]->base = 0;
	hunk_segments[0]->size = size - sizeof (hunkseg_t);
	hunk_numsegments = 1;
	hunk_low_used = 0;

	Cache_Init ();
	p = COM_CheckParm ("-zone");
	if (p)
	{
		if (p < com_argc-1)
			zonesize = Q_atoi (com_argv[p+1]) * 1024;
		else
			Sys_Error ("Memory_Init: you must specify a size in KB after -zone");
	}
	mainzone = (memzone_t *) Hunk_AllocName (zonesize, "zone" );
	Memory_InitZone (mainzone, zonesize);

	Cmd_AddCommand ("hunk_print", Hunk_Print_f); //johnfitz
}

