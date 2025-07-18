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

// common.c -- misc functions used in client and server

#include "quakedef.h"
#include "q_ctype.h"
#include <errno.h>
#include <sys/stat.h>

#ifndef _WIN32
	#include <dirent.h>
	#include <fnmatch.h>
	#ifndef FNM_CASEFOLD
		#define FNM_CASEFOLD 0	//not available. I guess we're not on gnu/linux
	#endif
#endif

#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif

#include "zlib.h" // woods #unpak

static char	*largv[MAX_NUM_ARGVS + 1];
static char	argvdummy[] = " ";

int		safemode;

cvar_t	registered = {"registered","1",CVAR_ROM}; /* set to correct value in COM_CheckRegistered() */
cvar_t	cmdline = {"cmdline","",CVAR_ROM/*|CVAR_SERVERINFO*/}; /* sending cmdline upon CCREQ_RULE_INFO is evil */
cvar_t	allow_download = {"allow_download", "2",CVAR_ARCHIVE}; /*set to 0 to block file downloads, both client+server*/ // woods #ftehack

static qboolean		com_modified;	// set true if using non-id files

qboolean		fitzmode;
qboolean		pak0; // woods #pak0only

static void COM_Path_f (void);
void Host_WriteConfig_f (void); // woods #writecfg

extern qboolean progs_check_done; // woods #botdetect

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT		339	/* id1/pak0.pak - v1.0x */
#define PAK0_CRC_V100		13900	/* id1/pak0.pak - v1.00 */
#define PAK0_CRC_V101		62751	/* id1/pak0.pak - v1.01 */
#define PAK0_CRC_V106		32981	/* id1/pak0.pak - v1.06 */
#define PAK0_CRC	(PAK0_CRC_V106)
#define PAK0_COUNT_V091		308	/* id1/pak0.pak - v0.91/0.92, not supported */
#define PAK0_CRC_V091		28804	/* id1/pak0.pak - v0.91/0.92, not supported */

char	com_token[1024];
int		com_argc;
char	**com_argv;

#define CMDLINE_LENGTH	256		/* johnfitz -- mirrored in cmd.c */
char	com_cmdline[CMDLINE_LENGTH];

qboolean standard_quake = true, rogue, hipnotic;

// this graphic needs to be in the pak file to use registered features
static unsigned short pop[] =
{
	0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x6600,0x0000,0x0000,0x0000,0x6600,0x0000,
	0x0000,0x0066,0x0000,0x0000,0x0000,0x0000,0x0067,0x0000,
	0x0000,0x6665,0x0000,0x0000,0x0000,0x0000,0x0065,0x6600,
	0x0063,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6563,
	0x0064,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6564,
	0x0064,0x6564,0x0000,0x6469,0x6969,0x6400,0x0064,0x6564,
	0x0063,0x6568,0x6200,0x0064,0x6864,0x0000,0x6268,0x6563,
	0x0000,0x6567,0x6963,0x0064,0x6764,0x0063,0x6967,0x6500,
	0x0000,0x6266,0x6769,0x6a68,0x6768,0x6a69,0x6766,0x6200,
	0x0000,0x0062,0x6566,0x6666,0x6666,0x6666,0x6562,0x0000,
	0x0000,0x0000,0x0062,0x6364,0x6664,0x6362,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0062,0x6662,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0061,0x6661,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x6500,0x0000,0x0000,0x0000,
	0x0000,0x0000,0x0000,0x0000,0x6400,0x0000,0x0000,0x0000
};

/*

All of Quake's data access is through a hierchal file system, but the contents
of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all
game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.
This can be overridden with the "-basedir" command line parm to allow code
debugging in a different directory.  The base directory is only used during
filesystem initialization.

The "game directory" is the first tree on the search path and directory that all
generated files (savegames, screenshots, demos, config files) will be saved to.
This can be overridden with the "-game" command line parameter.  The game
directory can never be changed while quake is executing.  This is a precacution
against having a malicious server instruct clients to write files over areas they
shouldn't.

The "cache directory" is only used during development to save network bandwidth,
especially over ISDN / T1 lines.  If there is a cache directory specified, when
a file is found by the normal search path, it will be mirrored into the cache
directory, then opened there.

FIXME:
The file "parms.txt" will be read out of the game directory and appended to the
current command line arguments to allow different games to initialize startup
parms differently.  This could be used to add a "-sspeed 22050" for the high
quality sound edition.  Because they are added at the end, they will not
override an explicit setting on the original command line.

*/

//============================================================================


// ClearLink is used for new headnodes
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

void InsertLinkAfter (link_t *l, link_t *after)
{
	l->next = after->next;
	l->prev = after;
	l->prev->next = l;
	l->next->prev = l;
}

/*
============================================================================

							DYNAMIC VECTORS

============================================================================
*/

// woods #modsmenu #demosmenu (iw)

void Vec_Grow(void** pvec, size_t element_size, size_t count)
{
	vec_header_t header;
	if (*pvec)
		header = VEC_HEADER(*pvec);
	else
		header.size = header.capacity = 0;

	if (header.size + count > header.capacity)
	{
		void* new_buffer;
		size_t total_size;

		header.capacity = header.size + count;
		header.capacity += header.capacity >> 1;
		if (header.capacity < 16)
			header.capacity = 16;
		total_size = sizeof(vec_header_t) + header.capacity * element_size;

		if (*pvec)
			new_buffer = realloc(((vec_header_t*)*pvec) - 1, total_size);
		else
			new_buffer = malloc(total_size);
		if (!new_buffer)
		{
			char errorMsg[128];
			snprintf(errorMsg, sizeof(errorMsg), "Vec_Grow: failed to allocate %llu bytes\n", (unsigned long long)total_size);
			Sys_Error("%s", errorMsg);

		}

		*pvec = 1 + (vec_header_t*)new_buffer;
		VEC_HEADER(*pvec) = header;
	}
}

void Vec_Append(void** pvec, size_t element_size, const void* data, size_t count)
{
	if (!count)
		return;
	Vec_Grow(pvec, element_size, count);
	memcpy((byte*)*pvec + VEC_HEADER(*pvec).size, data, count * element_size);
	VEC_HEADER(*pvec).size += count;
}

void Vec_Clear(void** pvec)
{
	if (*pvec)
		VEC_HEADER(*pvec).size = 0;
}

void Vec_Free(void** pvec)
{
	if (*pvec)
	{
		free(&VEC_HEADER(*pvec));
		*pvec = NULL;
	}
}

/*
============================================================================

					LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/


int q_strnaturalcmp (const char* s1, const char* s2) // woods #iwtabcomplete
{
	qboolean neg1, neg2, sign1, sign2;

	if (s1 == s2)
		return 0;

	neg1 = *s1 == '-';
	neg2 = *s2 == '-';
	sign1 = neg1 || *s1 == '+';
	sign2 = neg2 || *s2 == '+';

	// early out if strings start with different signs followed by digits
	if (neg1 != neg2 && q_isdigit (s1[sign1]) && q_isdigit (s1[sign2]))
		return neg2 - neg1;

skip_prefix:
	while (*s1 && !q_isdigit(*s1) && q_toupper(*s1) == q_toupper(*s2))
	{
		s1++;
		s2++;
		continue;
	}

	if (q_isdigit(*s1) && q_isdigit(*s2))
	{
		const char* begin1 = s1++;
		const char* begin2 = s2++;
		int diff, sign;

		while (*begin1 == '0')
			begin1++;
		while (*begin2 == '0')
			begin2++;

		while (q_isdigit(*s1))
			s1++;
		while (q_isdigit(*s2))
			s2++;

		sign = neg1 ? -1 : 1;

		diff = (s1 - begin1) - (s2 - begin2);
		if (diff)
			return diff * sign;

		while (begin1 != s1)
		{
			diff = *begin1++ - *begin2++;
			if (diff)
				return diff * sign;
		}

		// We only support negative numbers at the beginning of strings so that
		// "-2" is sorted before "-1", but "file-2345.ext" *after* "file-1234.ext".
		neg1 = neg2 = false;

		goto skip_prefix;
	}

	return q_toupper(*s1) - q_toupper(*s2);
}

int char_to_int (const char* str, int len) // woods #demolistsort
{
	int result = 0;
	for (int i = 0; i < len; ++i) 
	{
		if (!isdigit(str[i])) return -1;  // Invalid character for conversion
		result = result * 10 + (str[i] - '0');
	}
	return result;
}

int find_and_parse_date_time (const char* str, int* year, int* month, int* day, int* hour, int* min, int* sec) // woods #demolistsort
{
	// Ensure the string is in the expected format "YYYY-MM-DD HH:MM:SS"
	if (strlen(str) != 19) return 0;

	*year = char_to_int(str, 4);
	*month = char_to_int(str + 5, 2);
	*day = char_to_int(str + 8, 2);
	*hour = char_to_int(str + 11, 2);
	*min = char_to_int(str + 14, 2);
	*sec = char_to_int(str + 17, 2);

	if (*year == -1 || *month == -1 || *day == -1 || *hour == -1 || *min == -1 || *sec == -1) {
		return 0;  // Parsing failed
	}
	return 1;  // Successful parsing
}

int q_sortdemos (const char* s1, const char* s2) // woods #demolistsort
{
	int year1, month1, day1, hour1, min1, sec1;
	int year2, month2, day2, hour2, min2, sec2;

	int s1_has_datetime = find_and_parse_date_time(s1, &year1, &month1, &day1, &hour1, &min1, &sec1);
	int s2_has_datetime = find_and_parse_date_time(s2, &year2, &month2, &day2, &hour2, &min2, &sec2);

	if (s1_has_datetime && s2_has_datetime) 
	{
		// Compare each component starting from the year down to the second
		if (year1 != year2) return year1 - year2;  // Oldest year first
		if (month1 != month2) return month1 - month2;  // Oldest month first
		if (day1 != day2) return day1 - day2;  // Oldest day first
		if (hour1 != hour2) return hour1 - hour2;  // Oldest hour first
		if (min1 != min2) return min1 - min2;  // Oldest minute first
		return sec1 - sec2;  // Oldest second first
	}

	// If one or both strings don't contain valid dates, use a fallback comparison
	return strcmp(s1, s2);
}

char* Q_strnset(char* str, int c, size_t n) // woods
{
	size_t i;
	for (i = 0; i < n && str[i]; i++) {
		str[i] = c;
	}
	return str;
}

// woods strstr case in-sensitive (written by chatGPT)

char* Q_strcasestr(const char* haystack, const char* needle)
{
	size_t haystack_len = strlen(haystack);
	size_t needle_len = strlen(needle);
	char* haystack_lower = (char*)malloc(haystack_len + 1);
	char* needle_lower = (char*)malloc(needle_len + 1);

	if (!haystack_lower || !needle_lower) {
		free(haystack_lower);
		free(needle_lower);
		return NULL;
	}

	for (size_t i = 0; i < haystack_len; i++) {
		haystack_lower[i] = tolower(haystack[i]);
	}
	haystack_lower[haystack_len] = '\0';

	for (size_t i = 0; i < needle_len; i++) {
		needle_lower[i] = tolower(needle[i]);
	}
	needle_lower[needle_len] = '\0';

	char* result = strstr(haystack_lower, needle_lower);
	if (result) {
		result = (char*)haystack + (result - haystack_lower);
	}

	free(haystack_lower);
	free(needle_lower);

	return result;
}

// woods string reverse

#define SWAP(T, a, b) \
    do { T save = (a); (a) = (b); (b) = save; } while (0)

char* Q_strrev(char* s)
{
	size_t len = strlen(s);

	if (len > 1) {
		char* a = s;
		char* b = s + len - 1;

		for (; a < b; ++a, --b)
			SWAP(char, *a, *b);
	}

	return s;
}

// woods string remove

char* strremove(char* str, char* sub) {
	char* p, * q, * r;
	if (*sub && (q = r = strstr(str, sub)) != NULL) {
		size_t len = strlen(sub);
		while ((r = strstr(p = r + len, sub)) != NULL) {
			while (p < r)
				*q++ = *p++;
		}
		while ((*q++ = *p++) != '\0')
			continue;
	}
	return str;
}

int q_strcasecmp(const char * s1, const char * s2)
{
	const char * p1 = s1;
	const char * p2 = s2;
	char c1, c2;

	if (p1 == p2)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0')
			break;
	} while (c1 == c2);

	return (int)(c1 - c2);
}

int q_strncasecmp(const char *s1, const char *s2, size_t n)
{
	const char * p1 = s1;
	const char * p2 = s2;
	char c1, c2;

	if (p1 == p2 || n == 0)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0' || c1 != c2)
			break;
	} while (--n > 0);

	return (int)(c1 - c2);
}

//spike -- grabbed this from fte, because its useful to me
char *q_strcasestr(const char *haystack, const char *needle)
{
	int c1, c2, c2f;
	int i;
	c2f = *needle;
	if (c2f >= 'a' && c2f <= 'z')
		c2f -= ('a' - 'A');
	if (!c2f)
		return (char*)haystack;
	while (1)
	{
		c1 = *haystack;
		if (!c1)
			return NULL;
		if (c1 >= 'a' && c1 <= 'z')
			c1 -= ('a' - 'A');
		if (c1 == c2f)
		{
			for (i = 1; ; i++)
			{
				c1 = haystack[i];
				c2 = needle[i];
				if (c1 >= 'a' && c1 <= 'z')
					c1 -= ('a' - 'A');
				if (c2 >= 'a' && c2 <= 'z')
					c2 -= ('a' - 'A');
				if (!c2)
					return (char*)haystack;	//end of needle means we found a complete match
				if (!c1)	//end of haystack means we can't possibly find needle in it any more
					return NULL;
				if (c1 != c2)	//mismatch means no match starting at haystack[0]
					break;
			}
		}
		haystack++;
	}
	return NULL;	//didn't find it
}

char *q_strlwr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = q_tolower(*c);
		c++;
	}
	return str;
}

char *q_strupr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = q_toupper(*c);
		c++;
	}
	return str;
}

/* platform dependant (v)snprintf function names: */
#if defined(_WIN32)
#define	snprintf_func		_snprintf
#define	vsnprintf_func		_vsnprintf
#else
#define	snprintf_func		snprintf
#define	vsnprintf_func		vsnprintf
#endif

int q_vsnprintf(char *str, size_t size, const char *format, va_list args)
{
	int		ret;

	ret = vsnprintf_func (str, size, format, args);

	if (ret < 0)
		ret = (int)size;
	if (size == 0)	/* no buffer */
		return ret;
	if ((size_t)ret >= size)
		str[size - 1] = '\0';

	return ret;
}

int q_snprintf (char *str, size_t size, const char *format, ...)
{
	int		ret;
	va_list		argptr;

	va_start (argptr, format);
	ret = q_vsnprintf (str, size, format, argptr);
	va_end (argptr);

	return ret;
}

void Q_memset (void *dest, int fill, size_t count)
{
	size_t		i;

	if ( (((uintptr_t)dest | count) & 3) == 0)
	{
		count >>= 2;
		fill = fill | (fill<<8) | (fill<<16) | (fill<<24);
		for (i = 0; i < count; i++)
			((int *)dest)[i] = fill;
	}
	else
		for (i = 0; i < count; i++)
			((byte *)dest)[i] = fill;
}

void Q_memcpy (void *dest, const void *src, size_t count)
{
	size_t		i;

	if (( ( (uintptr_t)dest | (uintptr_t)src | count) & 3) == 0)
	{
		count >>= 2;
		for (i = 0; i < count; i++)
			((int *)dest)[i] = ((int *)src)[i];
	}
	else
		for (i = 0; i < count; i++)
			((byte *)dest)[i] = ((byte *)src)[i];
}

int Q_memcmp (const void *m1, const void *m2, size_t count)
{
	while(count)
	{
		count--;
		if (((byte *)m1)[count] != ((byte *)m2)[count])
			return -1;
	}
	return 0;
}

void Q_strcpy (char *dest, const char *src)
{
	while (*src)
	{
		*dest++ = *src++;
	}
	*dest++ = 0;
}

void Q_strncpy (char *dest, const char *src, int count)
{
	while (*src && count--)
	{
		*dest++ = *src++;
	}
	if (count)
		*dest++ = 0;
}

int Q_strlen (const char *str)
{
	int		count;

	count = 0;
	while (str[count])
		count++;

	return count;
}

char *Q_strrchr(const char *s, char c)
{
	int len = Q_strlen(s);
	s += len;
	while (len--)
	{
		if (*--s == c)
			return (char *)s;
	}
	return NULL;
}

void Q_strcat (char *dest, const char *src)
{
	dest += Q_strlen(dest);
	Q_strcpy (dest, src);
}

int Q_strcmp (const char *s1, const char *s2)
{
	while (1)
	{
		if (*s1 != *s2)
			return -1;		// strings not equal
		if (!*s1)
			return 0;		// strings are equal
		s1++;
		s2++;
	}

	return -1;
}

int Q_strncmp (const char *s1, const char *s2, int count)
{
	while (1)
	{
		if (!count--)
			return 0;
		if (*s1 != *s2)
			return -1;		// strings not equal
		if (!*s1)
			return 0;		// strings are equal
		s1++;
		s2++;
	}

	return -1;
}

int Q_atoi (const char *str)
{
	int		val;
	int		sign;
	int		c;

	while (q_isspace (*str))
		++str;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val<<4) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val<<4) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val<<4) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	while (1)
	{
		c = *str++;
		if (c <'0' || c > '9')
			return val*sign;
		val = val*10 + c - '0';
	}

	return 0;
}


float Q_atof (const char *str)
{
	double		val;
	int		sign;
	int		c;
	int	decimal, total;

	while (q_isspace (*str))
		++str;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val*16) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val*16) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val*16) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	decimal = -1;
	total = 0;
	while (1)
	{
		c = *str++;
		if (c == '.')
		{
			decimal = total;
			continue;
		}
		if (c <'0' || c > '9')
			break;
		val = val*10 + c - '0';
		total++;
	}

	if (decimal == -1)
		return val*sign;
	while (total > decimal)
	{
		val /= 10;
		total--;
	}

	return val*sign;
}

// Q_ftoa: convert IEEE 754 float to a base-10 string with "infinite" decimal places
void Q_ftoa(char *str, float in)
{
	struct {
		float f;
		unsigned int i;
	} u = {in};

	int signbit = (u.i & 0x80000000) >> 31;
	int exp = (signed int)((u.i & 0x7F800000) >> 23) - 127;
	int mantissa = (u.i & 0x007FFFFF);

	if (exp == 128) // 255(NaN/Infinity bits) - 127(bias)
	{
		if (signbit)
		{
			*str = '-';
			str++;
		}
		if (mantissa == 0) // infinity
			strcpy(str, "1.#INF");
		else // NaN or indeterminate
			strcpy(str, "1.#NAN");
		return;
	}

	exp = -exp;
	exp = (int)(exp * 0.30102999957f); // convert base 2 to base 10
	exp += 8;

	if (exp <= 0)
		sprintf(str, "%.0f", in);
	else
	{
		char tstr[32];
		char *lsig = str - 1;
		sprintf(tstr, "%%.%if", exp);
		sprintf(str, tstr, in);
		// find last significant digit and trim
		while (*str)
		{
			if (*str >= '1' && *str <= '9')
				lsig = str;
			else if (*str == '.')
				lsig = str - 1;
			str++;
		}
		lsig[1] = '\0';
	}
}

int wildcmp(const char *wild, const char *string)
{	//case-insensitive string compare with wildcards. returns true for a match.
	while (*string)
	{
		if (*wild == '*')
		{
			if (*string == '/' || *string == '\\')
			{
				//* terminates if we get a match on the char following it, or if its a \ or / char
				wild++;
				continue;
			}
			if (wildcmp(wild+1, string))
				return true;
			string++;
		}
		else if ((q_tolower(*wild) == q_tolower(*string)) || (*wild == '?'))
		{
			//this char matches
			wild++;
			string++;
		}
		else
		{
			//failure
			return false;
		}
	}

	while (*wild == '*')
	{
		wild++;
	}
	return !*wild;
}

void Info_RemoveKey(char *info, const char *key)
{	//only shrinks, so no need for max size.
	size_t keylen = strlen(key);

	while(*info)
	{
		char *l = info;
		if (*info++ != '\\')
			break;	//error / end-of-string

		if (!strncmp(info, key, keylen) && info[keylen] == '\\')
		{
			//skip the key name
			info += keylen+1;
			//this is the old value for the key. skip over it
			while (*info && *info != '\\')
				info++;

			//okay, we found it. strip it out now.
			memmove(l, info, strlen(info)+1);
			return;
		}
		else
		{
			//skip the key
			while (*info && *info != '\\')
				info++;

			//validate that its a value now
			if (*info++ != '\\')
				break;	//error
			//skip the value
			while (*info && *info != '\\')
				info++;
		}
	}
}
void Info_SetKey(char *info, size_t infosize, const char *key, const char *val)
{
	size_t keylen = strlen(key);
	size_t vallen = strlen(val);

	Info_RemoveKey(info, key);

	if (vallen)
	{
		char *o = info + strlen(info);
		char *e = info + infosize-1;

		if (!*key || strchr(key, '\\') || strchr(val, '\\'))
			Con_Warning("Info_SetKey(%s): invalid key/value\n", key);
		else if (o + 2 + keylen + vallen >= e)
			Con_Warning("Info_SetKey(%s): length exceeds max\n", key);
		else
		{
			*o++ = '\\';
			memcpy(o, key, keylen);
			o += keylen;
			*o++ = '\\';
			memcpy(o, val, vallen);
			o += vallen;

			*o = 0;
		}
	}
}
const char *Info_GetKey(const char *info, const char *key, char *out, size_t outsize)
{
	// woods -- check that input pointers are not NULL
	if ((uintptr_t)info == 0xFFFFFFFFFFFFE000)
	{
		Con_DPrintf("error: NULL pointer passed to Info_GetKey function.\n");
		return NULL;
	}
	
	const char *r = out;
	size_t keylen = strlen(key);

	outsize--;

	while(*info)
	{
		if (*info++ != '\\')
			break;	//error / end-of-string

		if (!strncmp(info, key, keylen) && info[keylen] == '\\')
		{
			//skip the key name
			info += keylen+1;
			//this is the value for the key. copy it out
			while (*info && *info != '\\' && outsize-->0)
				*out++ = *info++;
			break;
		}
		else
		{
			//skip the key
			while (*info && *info != '\\')
				info++;

			//validate that its a value now
			if (*info++ != '\\')
				break;	//error
			//skip the value
			while (*info && *info != '\\')
				info++;
		}
	}
	*out = 0;
	return r;
}

void Info_Enumerate(const char *info, void(*cb)(void *ctx, const char *key, const char *value), void *cbctx)
{
	char key[1024];
	char val[1024];
	size_t kl, vl;
	while(*info)
	{
		kl=vl=0;
		if (*info++ != '\\')
			break;	//error / end-of-string

		//skip the key
		while (*info && *info != '\\')
		{
			if (kl < sizeof(key)-1)
				key[kl++] = *info;
			info++;
		}

		//validate that its a value now
		if (*info++ != '\\')
			break;	//error
		//skip the value
		while (*info && *info != '\\')
		{
			if (vl < sizeof(val)-1)
				val[vl++] = *info;
			info++;
		}

		key[kl] = 0;
		val[vl] = 0;
		cb(cbctx, key, val);
	}
}
static void Info_Print_Callback(void *ctx, const char *key, const char *val)
{
	Con_Printf("%20s: %s\n", key, val);
}
void Info_Print(const char *info)
{
	Info_Enumerate(info, Info_Print_Callback, NULL);
}


/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

qboolean	host_bigendian;

short	(*BigShort) (short l);
short	(*LittleShort) (short l);
int	(*BigLong) (int l);
int	(*LittleLong) (int l);
float	(*BigFloat) (float l);
float	(*LittleFloat) (float l);

short ShortSwap (short l)
{
	byte	b1, b2;

	b1 = l&255;
	b2 = (l>>8)&255;

	return (b1<<8) + b2;
}

short ShortNoSwap (short l)
{
	return l;
}

int LongSwap (int l)
{
	byte	b1, b2, b3, b4;

	b1 = l&255;
	b2 = (l>>8)&255;
	b3 = (l>>16)&255;
	b4 = (l>>24)&255;

	return ((int)b1<<24) + ((int)b2<<16) + ((int)b3<<8) + b4;
}

int LongNoSwap (int l)
{
	return l;
}

float FloatSwap (float f)
{
	union
	{
		float	f;
		byte	b[4];
	} dat1, dat2;


	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

float FloatNoSwap (float f)
{
	return f;
}

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < -128 || c > 127)
		Sys_Error ("MSG_WriteChar: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < 0 || c > 255)
		Sys_Error ("MSG_WriteByte: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Sys_Error ("MSG_WriteShort: range error");
#endif

	buf = (byte *) SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte	*buf;

	buf = (byte *) SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

void MSG_WriteUInt64 (sizebuf_t *sb, unsigned long long c)
{	//0* 10*,*, 110*,*,* etc, up to 0xff followed by 8 continuation bytes
	byte *buf;
	int b = 0;
	unsigned long long l = 128;
	while (c > l-1u)
	{	//count the extra bytes we need
		b++;
		l <<= 7;	//each byte we add gains 8 bits, but we spend one on length.
	}
	buf = (byte*)SZ_GetSpace (sb, 1+b);
	*buf++ = 0xffu<<(8-b) | (c >> (b*8));
	while(b --> 0)
		*buf++ = (c >> (b*8))&0xff;
}
void MSG_WriteInt64 (sizebuf_t *sb, long long c)
{	//move the sign bit into the low bit and avoid sign extension for more efficient length coding.
	if (c < 0)
		MSG_WriteUInt64(sb, ((unsigned long long)(-1-c)<<1)|1);
	else
		MSG_WriteUInt64(sb, c<<1);
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float	f;
		int	l;
	} dat;

	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteDouble (sizebuf_t *sb, double f)
{
	union
	{
		double	f;
		int64_t	l;
	} dat;
	byte *o = SZ_GetSpace (sb, sizeof(f));
	dat.f = f;

	o[0] = dat.l>>0;
	o[1] = dat.l>>8;
	o[2] = dat.l>>16;
	o[3] = dat.l>>24;
	o[4] = dat.l>>32;
	o[5] = dat.l>>40;
	o[6] = dat.l>>48;
	o[7] = dat.l>>56;
}

void MSG_WriteString (sizebuf_t *sb, const char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, Q_strlen(s)+1);
}
void MSG_WriteStringUnterminated (sizebuf_t *sb, const char *s)
{
	SZ_Write (sb, s, Q_strlen(s));
}

//johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
void MSG_WriteCoord16 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, Q_rint(f*8));
}

//johnfitz -- 16.8 fixed point coords, max range +-32768
void MSG_WriteCoord24 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, f);
	MSG_WriteByte (sb, (int)(f*255)%255);
}

//johnfitz -- 32-bit float coords
void MSG_WriteCoord32f (sizebuf_t *sb, float f)
{
	MSG_WriteFloat (sb, f);
}

void MSG_WriteCoord (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATCOORD)
		MSG_WriteFloat (sb, f);
	else if (flags & PRFL_INT32COORD)
		MSG_WriteLong (sb, Q_rint (f * 16));
	else if (flags & PRFL_24BITCOORD)
		MSG_WriteCoord24 (sb, f);
	else MSG_WriteCoord16 (sb, f);
}

void MSG_WriteAngle (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		MSG_WriteFloat (sb, f);
	else if (flags & PRFL_SHORTANGLE)
		MSG_WriteShort (sb, Q_rint(f * 65536.0 / 360.0) & 65535);
	else MSG_WriteByte (sb, Q_rint(f * 256.0 / 360.0) & 255); //johnfitz -- use Q_rint instead of (int)	}
}

//johnfitz -- for PROTOCOL_FITZQUAKE
void MSG_WriteAngle16 (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		MSG_WriteFloat (sb, f);
	else MSG_WriteShort (sb, Q_rint(f * 65536.0 / 360.0) & 65535);
}
//johnfitz

//spike -- for PEXT2_REPLACEMENTDELTAS
void MSG_WriteEntity (sizebuf_t *sb, unsigned int entnum, unsigned int pext2)
{
	//high short, low byte
	if (entnum > 0x7fff && (pext2 & PEXT2_REPLACEMENTDELTAS))
	{
		MSG_WriteShort(sb, 0x8000|(entnum>>8));
		MSG_WriteByte(sb, entnum&0xff);
	}
	else
		MSG_WriteShort(sb, entnum);
}

//
// reading functions
//
int		msg_readcount;
qboolean	msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
int MSG_ReadChar (void)
{
	int	c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (signed char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadByte (void)
{
	int	c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int	c;

	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (short)(net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount+1]<<8));

	msg_readcount += 2;

	return c;
}

int MSG_ReadLong (void)
{
	int	c;

	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount+1]<<8)
			+ (net_message.data[msg_readcount+2]<<16)
			+ (net_message.data[msg_readcount+3]<<24);

	msg_readcount += 4;

	return c;
}

unsigned long long MSG_ReadUInt64 (void)
{	//0* 10*,*, 110*,*,* etc, up to 0xff followed by 8 continuation bytes
	byte l=0x80, v, b = 0;
	unsigned long long r;
	v = MSG_ReadByte();
	for (; v&l; l>>=1)
	{
		v-=l;
		b++;
	}
	r = v<<(b*8);
	while(b --> 0)
		r |= MSG_ReadByte()<<(b*8);
	return r;
}
long long MSG_ReadInt64 (void)
{	//we do some fancy bit recoding for more efficient length coding.
	unsigned long long c = MSG_ReadUInt64();
	if (c&1)
		return -1-(long long)(c>>1);
	else
		return (long long)(c>>1);
}

float MSG_ReadFloat (void)
{
	union
	{
		byte	b[4];
		float	f;
		int	l;
	} dat;

	dat.b[0] = net_message.data[msg_readcount];
	dat.b[1] = net_message.data[msg_readcount+1];
	dat.b[2] = net_message.data[msg_readcount+2];
	dat.b[3] = net_message.data[msg_readcount+3];
	msg_readcount += 4;

	dat.l = LittleLong (dat.l);

	return dat.f;
}
float MSG_ReadDouble (void)
{
	union
	{
		double	f;
		uint64_t	l;
	} dat;

	dat.l = ((uint64_t)net_message.data[msg_readcount  ]<<0 )	|
			((uint64_t)net_message.data[msg_readcount+1]<<8 )	|
			((uint64_t)net_message.data[msg_readcount+2]<<16)	|
			((uint64_t)net_message.data[msg_readcount+3]<<24)	|
			((uint64_t)net_message.data[msg_readcount+4]<<32)	|
			((uint64_t)net_message.data[msg_readcount+5]<<40)	|
			((uint64_t)net_message.data[msg_readcount+6]<<48)	|
			((uint64_t)net_message.data[msg_readcount+7]<<56)	;
	msg_readcount += 8;

	return dat.f;
}

const char *MSG_ReadString (void)
{
	static char	string[2048];
	int		c;
	size_t		l;

	l = 0;
	do
	{
		c = MSG_ReadByte ();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}

//johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
float MSG_ReadCoord16 (void)
{
	return MSG_ReadShort() * (1.0/8);
}

//johnfitz -- 16.8 fixed point coords, max range +-32768
float MSG_ReadCoord24 (void)
{
	return MSG_ReadShort() + MSG_ReadByte() * (1.0/255);
}

//johnfitz -- 32-bit float coords
float MSG_ReadCoord32f (void)
{
	return MSG_ReadFloat();
}

float MSG_ReadCoord (unsigned int flags)
{
	if (flags & PRFL_FLOATCOORD)
		return MSG_ReadFloat ();
	else if (flags & PRFL_INT32COORD)
		return MSG_ReadLong () * (1.0 / 16.0);
	else if (flags & PRFL_24BITCOORD)
		return MSG_ReadCoord24 ();
	else return MSG_ReadCoord16 ();
}

float MSG_ReadAngle (unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		return MSG_ReadFloat ();
	else if (flags & PRFL_SHORTANGLE)
		return MSG_ReadShort () * (360.0 / 65536);
	else return MSG_ReadChar () * (360.0 / 256);
}

//johnfitz -- for PROTOCOL_FITZQUAKE
float MSG_ReadAngle16 (unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		return MSG_ReadFloat ();	// make sure
	else return MSG_ReadShort () * (360.0 / 65536);
}
//johnfitz

unsigned int MSG_ReadEntity(unsigned int pext2)
{
	unsigned int e = (unsigned short)MSG_ReadShort();
	if (pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		if (e & 0x8000)
		{
			e = (e & 0x7fff) << 8;
			e |= MSG_ReadByte();
		}
	}
	return e;
}

//spike -- for downloads
byte *MSG_ReadData (unsigned int length)
{
	byte *data;

	if (msg_readcount+length > (unsigned int)net_message.cursize)
	{
		msg_badread = true;
		return NULL;
	}

	data = net_message.data+msg_readcount;
	msg_readcount += length;
	return data;
}


//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;
	buf->data = (byte *) Hunk_AllocName (startsize, "sizebuf");
	buf->maxsize = startsize;
	buf->cursize = 0;
}


void SZ_Free (sizebuf_t *buf)
{
//	Z_Free (buf->data);
//	buf->data = NULL;
//	buf->maxsize = 0;
	buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
	buf->overflowed = false;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void	*data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Host_Error ("SZ_GetSpace: overflow without allowoverflow set"); // ericw -- made Host_Error to be less annoying

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		Con_Printf ("SZ_GetSpace: overflow\n");
		SZ_Clear (buf);
		buf->overflowed = true;
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	Q_memcpy (SZ_GetSpace(buf,length),data,length);
}

void SZ_Print (sizebuf_t *buf, const char *data)
{
	int		len = Q_strlen(data) + 1;

	if (buf->data[buf->cursize-1])
	{	/* no trailing 0 */
		Q_memcpy ((byte *)SZ_GetSpace(buf, len  )  , data, len);
	}
	else
	{	/* write over trailing 0 */
		Q_memcpy ((byte *)SZ_GetSpace(buf, len-1)-1, data, len);
	}
}


//============================================================================

/*
============
COM_SkipPath -- woods #texturepointer
============
*/
const char *COM_SkipPath (const char *pathname)
{
	const char	*last;

	last = pathname;
	while (*pathname)
	{
		if (*pathname == '/')
			last = pathname + 1;
		pathname++;
	}
	return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (const char *in, char *out, size_t outsize)
{
	int	length;

	if (!*in)
	{
		*out = '\0';
		return;
	}
	if (in != out)	/* copy when not in-place editing */
		q_strlcpy (out, in, outsize);
	length = (int)strlen(out) - 1;
	while (length > 0 && out[length] != '.')
	{
		--length;
		if (out[length] == '/' || out[length] == '\\')
			return;	/* no extension */
	}
	if (length > 0)
		out[length] = '\0';
}

/*
============
COM_SkipColon -- woods #texturepointer
============
*/
const char* COM_SkipColon (const char* str)
{
	const char* last;

	last = str;
	while (*str)
	{
		if (*str == ':')
			last = str + 1;
		str++;
	}
	return last;
}

/*
============
COM_StripPort -- woods #historymenu
============
*/
const char* COM_StripPort (const char* str)
{
	const char* colon = strchr(str, ':');
	size_t length = colon ? (colon - str) : strlen(str);
	char* newStr = malloc(length + 1);
	if (newStr)
	{
		strncpy(newStr, str, length);
		newStr[length] = '\0';
	}
	return newStr;
}

/*
============
COM_FileGetExtension - doesn't return NULL
============
*/
const char *COM_FileGetExtension (const char *in)
{
	const char	*src;
	size_t		len;

	len = strlen(in);
	if (len < 2)	/* nothing meaningful */
		return "";

	src = in + len - 1;
	while (src != in && src[-1] != '.')
		src--;
	if (src == in || strchr(src, '/') != NULL || strchr(src, '\\') != NULL)
		return "";	/* no extension, or parent directory has a dot */

	return src;
}

/*
============
COM_ExtractExtension
============
*/
void COM_ExtractExtension (const char *in, char *out, size_t outsize)
{
	const char *ext = COM_FileGetExtension (in);
	if (! *ext)
		*out = '\0';
	else
		q_strlcpy (out, ext, outsize);
}

/*
============
COM_FileBase
take 'somedir/otherdir/filename.ext',
write only 'filename' to the output
============
*/
void COM_FileBase (const char *in, char *out, size_t outsize)
{
	const char	*dot, *slash, *s;

	s = in;
	slash = in;
	dot = NULL;
	while (*s)
	{
		if (*s == '/')
			slash = s + 1;
		if (*s == '.')
			dot = s;
		s++;
	}
	if (dot == NULL)
		dot = s;

	if (dot - slash < 2)
		q_strlcpy (out, "?model?", outsize);
	else
	{
		size_t	len = dot - slash;
		if (len >= outsize)
			len = outsize - 1;
		memcpy (out, slash, len);
		out[len] = '\0';
	}
}

/*
==================
COM_DefaultExtension -- woods #locext
if path doesn't have a .EXT, append extension
(extension should include the leading ".")
==================
*/
/* can be dangerous */
void COM_DefaultExtension (char *path, const char *extension, size_t len)
{
	char	*src;

	if (!*path) return;
	src = path + strlen(path) - 1;

	while (*src != '/' && *src != '\\' && src != path)
	{
		if (*src == '.')
			return; // it has an extension
		src--;
	}

	q_strlcat(path, extension, len);
}

/*
==================
COM_AddExtension
if path extension doesn't match .EXT, append it
(extension should include the leading ".")
==================
*/
void COM_AddExtension (char *path, const char *extension, size_t len)
{
	if (strcmp(COM_FileGetExtension(path), extension + 1) != 0)
		q_strlcat(path, extension, len);
}

/*
================
COM_TintSubstring -- // woods (ironwail)
================
*/
char* COM_TintSubstring(const char* in, const char* substr, char* out, size_t outsize)
{
	int l;
	char* m = out;
	q_strlcpy(out, in, outsize);
	if (*substr)
	{
		while ((m = q_strcasestr(m, substr)))
		{
			for (l = 0; substr[l]; l++)
				if (m[l] > ' ')
					m[l] |= 0x80;
			m += l;
		}
	}
	return out;
}

/*
================
COM_TintString  --  woods (ironwail)
================
*/
char* COM_TintString(const char* in, char* out, size_t outsize)
{
	char* ret = out;
	if (!outsize)
		return "";
	--outsize;
	while (*in && outsize > 0)
	{
		char c = *in++;
		if (c > ' ')
			c |= 0x80;
		*out++ = c;
		--outsize;
	}
	*out++ = '\0';
	return ret;
}

/*
==============
COM_ParseEx --  woods (ironwail) #mapdescriptions

Parse a token out of a string

The mode argument controls how overflow is handled:
- CPE_NOTRUNC:		return NULL (abort parsing)
- CPE_ALLOWTRUNC:	truncate com_token (ignore the extra characters in this token)
==============
*/
const char* COM_ParseEx (const char* data, cpe_mode mode)
{
	int		c;
	int		len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

	// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;	// end of file
		data++;
	}

	// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

	// skip /*..*/ comments
	if (c == '/' && data[1] == '*')
	{
		data += 2;
		while (*data && !(*data == '*' && data[1] == '/'))
			data++;
		if (*data)
			data += 2;
		goto skipwhite;
	}

	// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			if ((c = *data) != 0)
				++data;
			if (c == '\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			if (len < Q_COUNTOF(com_token) - 1)
				com_token[len++] = c;
			else if (mode == CPE_NOTRUNC)
				return NULL;
		}
	}

	// parse single characters
	if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' || c == ':')
	{
		if (len < Q_COUNTOF(com_token) - 1)
			com_token[len++] = c;
		else if (mode == CPE_NOTRUNC)
			return NULL;
		com_token[len] = 0;
		return data + 1;
	}

	// parse a regular word
	do
	{
		if (len < Q_COUNTOF(com_token) - 1)
			com_token[len++] = c;
		else if (mode == CPE_NOTRUNC)
			return NULL;
		data++;
		c = *data;
		/* commented out the check for ':' so that ip:port works */
		if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\''/* || c == ':' */)
			break;
	} while (c > 32);

	com_token[len] = 0;
	return data;
}

/*
spike -- this function simply says whether a filename is acceptable for downloading (used by both client+server)
*/
qboolean COM_DownloadNameOkay(const char *filename)
{
	if (!allow_download.value)
		return false;

	//quickly test the prefix to ensure that its in one of the allowed subdirs
	if (strncmp(filename, "sound/", 6) && 
		strncmp(filename, "progs/", 6) && 
		strncmp(filename, "maps/", 5) &&
		strncmp(filename, "locs/", 5) && // woods #locdownloads
		strncmp(filename, "models/", 7) &&
		strncmp(filename, "gfx/env/", 8))        /* skybox cube faces  */
		return false;
	//windows paths are NOT permitted, nor are alternative data streams, nor wildcards, and double quotes are always bad(which allows for spaces)
	if (strchr(filename, '\\') || strchr(filename, ':') || strchr(filename, '*') || strchr(filename, '?') || strchr(filename, '\"'))
		return false;
	//some operating systems interpret this as 'parent directory'
	if (strstr(filename, "//"))
		return false;
	//block unix hidden files, also blocks relative paths.
	if (*filename == '.' || strstr(filename, "/."))
		return false;
	//test the extension to ensure that its in one of the allowed file types
	//(no .dll, .so, .com, .exe, .bat, .vbs, .xls, .doc, etc please)
	//also don't allow config files.
	filename = COM_FileGetExtension(filename);
	if (
		//model formats
		q_strcasecmp(filename, "bsp") &&
		q_strcasecmp(filename, "mdl") &&
		q_strcasecmp(filename, "iqm") &&	//in case we ever support these later
		q_strcasecmp(filename, "md3") &&
		q_strcasecmp(filename, "spr") &&
		q_strcasecmp(filename, "spr32") &&
		//audio formats
		q_strcasecmp(filename, "wav") &&
		q_strcasecmp(filename, "ogg") &&
		//image formats (if we ever need that)
		q_strcasecmp(filename, "tga") &&
		q_strcasecmp(filename, "png") &&
		q_strcasecmp(filename, "jpg") &&
		q_strcasecmp(filename, "dds") &&
		//misc stuff
		q_strcasecmp(filename, "lux") &&
		q_strcasecmp(filename, "loc") && // woods #locdownloads
		q_strcasecmp(filename, "lit2") &&
		q_strcasecmp(filename, "lit"))
		return false;
	//okay, well, we didn't throw a hissy fit, so whatever dude, go ahead and download
	return true;
}


/*
==============
COM_Parse

Parse a token out of a string
==============
*/
const char *COM_Parse (const char *data)
{
	int		c;
	int		len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;	// end of file
		data++;
	}

// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

// skip /*..*/ comments
	if (c == '/' && data[1] == '*')
	{
		data += 2;
		while (*data && !(*data == '*' && data[1] == '/'))
			data++;
		if (*data)
			data += 2;
		goto skipwhite;
	}

// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			if ((c = *data) != 0)
				++data;
			if (c == '\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

// parse single characters
	if (c == '{' || c == '}'|| c == '('|| c == ')' || c == '\'' || c == ':')
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data+1;
	}

// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
		/* commented out the check for ':' so that ip:port works */
		if (c == '{' || c == '}'|| c == '('|| c == ')' || c == '\''/* || c == ':' */)
			break;
	} while (c > 32);

	com_token[len] = 0;
	return data;
}


/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParmNext (int last, const char *parm)
{
	int		i;

	for (i = last+1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;		// NEXTSTEP sometimes clears appkit vars.
		if (!Q_strcmp (parm,com_argv[i]))
			return i;
	}

	return 0;
}
int COM_CheckParm (const char *parm)
{
	return COM_CheckParmNext(0, parm);
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
static void COM_CheckRegistered (void)
{
	int		h;
	unsigned short	check[128];
	int		i;

	COM_OpenFile("gfx/pop.lmp", &h, NULL);

	if (h == -1)
	{
		Cvar_SetROM ("registered", "0");

		if (pak0) // woods #pak0only
		{
		Con_Printf ("Playing shareware version.\n");
			return;
		}

		if (com_modified)
			Sys_Error ("You must have the registered version to use modified games.\n\n"
				   "Basedir is: %s\n\n"
				   "Check that this has an " GAMENAME " subdirectory containing pak0.pak and pak1.pak, "
				   "or use the -basedir command-line option to specify another directory.",
				   com_basedir);
		return;
	}

	Sys_FileRead (h, check, sizeof(check));
	COM_CloseFile (h);

	for (i = 0; i < 128; i++)
	{
		if (pop[i] != (unsigned short)BigShort (check[i]))
			Sys_Error ("Corrupted data file.");
	}

	for (i = 0; com_cmdline[i]; i++)
	{
		if (com_cmdline[i]!= ' ')
			break;
	}

	Cvar_SetROM ("cmdline", &com_cmdline[i]);
	Cvar_SetROM ("registered", "1");
	Con_Printf ("Playing registered version.\n");
}


/*
================
COM_InitArgv
================
*/
void COM_InitArgv (int argc, char **argv)
{
	int		i, j, n;

// reconstitute the command line for the cmdline externally visible cvar
	n = 0;

	for (j = 0; (j<MAX_NUM_ARGVS) && (j< argc); j++)
	{
		i = 0;

		while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i])
		{
			com_cmdline[n++] = argv[j][i++];
		}

		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else
			break;
	}

	if (n > 0 && com_cmdline[n-1] == ' ')
		com_cmdline[n-1] = 0; //johnfitz -- kill the trailing space

	Con_Printf("Command line: %s\n", com_cmdline);

	for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++)
	{
		largv[com_argc] = argv[com_argc];
		if (!Q_strcmp ("-safe", argv[com_argc]))
			safemode = 1;
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;

	if (COM_CheckParm ("-rogue"))
	{
		rogue = true;
		standard_quake = false;
	}

	if (COM_CheckParm ("-hipnotic") || COM_CheckParm ("-quoth")) //johnfitz -- "-quoth" support
	{
		hipnotic = true;
		standard_quake = false;
	}
}

entity_state_t nullentitystate;
static void COM_SetupNullState(void)
{
	//the null state has some specific default values
//	nullentitystate.drawflags = /*SCALE_ORIGIN_ORIGIN*/96;
	nullentitystate.colormod[0] = 32;
	nullentitystate.colormod[1] = 32;
	nullentitystate.colormod[2] = 32;
	nullentitystate.glowmod[0] = 32;
	nullentitystate.glowmod[1] = 32;
	nullentitystate.glowmod[2] = 32;
	nullentitystate.alpha = ENTALPHA_DEFAULT;	//fte has 255 by default, with 0 for invisible. fitz uses 1 for invisible, 0 default, and 255=full alpha
	nullentitystate.scale = ENTSCALE_DEFAULT;
	nullentitystate.solidsize = ES_SOLID_NOT;
}

/*
================
COM_Init
================
*/
void COM_Init (void)
{
	int	i = 0x12345678;
		/*    U N I X */

	/*
	BE_ORDER:  12 34 56 78
		   U  N  I  X

	LE_ORDER:  78 56 34 12
		   X  I  N  U

	PDP_ORDER: 34 12 78 56
		   N  U  X  I
	*/
	if ( *(char *)&i == 0x12 )
		host_bigendian = true;
	else if ( *(char *)&i == 0x78 )
		host_bigendian = false;
	else /* if ( *(char *)&i == 0x34 ) */
		Sys_Error ("Unsupported endianism.");

	if (host_bigendian)
	{
		BigShort = ShortNoSwap;
		LittleShort = ShortSwap;
		BigLong = LongNoSwap;
		LittleLong = LongSwap;
		BigFloat = FloatNoSwap;
		LittleFloat = FloatSwap;
	}
	else /* assumed LITTLE_ENDIAN. */
	{
		BigShort = ShortSwap;
		LittleShort = ShortNoSwap;
		BigLong = LongSwap;
		LittleLong = LongNoSwap;
		BigFloat = FloatSwap;
		LittleFloat = FloatNoSwap;
	}

	if (COM_CheckParm("-fitz"))
	{
		fitzmode = true;
		cl_demoreel.string = "1";	//shouldn't be registered yet.
	}

	COM_SetupNullState();
}


/*
============
va

does a varargs printf into a temp buffer. cycles between
4 different static buffers. the number of buffers cycled
is defined in VA_NUM_BUFFS.
FIXME: make this buffer size safe someday
============
*/
#define	VA_NUM_BUFFS	4
#define	VA_BUFFERLEN	1024

static char *get_va_buffer(void)
{
	static char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
	static int buffer_idx = 0;
	buffer_idx = (buffer_idx + 1) & (VA_NUM_BUFFS - 1);
	return va_buffers[buffer_idx];
}

char *va (const char *format, ...)
{
	va_list		argptr;
	char		*va_buf;

	va_buf = get_va_buffer ();
	va_start (argptr, format);
	q_vsnprintf (va_buf, VA_BUFFERLEN, format, argptr);
	va_end (argptr);

	return va_buf;
}

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

qofs_t	com_filesize;


//
// on-disk pakfile
//
typedef struct
{
	char	name[56];
	unsigned int		filepos, filelen;
} dpackfile_t;

typedef struct
{
	char	id[4];
	unsigned int		dirofs;
	unsigned int		dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK	4096

char	com_gamenames[1024];	//eg: "hipnotic;quoth;warp", no id1, no private stuff
char	com_gamedir[MAX_OSPATH];
char	com_basedir[MAX_OSPATH];
int	file_from_pak;		// ZOID: global indicating that file came from a pak

searchpath_t	*com_searchpaths;
searchpath_t	*com_base_searchpaths;

/*
============
COM_Path_f -- woods
============
*/
static void COM_Path_f (void)
{
	searchpath_t* s;
	int				priority = 1;
	int				total_paks = 0;
	int				total_dirs = 0;
	int				total_files = 0;
	char			size_str[32];
	const char* filename;
	char			relative_path[MAX_OSPATH];  // Add this

	// Header
	Con_Printf("\n");
	Con_Printf("^mpriority type    files    size       path^m\n");
	Con_Printf("================================================================================\n");

	for (s = com_searchpaths; s; s = s->next)
	{
		if (s->pack)
		{
			// PAK file entry
			filename = COM_SkipPath(s->pack->filename);

			// Create relative path showing gamedir + filename
			const char* gamedir_name = COM_SkipPath(com_gamedir);
			q_snprintf(relative_path, sizeof(relative_path), "%s/%s", gamedir_name, filename);

			// Calculate approximate size (rough estimate)
			float size_mb = (s->pack->numfiles * 50.0f) / (1024.0f * 1024.0f); // Rough estimate
			if (size_mb >= 1.0f)
				q_snprintf(size_str, sizeof(size_str), "~%.1f MB", size_mb);
			else
				q_snprintf(size_str, sizeof(size_str), "~%.0f KB", size_mb * 1024.0f);

			Con_Printf("^m%2d^m       pak      ^m%4d^m    %-9s  %s\n",
				priority, s->pack->numfiles, size_str, relative_path);

			total_paks++;
			total_files += s->pack->numfiles;
		}
		else
		{
			// Directory entry - show the actual search path directory name
			q_snprintf(relative_path, sizeof(relative_path), "%s/", s->purename);

			Con_Printf("^m%2d^m       dir      ----     -         %s\n",
				priority, relative_path);
			total_dirs++;
		}
		priority++;
	}

	// Footer with totals
	Con_Printf("\n");
	Con_Printf("%d PAK files (%d files), %d directories, %d search paths total\n",
		total_paks, total_files, total_dirs, priority - 1);

	if (total_paks > 0 || total_dirs > 0)
	{
		Con_Printf("\n^msearch order:^m files are loaded from priority 1 first, then 2, etc.\n");
		Con_Printf("files in higher priority paths override files with the same name in lower priority paths.\n");
	}
	Con_Printf("\n");
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
void COM_WriteFile (const char *filename, const void *data, int len)
{
	int		handle;
	char	name[MAX_OSPATH];

	Sys_mkdir (com_gamedir); //johnfitz -- if we've switched to a nonexistant gamedir, create it now so we don't crash

	q_snprintf (name, sizeof(name), "%s/%s", com_gamedir, filename);

	handle = Sys_FileOpenWrite (name);
	if (handle == -1)
	{
		Sys_Printf ("COM_WriteFile: failed on %s\n", name);
		return;
	}

	Sys_Printf ("COM_WriteFile: %s\n", name);
	Sys_FileWrite (handle, data, len);
	Sys_FileClose (handle);
}

/*
============
COM_CreatePath
============
*/
void COM_CreatePath (char *path)
{
	char	*ofs;

	for (ofs = path + 1; *ofs; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
			*ofs = 0;
			Sys_mkdir (path);
			*ofs = '/';
		}
	}
}

/*
================
COM_filelength
================
*/
long COM_filelength (FILE *f)
{
	long		pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

/*
===========
COM_FindFile

Finds the file in the search path.
Sets com_filesize and one of handle or file
If neither of file or handle is set, this
can be used for detecting a file's presence.
===========
*/
static int COM_FindFile (const char *filename, int *handle, FILE **file,
							unsigned int *path_id)
{
	searchpath_t	*search;
	char		netpath[MAX_OSPATH];
	pack_t		*pak;
	int		i;
	const char *ext;

	if (file && handle)
		Sys_Error ("COM_FindFile: both handle and file set");

	file_from_pak = 0;

//
// search through the path, one element at a time
//
	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)	/* look through all the pak file elements */
		{
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (strcmp(pak->files[i].name, filename) != 0)
					continue;
				// found it!
				com_filesize = pak->files[i].filelen;
				file_from_pak = 1;
				if (path_id)
					*path_id = search->path_id;
				if (handle)
				{
					if (pak->files[i].deflatedsize)
					{
						FILE *f;
						f = fopen (pak->filename, "rb");
						if (f)
						{
							fseek (f, pak->files[i].filepos, SEEK_SET);
							f = FSZIP_Deflate(f, pak->files[i].deflatedsize, pak->files[i].filelen, pak->files[i].name);
							if (f)
								*handle = Sys_FileOpenStdio(f);
							else
							{	//error!
								com_filesize = -1;
								*handle = -1;
							}
						}
						else
						{	//error!
							com_filesize = -1;
							*handle = -1;
						}
					}
					else
					{
						*handle = pak->handle;
						Sys_FileSeek (pak->handle, pak->files[i].filepos);
					}
					return com_filesize;
				}
				else if (file)
				{ /* open a new file on the pakfile */
					*file = fopen (pak->filename, "rb");
					if (*file)
					{
						fseek (*file, pak->files[i].filepos, SEEK_SET);
						if (pak->files[i].deflatedsize)
							*file = FSZIP_Deflate(*file, pak->files[i].deflatedsize, pak->files[i].filelen, pak->files[i].name);
					}
					return com_filesize;
				}
				else /* for COM_FileExists() */
				{
					return com_filesize;
				}
			}
		}
		else	/* check a file in the directory tree */
		{
			if (!registered.value)
			{ /* if not a registered version, don't ever go beyond base */
				if ( strchr (filename, '/') || strchr (filename,'\\'))
					continue;
				if (!q_strcasecmp(COM_FileGetExtension(filename), "dat"))	//don't load custom progs.dats either
					continue;
			}

			q_snprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);
			if (! (Sys_FileType(netpath) & FS_ENT_FILE))
				continue;

			if (path_id)
				*path_id = search->path_id;
			if (handle)
			{
				com_filesize = Sys_FileOpenRead (netpath, &i);
				*handle = i;
				return com_filesize;
			}
			else if (file)
			{
				*file = fopen (netpath, "rb");
				com_filesize = (*file == NULL) ? -1 : COM_filelength (*file);
				return com_filesize;
			}
			else
			{
				return 0; /* dummy valid value for COM_FileExists() */
			}
		}
	}

	ext = COM_FileGetExtension(filename);
	if (strcmp(ext, "pcx") != 0
		&& strcmp(ext, "tga") != 0
		&& strcmp(ext, "png") != 0
		&& strcmp(ext, "jpg") != 0
		&& strcmp(ext, "jpeg") != 0
		&& strcmp(ext, "dds") != 0
		&& strcmp(ext, "lmp") != 0
		&& strcmp(ext, "iqm") != 0
		&& strcmp(ext, "md3") != 0
		&& strcmp(ext, "md5mesh") != 0
		&& strcmp(ext, "md5anim") != 0
		&& strcmp(ext, "lit") != 0
		&& strcmp(ext, "loc") != 0 // woods #locdownloads
		&& strcmp(ext, "vis") != 0
		&& strcmp(ext, "ent") != 0)
		Con_DPrintf ("FindFile: can't find %s\n", filename);
	else	Con_DPrintf2("FindFile: can't find %s\n", filename);

	if (handle)
		*handle = -1;
	if (file)
		*file = NULL;
	com_filesize = -1;
	return com_filesize;
}


/*
===========
COM_FileExists

Returns whether the file is found in the quake filesystem.
===========
*/
qboolean COM_FileExists (const char *filename, unsigned int *path_id)
{
	int ret = COM_FindFile (filename, NULL, NULL, path_id);
	return (ret == -1) ? false : true;
}

/*
===========
COM_OpenFile

filename never has a leading slash, but may contain directory walks
returns a handle and a length
it may actually be inside a pak file
===========
*/
int COM_OpenFile (const char *filename, int *handle, unsigned int *path_id)
{
	return COM_FindFile (filename, handle, NULL, path_id);
}

/*
===========
COM_FOpenFile

If the requested file is inside a packfile, a new FILE * will be opened
into the file.
===========
*/
int COM_FOpenFile (const char *filename, FILE **file, unsigned int *path_id)
{
	return COM_FindFile (filename, NULL, file, path_id);
}

/*
============
COM_CloseFile

If it is a pak file handle, don't really close it
============
*/
void COM_CloseFile (int h)
{
	searchpath_t	*s;

	for (s = com_searchpaths; s; s = s->next)
		if (s->pack && s->pack->handle == h)
			return;

	Sys_FileClose (h);
}


/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte.
============
*/
#define	LOADFILE_ZONE		0
#define	LOADFILE_HUNK		1
#define	LOADFILE_TEMPHUNK	2
#define	LOADFILE_CACHE		3
#define	LOADFILE_STACK		4
#define	LOADFILE_MALLOC		5

static byte	*loadbuf;
static cache_user_t *loadcache;
static int	loadsize;

byte *COM_LoadFile (const char *path, int usehunk, unsigned int *path_id)
{
	int		h;
	byte	*buf;
	char	base[32];
	int		len;

	buf = NULL;	// quiet compiler warning

// look for it in the filesystem or pack files
	len = COM_OpenFile (path, &h, path_id);
	if (h == -1)
		return NULL;

// extract the filename base name for hunk tag
	COM_FileBase (path, base, sizeof(base));

	switch (usehunk)
	{
	case LOADFILE_HUNK:
		buf = (byte *) Hunk_AllocName (len+1, base);
		break;
	case LOADFILE_TEMPHUNK:
		buf = (byte *) Hunk_TempAlloc (len+1);
		break;
	case LOADFILE_ZONE:
		buf = (byte *) Z_Malloc (len+1);
		break;
	case LOADFILE_CACHE:
		buf = (byte *) Cache_Alloc (loadcache, len+1, base);
		break;
	case LOADFILE_STACK:
		if (len < loadsize)
			buf = loadbuf;
		else
			buf = (byte *) Hunk_TempAlloc (len+1);
		break;
	case LOADFILE_MALLOC:
		buf = (byte *) malloc (len+1);
		break;
	default:
		Sys_Error ("COM_LoadFile: bad usehunk");
	}

	if (!buf)
		Sys_Error ("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

	Sys_FileRead (h, buf, len);
	COM_CloseFile (h);

	return buf;
}

byte *COM_LoadHunkFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_HUNK, path_id);
}

byte *COM_LoadZoneFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_ZONE, path_id);
}

byte *COM_LoadTempFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_TEMPHUNK, path_id);
}

void COM_LoadCacheFile (const char *path, struct cache_user_s *cu, unsigned int *path_id)
{
	loadcache = cu;
	COM_LoadFile (path, LOADFILE_CACHE, path_id);
}

// uses temp hunk if larger than bufsize
byte *COM_LoadStackFile (const char *path, void *buffer, int bufsize, unsigned int *path_id)
{
	byte	*buf;

	loadbuf = (byte *)buffer;
	loadsize = bufsize;
	buf = COM_LoadFile (path, LOADFILE_STACK, path_id);

	return buf;
}

// returns malloc'd memory
byte *COM_LoadMallocFile (const char *path, unsigned int *path_id)
{
	return COM_LoadFile (path, LOADFILE_MALLOC, path_id);
}

byte *COM_LoadMallocFile_TextMode_OSPath (const char *path, long *len_out)
{
	FILE	*f;
	byte	*data;
	long	len, actuallen;

	// ericw -- this is used by Host_Loadgame_f. Translate CRLF to LF on load games,
	// othewise multiline messages have a garbage character at the end of each line.
	// TODO: could handle in a way that allows loading CRLF savegames on mac/linux
	// without the junk characters appearing.
	f = fopen (path, "rt");
	if (f == NULL)
		return NULL;

	len = COM_filelength (f);
	if (len < 0)
	{
		fclose (f);
		return NULL;
	}

	data = (byte *) malloc (len + 1);
	if (data == NULL)
	{
		fclose (f);
		return NULL;
	}

	// (actuallen < len) if CRLF to LF translation was performed
	actuallen = fread (data, 1, len, f);
	if (ferror(f))
	{
		fclose (f);
		free (data);
		return NULL;
	}
	data[actuallen] = '\0';

	if (len_out != NULL)
		*len_out = actuallen;
	fclose (f);
	return data;
}

const char *COM_ParseIntNewline(const char *buffer, int *value)
{
	int consumed = 0;
	sscanf (buffer, "%i\n%n", value, &consumed);
	return buffer + consumed;
}

const char *COM_ParseFloatNewline(const char *buffer, float *value)
{
	int consumed = 0;
	sscanf (buffer, "%f\n%n", value, &consumed);
	return buffer + consumed;
}

const char *COM_ParseStringNewline(const char *buffer)
{
	int consumed = 0;
	com_token[0] = '\0';
	sscanf (buffer, "%1023s\n%n", com_token, &consumed);
	return buffer + consumed;
}

/*
=================
COM_LoadPackFile -- johnfitz -- modified based on topaz's tutorial

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *COM_LoadPackFile (const char *packfile)
{
	dpackheader_t	header;
	unsigned int i;
	packfile_t	*newfiles;
	unsigned int numpackfiles;
	pack_t		*pack;
	int		packhandle;
	dpackfile_t	info[MAX_FILES_IN_PACK];
	unsigned short	crc;

	if (Sys_FileOpenRead (packfile, &packhandle) == -1)
		return NULL;

	Sys_FileRead (packhandle, (void *)&header, sizeof(header));
	if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
		Sys_Error ("%s is not a packfile", packfile);

	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof(dpackfile_t);

	if (header.dirlen < 0 || header.dirofs < 0)
	{
		Sys_Error ("Invalid packfile %s (dirlen: %i, dirofs: %i)",
					packfile, header.dirlen, header.dirofs);
	}
	if (!numpackfiles)
	{
		Sys_Printf ("WARNING: %s has no files, ignored\n", packfile);
		Sys_FileClose (packhandle);
		return NULL;
	}
	if (numpackfiles > MAX_FILES_IN_PACK)
		Sys_Error ("%s has %i files", packfile, numpackfiles);

	newfiles = (packfile_t *) Z_Malloc(numpackfiles * sizeof(packfile_t));

	Sys_FileSeek (packhandle, header.dirofs);
	Sys_FileRead (packhandle, (void *)info, header.dirlen);

	if (numpackfiles != PAK0_COUNT)
		com_modified = true;	// not the original file
	else
	{
		// crc the directory to check for modifications
		CRC_Init (&crc);
		for (i = 0; i < header.dirlen; i++)
			CRC_ProcessByte (&crc, ((byte *)info)[i]);
		if (crc != PAK0_CRC_V106 && crc != PAK0_CRC_V101 && crc != PAK0_CRC_V100)
			com_modified = true;
		else // woods #pak0only
			pak0 = true;
	}

	// parse the directory
	for (i = 0; i < numpackfiles; i++)
	{
		q_strlcpy (newfiles[i].name, info[i].name, sizeof(newfiles[i].name));
		newfiles[i].filepos = LittleLong(info[i].filepos);
		newfiles[i].filelen = LittleLong(info[i].filelen);
	}

	pack = (pack_t *) Z_Malloc (sizeof (pack_t));
	q_strlcpy (pack->filename, packfile, sizeof(pack->filename));
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	//Sys_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}

#ifdef _WIN32
static time_t Sys_FileTimeToTime(FILETIME ft)
{
	ULARGE_INTEGER ull;
	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;
	return ull.QuadPart / 10000000u - 11644473600u;
}
#endif

void COM_ListSystemFiles(void *ctx, const char *gamedir, const char *ext, qboolean (*cb)(void *ctx, const char *fname))
{
#ifdef _WIN32
	WIN32_FIND_DATA	fdat;
	HANDLE		fhnd;
	char		filestring[MAX_OSPATH];
	q_snprintf (filestring, sizeof(filestring), "%s/*.%s", gamedir, ext);
	fhnd = FindFirstFile(filestring, &fdat);
	if (fhnd == INVALID_HANDLE_VALUE)
		return;
	do
	{
		cb (ctx, fdat.cFileName);
	} while (FindNextFile(fhnd, &fdat));
	FindClose(fhnd);
#else
	DIR		*dir_p;
	struct dirent	*dir_t;
	dir_p = opendir(gamedir);
	if (dir_p == NULL)
		return;
	while ((dir_t = readdir(dir_p)) != NULL)
	{
		if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), ext) != 0)
			continue;
		cb (ctx, dir_t->d_name);
	}
	closedir(dir_p);
#endif
}

static void COM_ListFiles(void *ctx, searchpath_t *spath, const char *pattern, qboolean (*cb)(void *ctx, const char *fname, time_t mtime, size_t fsize, searchpath_t *spath))
{
	char prefixdir[MAX_OSPATH];
	char *sl;
	sl = strrchr(pattern, '/');
	if (sl)
	{
		sl++;
		if (sl-pattern >= MAX_OSPATH)
			return;
		memcpy(prefixdir, pattern, sl-pattern);
		prefixdir[sl-pattern] = 0;
		pattern = sl;
	}
	else
		*prefixdir = 0;

#ifdef _WIN32
	{
		char filestring[MAX_OSPATH];
		WIN32_FIND_DATA	fdat;
		HANDLE		fhnd;
		q_snprintf (filestring, sizeof(filestring), "%s/%s%s", spath->filename, prefixdir, pattern);
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd == INVALID_HANDLE_VALUE)
			return;
		do
		{
			q_snprintf (filestring, sizeof(filestring), "%s%s", prefixdir, fdat.cFileName);
			cb (ctx, filestring, Sys_FileTimeToTime(fdat.ftLastWriteTime), fdat.nFileSizeLow, spath);
		} while (FindNextFile(fhnd, &fdat));
		FindClose(fhnd);
	}
#else
	{
		char filestring[MAX_OSPATH];
		DIR		*dir_p;
		struct dirent	*dir_t;

		q_snprintf (filestring, sizeof(filestring), "%s/%s", spath->filename, prefixdir);
		dir_p = opendir(filestring);
		if (dir_p == NULL)
			return;
		while ((dir_t = readdir(dir_p)) != NULL)
		{
			if (*dir_t->d_name == '.')	//ignore hidden paths... and parent etc weirdness.
				continue;
			if (!fnmatch(pattern, dir_t->d_name, FNM_NOESCAPE|FNM_PATHNAME|FNM_CASEFOLD))
			{
				struct stat s;
				q_snprintf (filestring, sizeof(filestring), "%s/%s%s", spath->filename, prefixdir, dir_t->d_name);
				if (stat(filestring, &s) < 0)
					memset(&s, 0, sizeof(s));

				q_snprintf (filestring, sizeof(filestring), "%s%s", prefixdir, dir_t->d_name);
				cb (ctx, filestring, s.st_mtime, s.st_size, spath);
			}
		}
		closedir(dir_p);
	}
#endif
}
void COM_ListAllFiles(void *ctx, const char *pattern, qboolean (*cb)(void *ctx, const char *fname, time_t mtime, size_t fsize, searchpath_t *spath), unsigned int flags, const char *pkgfilter)
{
	searchpath_t *search;
	const char *sp;
	qboolean foundpackage = false;

	if (*pattern == '/' || strchr(pattern, ':')	//block absolute paths
		|| strchr(pattern, '\\')	//block unportable paths (also ones that mess up other checks)
		|| strstr(pattern, "./"))	//block evil relative paths (any kind)
	{
		Con_Printf("Blocking absolute/non-portable/dodgy search pattern: %s\n", pattern);
		return;
	}

	//don't add the same pak twice.
	for (search = com_searchpaths; search; search = search->next)
	{
		if (pkgfilter)
		{
			if (flags & (1u<<1))
				sp = search->purename;
			else
			{
				sp = strchr(search->purename, '/');
				if (sp && !strchr(++sp, '/'))
					;
				else
					continue;	//ignore packages inside other packages. they're just too weird.
			}
			if (strcmp(pkgfilter, sp))
				continue;	//ignore this package
		}
		foundpackage = true;

		if (search->pack)
		{
			pack_t *pak = search->pack;
			int i;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (wildcmp(pattern, pak->files[i].name))
					cb(ctx, pak->files[i].name, pak->mtime, pak->files[i].filelen, search);
			}
		}
		else
		{
			COM_ListFiles(ctx, search, pattern, cb);
		}
	}

	if (flags & (1u<<1) && (flags & (1u<<2)) && pkgfilter && foundpackage)
	{	//if we're using full package paths, and we're trying to force the search, then be prepared to search gamedirs which are not currently active too if we didn't already search it.
//		searchpath_t dummy;
//		dummy.filename =
		Con_Printf("search_begin: SB_FORCESEARCH not supported\n");
	}
}

static qboolean COM_AddPackage(searchpath_t *basepath, const char *pakfile, const char *purename)
{
	searchpath_t *search;
	pack_t *pak;
	const char *ext = COM_FileGetExtension(pakfile);

	//don't add the same pak twice.
	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
			if (!q_strcasecmp(pakfile, search->pack->filename))
				return true;
	}

	{
		struct stat sb;
		char pakdir[MAX_OSPATH];
		q_snprintf(pakdir, sizeof(pakdir), "%sdir", pakfile);
		if (!stat(pakdir, &sb) && (sb.st_mode&S_IFMT)==S_IFDIR)
		{
			search = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
			q_strlcpy(search->filename, pakdir, sizeof(search->filename));
			q_strlcpy(search->purename, purename, sizeof(search->purename));
			search->path_id = basepath?basepath->path_id:0;	//doesn't count as a new gamedir.
			search->pack = NULL;
			search->next = com_searchpaths;
			com_searchpaths = search;

			com_modified = true;
			return true;
		}
	}

	if (!q_strcasecmp(ext, "pak"))
		pak = COM_LoadPackFile (pakfile);
	else if (!q_strcasecmp(ext, "pk3") || !q_strcasecmp(ext, "pk4") || !q_strcasecmp(ext, "zip") || !q_strcasecmp(ext, "apk") || !q_strcasecmp(ext, "kpf"))
	{
		pak = FSZIP_LoadArchive(pakfile);
		if (pak)
			com_modified = true;	//would always be true, so we don't bother with crcs.
	}
	else
		pak = NULL;

	if (!pak)
		return false;

	{
		struct stat s;
		if (stat(pakfile, &s) >= 0)
			pak->mtime = s.st_mtime;
	}

	search = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
	q_strlcpy(search->filename, pakfile, sizeof(search->filename));
	q_strlcpy(search->purename, purename, sizeof(search->purename));
	search->path_id = basepath?basepath->path_id:0;
	search->pack = pak;
	search->next = com_searchpaths;
	com_searchpaths = search;

	return true;
}

static qboolean COM_AddEnumeratedPackage(void *ctx, const char *pakfile)
{
	searchpath_t *basepath = ctx;
	char fullpakfile[MAX_OSPATH];
	char purepakfile[MAX_OSPATH];
	q_snprintf (fullpakfile, sizeof(fullpakfile), "%s/%s", basepath->filename, pakfile);
	q_snprintf (purepakfile, sizeof(purepakfile), "%s/%s", basepath->purename, pakfile);
	return COM_AddPackage(basepath, fullpakfile, purepakfile);
}

const char *COM_GetGameNames(qboolean full)
{
	if (full)
	{
		if (*com_gamenames)
			return va("%s;%s", GAMENAME, com_gamenames);
		else
			return GAMENAME;
	}
	return com_gamenames;
//	return COM_SkipPath(com_gamedir);
}
//if either contain id1 then that gets ignored
qboolean COM_GameDirMatches(const char *tdirs)
{
	int gnl = strlen(GAMENAME);
	const char *odirs = COM_GetGameNames(false);

	//ignore any core paths.
	if (!strncmp(tdirs, GAMENAME, gnl) && (tdirs[gnl] == ';' || !tdirs[gnl]))
	{
		tdirs+=gnl;
		if (*tdirs == ';')
			tdirs++;
	}
	if (!strncmp(odirs, GAMENAME, gnl) && (odirs[gnl] == ';' || !odirs[gnl]))
	{
		odirs+=gnl;
		if (*odirs == ';')
			odirs++;
	}
	//skip any qw in there from quakeworld (remote servers should really be skipping this, unless its maybe the only one in the path).
	if (!strncmp(tdirs, "qw;", 3) || !strcmp(tdirs, "qw"))
	{
		tdirs+=2;
		if (*tdirs==';')
			tdirs++;
	}
	if (!strncmp(odirs, "qw;", 3) || !strcmp(odirs, "qw"))	//need to cope with ourselves setting it that way too, just in case.
	{
		odirs+=2;
		if (*odirs==';')
			odirs++;
	}

	//okay, now check it properly
	if (!strcmp(odirs, tdirs))
		return true;
	return false;
}

static void COM_AddGameDirectory(const char* dir); // woods #pakdirs

qboolean AddHashedDirectories(void* ctx, const char* fname, time_t mtime, size_t fsize, searchpath_t* spath)
{
	if (*fname == '#') // Checking if the directory starts with #
	{
		const char* combinedName = va("%s/%s", GAMENAME, fname);
		COM_AddGameDirectory(combinedName);
	}
	return true; // Return true to continue processing
}

/*
=================
COM_AddGameDirectory -- johnfitz -- modified based on topaz's tutorial, reworked (woods)
=================
*/
////////////////////////////////////////////////////////////////////
//
// Loading Order Within Each Game Directory:
//
// 1. Base Pak 0 (ALWAYS first)
//    - pak0.pak
//    - Both .pak and .pk3 formats tried for each
//
// 2. Engine Paks (ALWAYS second)
//    - quakespasm.pak
//    - qssm.pak
//    - Both .pak and .pk3 formats tried for each
//
// 3. Base Pak 1 (ALWAYS after engine paks)
//    - pak1.pak
//    - Both .pak and .pk3 formats tried for each
//
// 4. Paks Listed in pak.lst
//    - Loaded in the order specified in pak.lst
//    - Should NOT include engine paks (quakespasm.pak, qssm.pak)
//    - Should NOT include base paks (pak0.pak, pak1.pak)
//    - If pak.lst doesn't exist, falls back to loading pak2+ numerically
//    - Duplicate entries in pak.lst are ignored (first occurrence is used)
//    - Non-existent files in pak.lst are skipped with a warning.
//
// 5. Unlisted Paks (unless -nowildpaks is specified)
//    - Any .pak files not listed in pak.lst
//    - Loaded in alphabetical order
//    - Example: pak2.pak, custom.pak, etc.
//    - Both .pak and .pk3 formats are included
//    - Command-line parameter `-nowildpaks` skips loading these files.
//
// 6. #Directories Within Current Game Directory
//    - Special directories that load as virtual paks
//    - Files in #folders override pak files in the same game directory
//    - Multiple #folders load in alphabetical order
//    - Example: #textures, #models override paks in current directory
//    - Useful for development and easy file overrides
//    - Files in #directories can still be overridden by loose files in 
//      later game directories.
//
// 7. Loose Files in Regular Directories
//    - Files in regular folders (e.g., id1/progs/, id1/maps/)
//    - Searched last within the current game directory
//    - Override by files in later game directories
//    - Priority: Loose Files < #Directories < Paks (including pak.lst)
//
// Game Directory Priority:
// - Each new game directory is added to the FRONT of the search path
// - Later game directories override earlier ones
// - Example order (last has highest priority):
//   * id1/          (base game)
//   * hipnotic/     (mission pack)
//   * mymod/        (custom mod)
//
// Notes:
// - All paths support both .pak and .pk3 formats
// - The -nowildpaks command line parameter disables loading of unlisted paks
// - Files in a later game directory will override ALL content 
//   (including #directories and loose files) from earlier game directories
// - If a .pak or .pk3 file cannot be loaded, it is skipped with a warning.
//
// Example:
// The following files:
// pak2.pak, text.pak, custom.pak, zelda.pak, pak3.pak
// would be loaded in this order:
//
// 1. custom.pak
// 2. pak2.pak
// 3. pak3.pak
// 4. text.pak
// 5. zelda.pak
////////////////////////////////////////////////////////////////////

static void COM_AddGameDirectory (const char *dir)
{
	const char *base = com_basedir;
	int i;
	unsigned int path_id;
	searchpath_t *searchdir;
	char pakfile[MAX_OSPATH];
	char purename[MAX_OSPATH];
	qboolean been_here = false;
	FILE *listing;
	const char* enginepacknames[] = { "quakespasm", "qssm" }; // woods
	int num_enginepacks = sizeof(enginepacknames) / sizeof(enginepacknames[0]); // Number of engine pack names

	if (*dir == '*')
		dir++;
	else if (!strchr(dir, '/') && !strchr(dir, '\\'))
	{
		//fixme: block dupes
		if (*com_gamenames)
			q_strlcat(com_gamenames, ";", sizeof(com_gamenames));
		q_strlcat(com_gamenames, dir, sizeof(com_gamenames));
	}

	//quakespasm enables mission pack flags automatically, so -game rogue works without breaking the hud
//we might as well do that here to simplify the code.
	if (!q_strcasecmp(dir,"rogue")) {
		rogue = true;
		standard_quake = false;
	}
	if (!q_strcasecmp(dir,"hipnotic") || !q_strcasecmp(dir,"quoth")) {
		hipnotic = true;
		standard_quake = false;
	}

	q_strlcpy (com_gamedir, va("%s/%s", base, dir), sizeof(com_gamedir));

	// assign a path_id to this game directory
	if (com_searchpaths && com_searchpaths->path_id)
		path_id = com_searchpaths->path_id << 1;
	else	path_id = 1U;

_add_path:
	searchdir = (searchpath_t *) Z_Malloc(sizeof(searchpath_t));
	searchdir->path_id = path_id;
	q_strlcpy (searchdir->filename, com_gamedir, sizeof(searchdir->filename));
	q_strlcpy (searchdir->purename, dir, sizeof(searchdir->purename));

	// Load pak0.pak
	q_snprintf(pakfile, sizeof(pakfile), "%s/pak0.pak", com_gamedir);
	q_snprintf(purename, sizeof(purename), "%s/pak0.pak", dir);
	COM_AddPackage(searchdir, pakfile, purename);

	// Load pak0.pk3
	q_snprintf(pakfile, sizeof(pakfile), "%s/pak0.pk3", com_gamedir);
	q_snprintf(purename, sizeof(purename), "%s/pak0.pk3", dir);
	COM_AddPackage(searchdir, pakfile, purename);

	//  Load engine paks
	for (i = 0; i < num_enginepacks; i++) 
	{
		const char* enginepackname = enginepacknames[i];
		qboolean old = com_modified;

		if (been_here)
			base = host_parms->userdir;

		// Try both .pak and .pk3 formats for engine paks
		q_snprintf(pakfile, sizeof(pakfile), "%s/%s.pak", base, enginepackname);
		q_snprintf(purename, sizeof(purename), "%s.pak", enginepackname);
		COM_AddPackage(searchdir, pakfile, purename);

		q_snprintf(pakfile, sizeof(pakfile), "%s/%s.pk3", base, enginepackname);
		q_snprintf(purename, sizeof(purename), "%s.pk3", enginepackname);
		COM_AddPackage(searchdir, pakfile, purename);

		com_modified = old;
	}

	// Load pak1.pak
	q_snprintf(pakfile, sizeof(pakfile), "%s/pak1.pak", com_gamedir);
	q_snprintf(purename, sizeof(purename), "%s/pak1.pak", dir);
	COM_AddPackage(searchdir, pakfile, purename);

	// Load pak1.pk3
	q_snprintf(pakfile, sizeof(pakfile), "%s/pak1.pk3", com_gamedir);
	q_snprintf(purename, sizeof(purename), "%s/pak1.pk3", dir);
	COM_AddPackage(searchdir, pakfile, purename);

	// Load additional paks not in pak.lst first
	q_snprintf (pakfile, sizeof(pakfile), "%s/pak.lst", com_gamedir);
	listing = fopen(pakfile, "rb");

	if (!listing)
	{
		// If no pak.lst exists, load remaining paks in alphabetical order
		for (i = 2; ; i++) {
			qboolean found = false;

			q_snprintf(pakfile, sizeof(pakfile), "%s/pak%i.pak", com_gamedir, i);
			q_snprintf(purename, sizeof(purename), "%s/pak%i.pak", dir, i);
			found |= COM_AddPackage(searchdir, pakfile, purename);

			q_snprintf(pakfile, sizeof(pakfile), "%s/pak%i.pk3", com_gamedir, i);
			q_snprintf(purename, sizeof(purename), "%s/pak%i.pk3", dir, i);
			found |= COM_AddPackage(searchdir, pakfile, purename);

			if (!found)
				break;
		}
	}
	else 
	{
		// Read and process pak.lst
		int len;
		char *buffer;
		const char *name;
		fseek(listing, 0, SEEK_END);
		len = ftell(listing);
		fseek(listing, 0, SEEK_SET);
		buffer = Z_Malloc(len+1);

		if (fread(buffer, 1, len, listing) != (size_t)len) {
			Con_Printf("Warning: Failed to read all data from %s\n", pakfile);
			Z_Free(buffer);
			fclose(listing);
			return;
		}

		buffer[len] = 0;
		fclose(listing);

		name = buffer;
		com_modified = true;	//any reordering of paks should be frowned upon
		while ((name = COM_Parse(name))) 
		{
			if (!*com_token)
				continue;
			if (strchr(com_token, '/') || strchr(com_token, '\\') || strchr(com_token, ':'))
				continue;

			// Skip pak0 and pak1 as they were already loaded
			if (q_strncasecmp(com_token, "pak0.", 5) == 0 ||
				q_strncasecmp(com_token, "pak1.", 5) == 0)
				continue;

			q_snprintf (pakfile, sizeof(pakfile), "%s/%s", com_gamedir, com_token);
			q_snprintf (purename, sizeof(purename), "%s/%s", dir, com_token);
			COM_AddPackage(searchdir, pakfile, purename);
		}

		Z_Free(buffer);
	}

	// Load wildcard paks if enabled
	i = COM_CheckParm ("-nowildpaks");
	if (!i) 
	{
		COM_ListSystemFiles(searchdir, com_gamedir, "pak", COM_AddEnumeratedPackage);
		COM_ListSystemFiles(searchdir, com_gamedir, "pk3", COM_AddEnumeratedPackage);
	}

	COM_ListFiles(NULL, searchdir, "#*", AddHashedDirectories); // woods #pakdirs

	// then finally link the directory to the search path
	//spike -- moved this last (also explicitly blocked loading progs.dat from system paths when running the demo)
	searchdir->next = com_searchpaths;
	com_searchpaths = searchdir;

	if (!been_here && host_parms->userdir != host_parms->basedir) 
	{
		been_here = true;
		q_strlcpy(com_gamedir, va("%s/%s", host_parms->userdir, dir), sizeof(com_gamedir));
		Sys_mkdir(com_gamedir);
		goto _add_path;
	}
}

void COM_ResetGameDirectories(char *newgamedirs)
{
	char *newpath, *path;
	searchpath_t *search;
	//Kill the extra game if it is loaded
	while (com_searchpaths != com_base_searchpaths)
	{
		if (com_searchpaths->pack)
		{
			Sys_FileClose (com_searchpaths->pack->handle);
			Z_Free (com_searchpaths->pack->files);
			Z_Free (com_searchpaths->pack);
		}
		search = com_searchpaths->next;
		Z_Free (com_searchpaths);
		com_searchpaths = search;
	}
	hipnotic = false;
	rogue = false;
	standard_quake = true;
	//wipe the list of mod gamedirs
	*com_gamenames = 0;
	//reset this too
	q_strlcpy (com_gamedir, va("%s/%s", (host_parms->userdir != host_parms->basedir)?host_parms->userdir:com_basedir, GAMENAME), sizeof(com_gamedir));

	for(newpath = newgamedirs; newpath && *newpath; )
	{
		char *e = strchr(newpath, ';');
		if (e)
			*e++ = 0;

		if (!q_strcasecmp(GAMENAME, newpath))
			path = NULL;
		else for (path = newgamedirs; path < newpath; path += strlen(path)+1)
		{
			if (!q_strcasecmp(path, newpath))
				break;
		}

		if (path == newpath)	//not already loaded
			COM_AddGameDirectory(newpath);
		newpath = e;
	}
}

//==============================================================================
//johnfitz -- dynamic gamedir stuff -- modified by QuakeSpasm team.
//==============================================================================
static void COM_Game_f (void)
{
	if (Cmd_Argc() > 1)
	{
		int i, pri;
		char paths[1024];

		if (!registered.value) //disable shareware quake
		{
			Con_Printf("You must have the registered version to use modified games\n");
			return;
		}

		*paths = 0;
		q_strlcat(paths, GAMENAME, sizeof(paths));
		for (pri = 0; pri <= 1; pri++)
		{
			for (i = 1; i < Cmd_Argc(); i++)
			{
				const char *p = Cmd_Argv(i);
				if (!*p)
					p = GAMENAME;
				if (pri == 0)
				{
					if (*p != '-')
						continue;
					p++;
				}
				else if (*p == '-')
					continue;
				
				if (!*p || !strcmp(p, ".") || strstr(p, "..") || strstr(p, "/") || strstr(p, "\\") || strstr(p, ":"))
				{
					Con_Printf ("gamedir should be a single directory name, not a path\n");
					return;
				}

				if (!q_strcasecmp(p, GAMENAME))
					continue; //don't add id1, its not interesting enough.

				if (*paths)
					q_strlcat(paths, ";", sizeof(paths));
				q_strlcat(paths, p, sizeof(paths));
			}
		}

		if (!q_strcasecmp(paths, COM_GetGameNames(true)))
		{
			Con_Printf("\"game\" is already \"%s\"\n", COM_GetGameNames(true));
			return;
		}

		com_modified = true;

		//Kill the server
		CL_Disconnect ();
		Host_ShutdownServer(true);
		Host_ClearMemory (); // woods -- clear allocated mem on game switches

		//Write config file
		//fixme -- writing configs without reloading when switching between many mods is SERIOUSLY dangerous. ignore if no 'exec default.cfg' commands were used?
		Host_WriteConfiguration ();

		Host_BackupConfiguration (); // woods #cfgbackup

		COM_ResetGameDirectories(paths);

		//clear out and reload appropriate data
		Cache_Flush ();
		Mod_ResetAll();
		Sky_ClearAll();
		if (!isDedicated)
			Draw_ReloadTextures(true);
		ExtraMaps_NewGame ();
		Host_Resetdemos ();
		DemoList_Rebuild ();
		ParticleList_Rebuild (); // woods #particlelist
		SkyList_Rebuild (); // woods #skylist
		FolderList_Rebuild (); // woods #folderlist
		ExecList_Rebuild (); // woods #execlist
		MusicList_Rebuild (); // woods #musiclist
		TextList_Rebuild (); // woods #textlist
		M_CheckMods (); // woods #modsmenu (iw)

		progs_check_done = false; // woods #botdetect

		Con_Printf("\"game\" changed to \"%s\"\n", COM_GetGameNames(true));

		VID_Lock ();
		Cbuf_AddText ("exec quake.rc\n");
		Cbuf_AddText ("vid_unlock\n");
	}
	else //Diplay the current gamedir
		Con_Printf("\"game\" is \"%s\"\n", COM_GetGameNames(true));
}

//
//Spike -- so people can more easily see where their files are actually coming from (to help resolve file conflicts)
//lack of mouse-coy makes this a pain to report still, however
//
static qboolean COM_Dir_Result2(void *ctx, const char *fname, time_t mtime, size_t fsize, searchpath_t *spath)
{
	searchpath_t **primary = ctx;
	if (!*primary)
		*primary = spath;
	return false;
}
static qboolean COM_Dir_Result(void *ctx, const char *fname, time_t mtime, size_t fsize, searchpath_t *spath)
{
	searchpath_t *primary = NULL;
	const char *prefix;
	COM_ListAllFiles(&primary, fname, COM_Dir_Result2, 0, NULL);

	if (primary == spath)
		prefix = "^m";	//file that will be opened.
	else
		prefix = "";	//file that will be ignored.

	if (fsize > 1024*1024*1024)
		Con_SafePrintf("%s    %6.1fgb %-32s %s\n", prefix, (double)fsize/(1024*1024*1024), fname, spath->filename);
	else if (fsize > 1024*1024)
		Con_SafePrintf("%s    %6.1fmb %-32s %s\n", prefix, (double)fsize/(1024*1024), fname, spath->filename);
	else if (fsize > 1024)
		Con_SafePrintf("%s    %6.1fkb %-32s %s\n", prefix, (double)fsize/1024, fname, spath->filename);
	else
		Con_SafePrintf("%s    %4.0fb    %-32s %s\n", prefix, (double)fsize, fname, spath->filename);
	return true;
}
static void COM_Dir_f(void)
{
	int i;
	for (i = 1; i < Cmd_Argc(); i++)
		COM_ListAllFiles(NULL, Cmd_Argv(i), COM_Dir_Result, 0, NULL);
}

static void COM_Dir_Open_f(void) // woods #openfolder opens folder outside of game
{
	int c = Cmd_Argc();

	if (c > 2 || c < 2)
	{
		Con_Printf("\n");
		Con_Printf("^mopen^m <gamedir folder> or <id1 path>  ie: open id1, open id1/maps, open demos\n");
		Con_Printf("\n");
		return;
	}
	else
	{
		char	path[MAX_OSPATH];
		char	folder[MAX_OSPATH];

		if (strstr(Cmd_Argv(1), "id1"))
			q_snprintf(path, sizeof(path), "file://%s/%s", com_basedir, Cmd_Argv(1));
		else
		{ 
			q_strlcpy(folder, Cmd_Argv(1), sizeof(folder));
			q_snprintf(path, sizeof(path), "file://%s/%s", com_gamedir, folder);
		}

		if (SDL_OpenURL(path) == -1)
		{ 
			Con_Printf("\n");
			Con_Printf("no folder found\n");
			Con_Printf("\n");
		}
		else
			SDL_OpenURL(path);
	}
}

static qboolean COM_SameDirs(const char *dir1, const char *dir2)
{
#ifdef _WIN32
	//windows docs say it doesn't provide any inode equivelent for most windows filesystems.
	char p1[4096], p2[4096];
	size_t l;
	GetFullPathName(dir1, countof(p1), p1, NULL);
	GetFullPathName(dir2, countof(p2), p2, NULL);
	//kill any trailing slashes, to make sure they're actually the same...
	for (l = strlen(p1); l > 0 && (p1[l-1] == '/' || p1[l-1] == '\\'); )
		p1[--l] = 0;
	for (l = strlen(p2); l > 0 && (p2[l-1] == '/' || p2[l-1] == '\\'); )
		p2[--l] = 0;
	return !strcmp(p1, p2);
#else
	struct stat bd, gd;
	if (!stat(dir1, &bd) && bd.st_ino &&
		!stat(dir2, &gd) && gd.st_ino &&
		bd.st_dev == gd.st_dev && bd.st_ino == gd.st_ino)
		return true;
#endif
	return false;
}

/*
================================================================================================
 ZIP/PAK File Handling Utilities -- woods #unpak
 
 ZIP_Extract: Extracts all files from a given ZIP archive into a specified directory.
 COM_UnPAK_f: Handles extraction of PAK or PK3 archives based on input commands.
 CompletePAKList: Searches for valid PAK/PK3 files in specified directories for tab completion.
 
 Notes:
 Supports both PAK, PK3, KPF formats with recursive directory creation.
 Utilizes zlib for DEFLATE-compressed ZIP files.
 ================================================================================================
 */

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

#define ZIP_EOCD_SIGNATURE  0x06054b50
#define ZIP_CD_SIGNATURE    0x02014b50
#define ZIP_LFH_SIGNATURE   0x04034b50

#define ARCHIVE_MAX_SIZE       (1024 * 1024 * 1024)  // 1GB max archive size
#define ARCHIVE_MAX_FILE_SIZE  (100 * 1024 * 1024)   // 100MB max per file
#define ARCHIVE_MAX_FILES      65535                 // Maximum number of files
#define ARCHIVE_MAX_FILENAME   256                   // Maximum filename length
#define ARCHIVE_MAX_PATH_DEPTH 16                    // Maximum directory depth

#define ZIP_EOCD_SIZE 22
#define ZIP_CHUNK     16384

#define PAK_MAX_SIZE         ARCHIVE_MAX_SIZE
#define PAK_MAX_FILE_SIZE    ARCHIVE_MAX_FILE_SIZE
#define ZIP_MAX_SIZE         ARCHIVE_MAX_SIZE
#define ZIP_MAX_FILE_SIZE    ARCHIVE_MAX_FILE_SIZE
#define PAK_MAX_FILES        ARCHIVE_MAX_FILES
#define ZIP_MAX_FILES        ARCHIVE_MAX_FILES
#define PAK_MAX_FILENAME     ARCHIVE_MAX_FILENAME
#define ZIP_MAX_FILENAME     ARCHIVE_MAX_FILENAME
#define MAX_PATH_DEPTH       ARCHIVE_MAX_PATH_DEPTH

 /*
======================================
ZIP_ValidatePath

Path Validation and Directory Creation
======================================
*/

static qboolean ValidatePath(const char* filename, const char* outdir, size_t max_filename, const char* type)
{
	const char* p;
	int depth = 0;
	char resolved_path[MAX_OSPATH];
	char clean_path[MAX_OSPATH];

	// Check for NULL or empty filename
	if (!filename || !filename[0]) {
		Con_Printf("WARNING: Empty filename rejected\n");
		return false;
	}

	// Check filename length
	if (strlen(filename) >= max_filename) {
		Con_Printf("WARNING: %s filename too long: %s\n", type, filename);
		return false;
	}

	// Normalize path separators to forward slashes
	size_t clean_len = 0;
	for (p = filename; *p && clean_len < sizeof(clean_path) - 1; p++) {
		if (*p == '\\') {
			clean_path[clean_len++] = '/';
		}
		else {
			clean_path[clean_len++] = *p;
		}
	}
	clean_path[clean_len] = '\0';

	// Check for dangerous characters
	if (strstr(clean_path, "..") ||    // Directory traversal
		strstr(clean_path, ":") ||     // Windows drive letter
		strstr(clean_path, "|") ||     // Command injection
		strstr(clean_path, ";") ||     // Command injection
		strstr(clean_path, ">") ||     // Redirection
		strstr(clean_path, "<")) {     // Redirection
		Con_Printf("WARNING: Invalid characters in filename: %s\n", filename);
		return false;
	}

	// Check for absolute paths
	if (clean_path[0] == '/') {
		Con_Printf("WARNING: Absolute path rejected: %s\n", filename);
		return false;
	}

	// Check directory depth
	for (p = clean_path; *p; p++) {
		if (*p == '/') {
			depth++;
			if (depth > MAX_PATH_DEPTH) {
				Con_Printf("WARNING: Directory depth exceeds maximum: %s\n", filename);
				return false;
			}
		}
	}

	// Check for control characters
	for (p = clean_path; *p; p++) {
		if ((unsigned char)*p < 32) {
			Con_Printf("WARNING: Control character in filename: %s\n", filename);
			return false;
		}
	}

	// Construct and verify full path
	q_snprintf(resolved_path, sizeof(resolved_path), "%s/%s", outdir, clean_path);

	// Ensure the final path stays within outdir
	if (strstr(resolved_path, "..") || !strstr(resolved_path, outdir)) {
		Con_Printf("WARNING: Path escapes output directory: %s\n", filename);
		return false;
	}

	return true;
}

#define ZIP_ValidatePath(filename, outdir) ValidatePath(filename, outdir, ZIP_MAX_FILENAME, "ZIP")
#define PAK_ValidatePath(filename, outdir) ValidatePath(filename, outdir, PAK_MAX_FILENAME, "PAK")

static void CreateDirectoryPath(const char* path)
{
	char temp[MAX_OSPATH];
	char* p;
	size_t len;

	// Make a temporary copy
	len = strlen(path);
	if (len >= sizeof(temp)) {
		Con_Printf("ERROR: Path too long\n");
		return;
	}
	strcpy(temp, path);

	// Create each directory in the path
	for (p = temp + 1; *p; p++) {
		if (*p == '/' || *p == '\\') {
			*p = 0;  // Temporarily terminate
			Sys_mkdir(temp);
			*p = '/';  // Restore slash (always use forward slash)
		}
	}
	// Create the final directory
	Sys_mkdir(temp);
}

/*
===================
ZIP_Extract

Extraction Function
===================
*/

qboolean ZIP_Extract(const char* zipfile, const char* outdir)
{
	Con_DPrintf("Extracting %s to %s\n", zipfile, outdir);

	FILE* zip = fopen(zipfile, "rb");
	if (!zip) {
		Con_Printf("ERROR: Could not open ZIP file\n");
		return false;
	}

	// Get file size
	fseek(zip, 0, SEEK_END);
	long filesize = ftell(zip);
	fseek(zip, 0, SEEK_SET);

	// Check archive size
	if (filesize > ZIP_MAX_SIZE) {
		Con_Printf("ERROR: Archive too large (%.2f MB > %.2f MB max)\n",
			filesize / (1024.0 * 1024.0),
			ZIP_MAX_SIZE / (1024.0 * 1024.0));
		fclose(zip);
		return false;
	}

	char mutable_outdir[MAX_OSPATH];
	if (strlen(outdir) >= sizeof(mutable_outdir)) {
		Con_Printf("ERROR: Output directory path too long\n");
		fclose(zip);
		return false;
	}
	strcpy(mutable_outdir, outdir);

	CreateDirectoryPath(mutable_outdir);

	// Look for ZIP end of central directory
	unsigned char buffer[ZIP_EOCD_SIZE];
	long pos;

	// Search for end of central directory signature
	for (pos = filesize - ZIP_EOCD_SIZE; pos >= 0; pos--) {
		fseek(zip, pos, SEEK_SET);
		if (fread(buffer, 1, 4, zip) != 4) continue;

		unsigned int sig = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
		if (sig == ZIP_EOCD_SIGNATURE) {
			break;
		}
	}

	if (pos < 0) {
		Con_Printf("ERROR: Could not find ZIP central directory\n");
		fclose(zip);
		return false;
	}

	// Read central directory info
	fseek(zip, pos, SEEK_SET);
	if (fread(buffer, 1, ZIP_EOCD_SIZE, zip) != ZIP_EOCD_SIZE) {
		Con_Printf("ERROR: Could not read central directory\n");
		fclose(zip);
		return false;
	}

	// Parse EOCD record
	unsigned short total_entries = buffer[10] | (buffer[11] << 8);
	unsigned int cd_offset = buffer[16] | (buffer[17] << 8) | (buffer[18] << 16) | (buffer[19] << 24);

	// Check number of files
	if (total_entries > ZIP_MAX_FILES) {
		Con_Printf("ERROR: Too many files (%d > %d max)\n",
			total_entries, ZIP_MAX_FILES);
		fclose(zip);
		return false;
	}

	// Go to start of central directory
	if (fseek(zip, cd_offset, SEEK_SET) != 0) {
		Con_Printf("ERROR: Could not seek to central directory\n");
		fclose(zip);
		return false;
	}

	qboolean success = true;
	int files_extracted = 0;
	unsigned long total_bytes = 0;
	unsigned long processed_bytes = 0;
	float last_progress = 0;

	// Calculate total size for progress tracking
	long initial_pos = ftell(zip);
	for (int i = 0; i < total_entries; i++) {
		unsigned int cd_sig;
		if (fread(&cd_sig, 1, 4, zip) != 4) break;

		unsigned char header[42];
		if (fread(header, 1, 42, zip) != 42) break;

		unsigned int size = header[16] | (header[17] << 8) |
			(header[18] << 16) | (header[19] << 24);
		total_bytes += size;

		// Skip name, extra, and comment
		unsigned short name_len = header[24] | (header[25] << 8);
		unsigned short extra_len = header[26] | (header[27] << 8);
		unsigned short comment_len = header[28] | (header[29] << 8);
		fseek(zip, name_len + extra_len + comment_len, SEEK_CUR);
	}

	// Return to start of central directory
	fseek(zip, initial_pos, SEEK_SET);

	Con_Printf("\n"); // Add newline before progress starts

	// Process each file in the central directory
	for (int i = 0; i < total_entries; i++) {
		// Read signature first
		unsigned int cd_sig;
		if (fread(&cd_sig, 1, 4, zip) != 4) {
			Con_Printf("ERROR: Could not read central directory signature\n");
			break;
		}

		if (cd_sig != ZIP_CD_SIGNATURE) {
			Con_Printf("ERROR: Invalid central directory signature\n");
			break;
		}

		// Read rest of fixed-size central directory header
		unsigned char cd_header[42];  // 46 - 4 (already read signature)
		if (fread(cd_header, 1, 42, zip) != 42) {
			Con_Printf("ERROR: Could not read central directory header\n");
			break;
		}

		// Get file info from central directory
		unsigned short compression_method = cd_header[6] | (cd_header[7] << 8);
		unsigned int compressed_size = cd_header[16] | (cd_header[17] << 8) |
			(cd_header[18] << 16) | (cd_header[19] << 24);
		unsigned short name_length = cd_header[24] | (cd_header[25] << 8);
		unsigned short extra_length = cd_header[26] | (cd_header[27] << 8);
		unsigned short comment_length = cd_header[28] | (cd_header[29] << 8);
		unsigned int local_header_offset = cd_header[38] | (cd_header[39] << 8) |
			(cd_header[40] << 16) | (cd_header[41] << 24);

		// Read filename
		char filename[MAX_OSPATH];
		if (name_length >= sizeof(filename)) {
			Con_Printf("ERROR: Filename too long\n");
			break;
		}

		if (fread(filename, 1, name_length, zip) != name_length) {
			Con_Printf("ERROR: Could not read filename from central directory\n");
			break;
		}
		filename[name_length] = 0;

		// Skip extra field and comment in central directory
		if (fseek(zip, extra_length + comment_length, SEEK_CUR) != 0) {
			Con_Printf("ERROR: Could not skip extra/comment fields\n");
			break;
		}

		// Remember current position in central directory
		long current_pos = ftell(zip);

		// Add security checks here
		if (!ZIP_ValidatePath(filename, outdir)) {
			Con_Printf("ERROR: Invalid filename rejected: %s\n", filename);
			fseek(zip, current_pos, SEEK_SET);
			continue;
		}

		// Check file size
		if (compressed_size > ZIP_MAX_FILE_SIZE) {
			Con_Printf("ERROR: File too large: %s (%.2f MB > %.2f MB max)\n",
				filename,
				compressed_size / (1024.0 * 1024.0),
				ZIP_MAX_FILE_SIZE / (1024.0 * 1024.0));
			fseek(zip, current_pos, SEEK_SET);
			continue;
		}

		// Go to local file header
		if (fseek(zip, local_header_offset, SEEK_SET) != 0) {
			Con_Printf("ERROR: Could not seek to local header\n");
			break;
		}

		// Read and verify local file header signature
		unsigned int lfh_sig;
		if (fread(&lfh_sig, 1, 4, zip) != 4 || lfh_sig != ZIP_LFH_SIGNATURE) {
			Con_Printf("ERROR: Invalid local file header signature\n");
			break;
		}

		// Skip rest of local header
		unsigned char lfh[26];
		if (fread(lfh, 1, 26, zip) != 26) {
			Con_Printf("ERROR: Could not read local file header\n");
			break;
		}

		// Skip local header name and extra field
		unsigned short local_name_length = lfh[22] | (lfh[23] << 8);
		unsigned short local_extra_length = lfh[24] | (lfh[25] << 8);

		if (fseek(zip, local_name_length + local_extra_length, SEEK_CUR) != 0) {
			Con_Printf("ERROR: Could not skip local header name/extra\n");
			break;
		}

		// Create output path
		char outpath[MAX_OSPATH];
		q_snprintf(outpath, sizeof(outpath), "%s/%s", outdir, filename);
		for (char* p = outpath; *p; p++) {
			if (*p == '\\') *p = '/';
		}

		// Create parent directories
		char dirpath[MAX_OSPATH];
		q_strlcpy(dirpath, outpath, sizeof(dirpath));
		char* last_slash = strrchr(dirpath, '/');
		if (last_slash) {
			*last_slash = 0;
			CreateDirectoryPath(dirpath);
			*last_slash = '/';
		}

		// If this is a directory entry, create it and continue
		if (filename[strlen(filename) - 1] == '/') {
			COM_CreatePath(outpath);
			fseek(zip, current_pos, SEEK_SET);
			continue;
		}

		// Open output file
		FILE* outfile = fopen(outpath, "wb");
		if (!outfile) {
			Con_Printf("ERROR: Could not create file %s\n", outpath);
			fseek(zip, current_pos, SEEK_SET);
			continue;
		}

		Con_DPrintf("Extracting %s\n", filename);

		// Extract file content based on compression method
		qboolean extract_success = true;
		if (compression_method == 0) {
			// No compression - straight copy
			byte buffer[ZIP_CHUNK];
			size_t remaining = compressed_size;
			while (remaining > 0 && extract_success) {
				size_t to_read = q_min(remaining, ZIP_CHUNK);
				size_t bytes_read = fread(buffer, 1, to_read, zip);
				if (bytes_read != to_read || fwrite(buffer, 1, bytes_read, outfile) != bytes_read) {
					extract_success = false;
				}
				remaining -= bytes_read;
			}
		}
		else if (compression_method == 8) {
			// DEFLATE compression
			z_stream strm = { 0 };
			byte in[ZIP_CHUNK];
			byte out[ZIP_CHUNK];

			if (inflateInit2(&strm, -MAX_WBITS) == Z_OK) {
				size_t remaining = compressed_size;
				do {
					size_t to_read = q_min(remaining, ZIP_CHUNK);
					strm.avail_in = fread(in, 1, to_read, zip);
					if (strm.avail_in == 0) break;

					remaining -= strm.avail_in;
					strm.next_in = in;

					do {
						strm.avail_out = ZIP_CHUNK;
						strm.next_out = out;
						int ret = inflate(&strm, Z_NO_FLUSH);

						if (ret != Z_OK && ret != Z_STREAM_END) {
							extract_success = false;
							break;
						}

						size_t have = ZIP_CHUNK - strm.avail_out;
						if (fwrite(out, 1, have, outfile) != have) {
							extract_success = false;
							break;
						}
					} while (strm.avail_out == 0);

				} while (remaining > 0 && extract_success);

				inflateEnd(&strm);
			}
		}
		else {
			extract_success = false;
		}

		fclose(outfile);
		if (!extract_success) {
			remove(outpath);  // Delete failed file
			Con_DPrintf("ERROR: Failed to extract %s\n", filename);
		}
		else {
			files_extracted++;
			processed_bytes += compressed_size;
			if (total_bytes > 0) {
				float current_progress = (float)processed_bytes / total_bytes * 100;
				// Only show progress if it's increased by at least 5% or at 100%
				if (current_progress - last_progress >= 5.0f || current_progress == 100.0f) {
					Con_Printf("\rprogress: ^m%.1f%%^m", current_progress);
					last_progress = current_progress;
				}
			}
		}

		// Return to central directory position
		if (fseek(zip, current_pos, SEEK_SET) != 0) {
			Con_Printf("ERROR: Could not return to central directory\n");
			break;
		}
	}

	// At the end of the function, replace the success message with:
	if (files_extracted > 0) {
		Con_Printf("\rsuccessfully unpacked ^m%d^m files (%.2f MB)\n\n",
			files_extracted,
			total_bytes / (1024.0 * 1024.0));
	}
	else {
		Con_Printf("\rno files extracted%s\n\n", success ? "" : " due to errors");
	}

	fclose(zip);
	return success;
}

/*
======================
TryOpenPakPath

PAK Extraction Support
======================
*/

static qboolean TryOpenPakPath(const char* dir, const char* base_name, char* out_path, size_t out_path_size,
	const char* exts_to_try[], int exts_count)
{
	const char* provided_ext = COM_FileGetExtension(base_name);

	if (!*provided_ext) {
		// No extension provided, try each known extension
		for (int ei = 0; ei < exts_count; ei++) {
			q_snprintf(out_path, out_path_size, "%s/%s.%s", dir, base_name, exts_to_try[ei]);
			{
				int handle;
				if (Sys_FileOpenRead(out_path, &handle) != -1) {
					Sys_FileClose(handle);
					return true;
				}
			}
		}
	}
	else {
		// Extension provided, try that exact file
		q_snprintf(out_path, out_path_size, "%s/%s", dir, base_name);
		{
			int handle;
			if (Sys_FileOpenRead(out_path, &handle) != -1) {
				Sys_FileClose(handle);
				return true;
			}
		}
	}

	return false;
}

/*
====================
COM_UnPAK_f

Main Unpack Function
====================
*/

void COM_UnPAK_f(void)
{
	char pakpath[MAX_OSPATH];
	char outdir[MAX_OSPATH];
	const char* pakname;
	const char* ext;
	pack_t* pack;
	int i;
	qboolean success = true;
	int files_extracted = 0;
	unsigned long total_bytes = 0;
	unsigned long processed_bytes = 0;

	if (Cmd_Argc() != 2) {
		Con_Printf("\nusage: unpak <filename>\n\n");
		return;
	}

	pakname = Cmd_Argv(1);

	// Helper arrays to try multiple directories and extensions
	const char* dirs_to_try[] = {
		com_gamedir,
		com_basedir,
		va("%s/id1", com_basedir)
	};

	const char* exts_to_try[] = { "pak", "pk3", "kpf" };

	qboolean found_pak = false;
	const char* provided_ext = COM_FileGetExtension(pakname);

	if (!*provided_ext) {
		// If no extension given, try each directory with all known extensions
		for (int di = 0; di < (int)ARRAY_COUNT(dirs_to_try); di++) {
			if (TryOpenPakPath(dirs_to_try[di], pakname, pakpath, sizeof(pakpath),
				exts_to_try, (int)ARRAY_COUNT(exts_to_try))) {
				found_pak = true;
				break;
			}
		}
	}
	else {
		// Extension provided: just try com_gamedir and then com_basedir
		if (TryOpenPakPath(com_gamedir, pakname, pakpath, sizeof(pakpath),
			exts_to_try, (int)ARRAY_COUNT(exts_to_try)) ||
			TryOpenPakPath(com_basedir, pakname, pakpath, sizeof(pakpath),
				exts_to_try, (int)ARRAY_COUNT(exts_to_try))) {
			found_pak = true;
		}
	}

	if (!found_pak) {
		Con_Printf("\ncould not find file ^m%s^m in game, base, or id1 directories\n\n", pakname);
		return;
	}

	// Check file type
	ext = COM_FileGetExtension(pakpath);
	if (q_strcasecmp(ext, "pak") && q_strcasecmp(ext, "pk3") && q_strcasecmp(ext, "kpf")) {
		Con_Printf("ERROR: Unsupported file type for %s\n", pakpath);
		return;
	}

	// Create output directory (remove extension)
	q_strlcpy(outdir, pakpath, sizeof(outdir));
	COM_StripExtension(outdir, outdir, sizeof(outdir));
	Sys_mkdir(outdir);

	Con_Printf("\nunpacking ^m%s^m to %s\n", pakname, outdir);

	// Handle PK3/KPF (ZIP-based) files
	if (!q_strcasecmp(ext, "pk3") || !q_strcasecmp(ext, "kpf")) {
		ZIP_Extract(pakpath, outdir);
		return;
	}

	// Handle PAK files
	pack = COM_LoadPackFile(pakpath);
	if (!pack) {
		Con_Printf("ERROR: Could not open file %s\n", pakpath);
		return;
	}


	// Get file size using standard file operations
	FILE* f = fopen(pakpath, "rb");
	if (!f) {
		Con_Printf("ERROR: Could not open file for size check %s\n", pakpath);
		Sys_FileClose(pack->handle);
		Z_Free(pack->files);
		Z_Free(pack);
		return;
	}

	// Get file size
	fseek(f, 0, SEEK_END);
	long filesize = ftell(f);
	fclose(f);

	// Check PAK file size
	if (filesize > PAK_MAX_SIZE) {
		Con_Printf("ERROR: PAK file too large (%.2f MB > %.2f MB max)\n",
			(double)filesize / (1024.0 * 1024.0),
			(double)PAK_MAX_SIZE / (1024.0 * 1024.0));
		Sys_FileClose(pack->handle);
		Z_Free(pack->files);
		Z_Free(pack);
		return;
	}

	// Check number of files
	if (pack->numfiles > PAK_MAX_FILES) {
		Con_Printf("ERROR: Too many files in PAK (%d > %d max)\n",
			pack->numfiles, PAK_MAX_FILES);
		Sys_FileClose(pack->handle);
		Z_Free(pack->files);
		Z_Free(pack);
		return;
	}

	Con_Printf("\n");

	// Calculate total size (add after pack is loaded, before extraction loop)
	for (i = 0; i < pack->numfiles; i++) {
		if (pack->files[i].name[0] &&
			(strlen(pack->files[i].name) == 0 ||
				pack->files[i].name[strlen(pack->files[i].name) - 1] != '/')) {
			total_bytes += pack->files[i].filelen;
		}
	}

	// Extract each file
	float last_progress = 0;  // Track last shown progress
	for (i = 0; i < pack->numfiles; i++) {
		char outpath[MAX_OSPATH];
		char dirpath[MAX_OSPATH];
		byte* buffer;
		char* p;

		if (!pack->files[i].name[0])
			continue;

		// Skip directory entries
		size_t fnlen = strlen(pack->files[i].name);
		if (fnlen > 0 && pack->files[i].name[fnlen - 1] == '/')
			continue;

		// Validate filename
		if (!PAK_ValidatePath(pack->files[i].name, outdir)) {
			Con_DPrintf("ERROR: Invalid filename rejected: %s\n", pack->files[i].name);
			success = false;
			continue;
		}

		// Check individual file size
		if (pack->files[i].filelen > PAK_MAX_FILE_SIZE) {
			Con_DPrintf("ERROR: File too large: %s (%.2f MB > %.2f MB max)\n",
				pack->files[i].name,
				(double)pack->files[i].filelen / (1024.0 * 1024.0),
				(double)PAK_MAX_FILE_SIZE / (1024.0 * 1024.0));
			success = false;
			continue;
		}

		// Create full output path
		q_snprintf(outpath, sizeof(outpath), "%s/%s", outdir, pack->files[i].name);

		// Create directories
		q_strlcpy(dirpath, outpath, sizeof(dirpath));
		for (p = dirpath + 1; *p; p++) {
			if (*p == '/' || *p == '\\') {
				char save = *p;
				*p = 0;
				Sys_mkdir(dirpath);
				*p = save;
			}
		}

		buffer = (byte*)malloc(pack->files[i].filelen);
		if (!buffer) {
			Con_DPrintf("ERROR: Not enough memory to extract %s (%d bytes)\n",
				pack->files[i].name, (int)pack->files[i].filelen);
			success = false;
			continue;
		}

		Sys_FileSeek(pack->handle, pack->files[i].filepos);
		if (Sys_FileRead(pack->handle, buffer, pack->files[i].filelen) == pack->files[i].filelen) {
			FILE* outfile = fopen(outpath, "wb");
			if (outfile) {
				if (fwrite(buffer, 1, pack->files[i].filelen, outfile) == (size_t)pack->files[i].filelen) {
					files_extracted++;
					processed_bytes += pack->files[i].filelen;
					if (total_bytes > 0) {
						float current_progress = (float)processed_bytes / total_bytes * 100;
						// Only show progress if it's increased by at least 5% or at 100%
						if (current_progress - last_progress >= 5.0f || current_progress == 100.0f) {
							Con_Printf("\rprogress: ^m%.1f%%^m", current_progress);
							last_progress = current_progress;
						}
					}
					Con_DPrintf("  %s\n", pack->files[i].name);
				}
				else {
					Con_DPrintf("ERROR: Failed to write %s\n", pack->files[i].name);
					success = false;
				}
				fclose(outfile);
			}
			else {
				Con_DPrintf("ERROR: Could not create file %s\n", outpath);
				success = false;
			}
		}
		else {
			Con_DPrintf("ERROR: Failed to extract %s\n", pack->files[i].name);
			success = false;
		}

		free(buffer);
	}

	// Clean up
	Sys_FileClose(pack->handle);
	Z_Free(pack->files);
	Z_Free(pack);

	Con_Printf("\r%80s\r", ""); // Clear progress line
	if (success) {
		Con_Printf("successfully unpacked ^m%d^m files (%.2f MB)\n\n",
			files_extracted,
			total_bytes / (1024.0 * 1024.0));
	}
	else {
		Con_Printf("finished unpacking %d files (%.2f MB) from %s with errors\n\n",
			files_extracted,
			total_bytes / (1024.0 * 1024.0),
			pakname);
	}
}

/*
================================
CompletePAKList

Tab Completion for PAK/PK3/KPF Files
================================
*/

qboolean CompletePAKList(const char* partial, void* unused)
{
#ifdef _WIN32
	WIN32_FIND_DATA fdat;
	HANDLE fhnd;
#else
	DIR* dir_p;
	struct dirent* dir_t;
#endif
	char filestring[MAX_OSPATH];
	char pakname[32];
	qboolean found = false;

	// Ensure com_gamedir and com_basedir are valid
	if (com_gamedir[0] == '\0' && com_basedir[0] == '\0')
		return false;

	// Helper macro to process files
#define PROCESS_FILE(file, partial, found_flag) \
    do { \
        const char* ext = COM_FileGetExtension(file); \
        if (q_strcasecmp(ext, "pak") == 0 || \
            q_strcasecmp(ext, "pk3") == 0 || \
            q_strcasecmp(ext, "kpf") == 0) { \
            COM_StripExtension(file, pakname, sizeof(pakname)); \
            Con_AddToTabList(file, partial, NULL, NULL); \
            found_flag = true; \
        } \
    } while (0)

	// Search in com_gamedir
	if (com_gamedir[0] != '\0')
	{
#ifdef _WIN32
		q_snprintf(filestring, sizeof(filestring), "%s\\*.*", com_gamedir);
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				PROCESS_FILE(fdat.cFileName, partial, found);
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
		}
#else
		q_snprintf(filestring, sizeof(filestring), "%s", com_gamedir);
		dir_p = opendir(filestring);
		if (dir_p)
		{
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
					continue;

				PROCESS_FILE(dir_t->d_name, partial, found);
			}
			closedir(dir_p);
		}
#endif
	}

	// Search in com_basedir
	if (com_basedir[0] != '\0')
	{
#ifdef _WIN32
		q_snprintf(filestring, sizeof(filestring), "%s\\*.*", com_basedir);
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				PROCESS_FILE(fdat.cFileName, partial, found);
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
		}
#else
		q_snprintf(filestring, sizeof(filestring), "%s", com_basedir);
		dir_p = opendir(filestring);
		if (dir_p)
		{
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
					continue;

				PROCESS_FILE(dir_t->d_name, partial, found);
			}
			closedir(dir_p);
		}
#endif
	}

	// Search in com_basedir/id1
	if (com_basedir[0] != '\0')
	{
#ifdef _WIN32
		q_snprintf(filestring, sizeof(filestring), "%s\\id1\\*.*", com_basedir);
		fhnd = FindFirstFile(filestring, &fdat);
		if (fhnd != INVALID_HANDLE_VALUE)
		{
			do
			{
				PROCESS_FILE(fdat.cFileName, partial, found);
			} while (FindNextFile(fhnd, &fdat));
			FindClose(fhnd);
		}
#else
		q_snprintf(filestring, sizeof(filestring), "%s/id1", com_basedir);
		dir_p = opendir(filestring);
		if (dir_p)
		{
			while ((dir_t = readdir(dir_p)) != NULL)
			{
				if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
					continue;

				PROCESS_FILE(dir_t->d_name, partial, found);
			}
			closedir(dir_p);
		}
#endif
	}

#undef PROCESS_FILE

	return found;
}

/*
=================
COM_InitFilesystem
=================
*/
void COM_InitFilesystem (void) //johnfitz -- modified based on topaz's tutorial
{
	int i, j;
	const char *p;

	Cvar_RegisterVariable (&allow_download);
	Cvar_RegisterVariable (&registered);
	Cvar_RegisterVariable (&cmdline);
	Cmd_AddCommand ("path", COM_Path_f);
	Cmd_AddCommand ("dir", COM_Dir_f);
	Cmd_AddCommand ("ls", COM_Dir_f);
	Cmd_AddCommand ("which", COM_Dir_f);
	Cmd_AddCommand ("flocate", COM_Dir_f);
	Cmd_AddCommand ("game", COM_Game_f); //johnfitz
	Cmd_AddCommand ("gamedir", COM_Game_f); //Spike -- alternative name for it, consistent with quakeworld and a few other engines
	Cmd_AddCommand ("open", COM_Dir_Open_f); // woods #openfolder
	Cmd_AddCommand ("writeconfig", Host_WriteConfig_f); // woods #writecfg
	Cmd_AddCommand ("unpak", COM_UnPAK_f); // woods #unpak

	i = COM_CheckParm ("-basedir");
	if (i && i < com_argc-1)
		q_strlcpy (com_basedir, com_argv[i + 1], sizeof(com_basedir));
	else
		q_strlcpy (com_basedir, host_parms->basedir, sizeof(com_basedir));

	j = strlen (com_basedir);
	if (j < 1) Sys_Error("Bad argument to -basedir");
	if ((com_basedir[j-1] == '\\') || (com_basedir[j-1] == '/'))
		com_basedir[j-1] = 0;

	//this is horrible.
	if (!fitzmode)
	{
		if (!COM_AddPackage(NULL, va("%s/QuakeEX.kpf", host_parms->userdir), "QuakeEX.kpf") && strcmp(com_basedir, host_parms->userdir))
			COM_AddPackage(NULL, va("%s/QuakeEX.kpf", com_basedir), "QuakeEX.kpf");
	}

	i = COM_CheckParmNext (i, "-basegame");
	if (i)
	{	//-basegame:
		// a) replaces all hardcoded dirs (read: alternative to id1)
		// b) isn't flushed on normal gamedir switches (like id1).
		com_modified = true; //shouldn't be relevant when not using id content... but we don't really know.
		for(;; i = COM_CheckParmNext (i, "-basegame"))
		{
			if (!i || i >= com_argc-1)
				break;

			p = com_argv[i + 1];
			if (!*p || !strcmp(p, ".") || strstr(p, "..") || *p=='/' || *p=='\\' || strstr(p, ":"))
				Sys_Error ("gamedir should be a single directory name, not a path\n");
			if (p != NULL)
				COM_AddGameDirectory (p);
		}
	}
	else
	{
		// start up with GAMENAME by default (id1)
		COM_AddGameDirectory (GAMENAME);
	}

	/* this is the end of our base searchpath:
	 * any set gamedirs, such as those from -game command line
	 * arguments or by the 'game' console command will be freed
	 * up to here upon a new game command. */
	com_base_searchpaths = com_searchpaths;
	COM_ResetGameDirectories("");

	// add mission pack requests (only one should be specified)
	if (COM_CheckParm ("-rogue"))
		COM_AddGameDirectory ("rogue");
	if (COM_CheckParm ("-hipnotic"))
		COM_AddGameDirectory ("hipnotic");
	if (COM_CheckParm ("-quoth"))
		COM_AddGameDirectory ("quoth");


	for(i = 0;;)
	{
		i = COM_CheckParmNext (i, "-game");
		if (!i || i >= com_argc-1)
			break;

		p = com_argv[i + 1];
		if (p != NULL)
		{
			if (!*p || !strcmp(p, ".") || strstr(p, "..") || *p=='/' || *p=='\\' || strstr(p, ":"))
				Sys_Error ("gamedir should be a single directory name, not a path\n");
			com_modified = true;
			COM_AddGameDirectory (p);
		}
	}

	if (com_argc>1 && (p=com_argv[1]))
		if (*p != '+' && *p != '-')
		{
			//weird variation of COM_SkipPath that doesn't get confused over trailing slashes.
			char *n = va("%s", p);
			size_t l = strlen(n);
			p = n;
			while (l > 0 && (n[l-1] == '/' || n[l-1] == '\\'))
				n[--l] = 0;

			while (l --> 0)
			{
				if (n[l] == '/' || n[l] == '\\')
				{
					l++;
					break;
				}
			}
			n+=l;
			if (COM_SameDirs(va("%s", p), va("%s/%s", com_basedir, n)))
			{
				com_modified = true;
				COM_AddGameDirectory (n);
			}
		}

	COM_CheckRegistered ();
}


/* The following FS_*() stdio replacements are necessary if one is
 * to perform non-sequential reads on files reopened on pak files
 * because we need the bookkeeping about file start/end positions.
 * Allocating and filling in the fshandle_t structure is the users'
 * responsibility when the file is initially opened. */

size_t FS_fread(void *ptr, size_t size, size_t nmemb, fshandle_t *fh)
{
	long byte_size;
	long bytes_read;
	size_t nmemb_read;

	if (!fh) {
		errno = EBADF;
		return 0;
	}
	if (!ptr) {
		errno = EFAULT;
		return 0;
	}
	if (!size || !nmemb) {	/* no error, just zero bytes wanted */
		errno = 0;
		return 0;
	}

	byte_size = nmemb * size;
	if (byte_size > fh->length - fh->pos)	/* just read to end */
		byte_size = fh->length - fh->pos;
	bytes_read = fread(ptr, 1, byte_size, fh->file);
	fh->pos += bytes_read;

	/* fread() must return the number of elements read,
	 * not the total number of bytes. */
	nmemb_read = bytes_read / size;
	/* even if the last member is only read partially
	 * it is counted as a whole in the return value. */
	if (bytes_read % size)
		nmemb_read++;

	return nmemb_read;
}

int FS_fseek(fshandle_t *fh, long offset, int whence)
{
/* I don't care about 64 bit off_t or fseeko() here.
 * the quake/hexen2 file system is 32 bits, anyway. */
	int ret;

	if (!fh) {
		errno = EBADF;
		return -1;
	}

	/* the relative file position shouldn't be smaller
	 * than zero or bigger than the filesize. */
	switch (whence)
	{
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += fh->pos;
		break;
	case SEEK_END:
		offset = fh->length + offset;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}

	if (offset > fh->length)	/* just seek to end */
		offset = fh->length;

	ret = fseek(fh->file, fh->start + offset, SEEK_SET);
	if (ret < 0)
		return ret;

	fh->pos = offset;
	return 0;
}

int FS_fclose(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fclose(fh->file);
}

long FS_ftell(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fh->pos;
}

void FS_rewind(fshandle_t *fh)
{
	if (!fh) return;
	clearerr(fh->file);
	fseek(fh->file, fh->start, SEEK_SET);
	fh->pos = 0;
}

int FS_feof(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	if (fh->pos >= fh->length)
		return -1;
	return 0;
}

int FS_ferror(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return ferror(fh->file);
}

int FS_fgetc(fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return EOF;
	}
	if (fh->pos >= fh->length)
		return EOF;
	fh->pos += 1;
	return fgetc(fh->file);
}

char *FS_fgets(char *s, int size, fshandle_t *fh)
{
	char *ret;

	if (FS_feof(fh))
		return NULL;

	if (size > (fh->length - fh->pos) + 1)
		size = (fh->length - fh->pos) + 1;

	ret = fgets(s, size, fh->file);
	fh->pos = ftell(fh->file) - fh->start;

	return ret;
}

long FS_filelength (fshandle_t *fh)
{
	if (!fh) {
		errno = EBADF;
		return -1;
	}
	return fh->length;
}

//for compat with dpp7 protocols, and mods that cba to precache things.
void COM_Effectinfo_Enumerate(int (*cb)(const char *pname))
{
	int i;
	const char *f, *e;
	char *buf;
	static const char *dpnames[] =
	{
		"TE_GUNSHOT",
		"TE_GUNSHOTQUAD",
		"TE_SPIKE",
		"TE_SPIKEQUAD",
		"TE_SUPERSPIKE",
		"TE_SUPERSPIKEQUAD",
		"TE_WIZSPIKE",
		"TE_KNIGHTSPIKE",
		"TE_EXPLOSION",
		"TE_EXPLOSIONQUAD",
		"TE_TAREXPLOSION",
		"TE_TELEPORT",
		"TE_LAVASPLASH",
		"TE_SMALLFLASH",
		"TE_FLAMEJET",
		"EF_FLAME",
		"TE_BLOOD",
		"TE_SPARK",
		"TE_PLASMABURN",
		"TE_TEI_G3",
		"TE_TEI_SMOKE",
		"TE_TEI_BIGEXPLOSION",
		"TE_TEI_PLASMAHIT",
		"EF_STARDUST",
		"TR_ROCKET",
		"TR_GRENADE",
		"TR_BLOOD",
		"TR_WIZSPIKE",
		"TR_SLIGHTBLOOD",
		"TR_KNIGHTSPIKE",
		"TR_VORESPIKE",
		"TR_NEHAHRASMOKE",
		"TR_NEXUIZPLASMA",
		"TR_GLOWTRAIL",
		"SVC_PARTICLE",
		NULL
	};

	buf = (char*)COM_LoadMallocFile("effectinfo.txt", NULL);
	if (!buf)
		return;

	for (i = 0; dpnames[i]; i++)
		cb(dpnames[i]);

	for (f = buf; f; f = e)
	{
		e = COM_Parse (f);
		if (!strcmp(com_token, "effect"))
		{
			e = COM_Parse (e);
			cb(com_token);
		}
		while (e && *e && *e != '\n')
			e++;
	}
	free(buf);
}

/*
============================================================================
								LOCALIZATION
============================================================================
*/
typedef struct
{
	char *key;
	char *value;
} locentry_t;

typedef struct
{
	int			numentries;
	int			maxnumentries;
	int			numindices;
	unsigned	*indices;
	locentry_t	*entries;
	char		*text;
} localization_t;

static localization_t localization;

/*
================
COM_HashString
Computes the FNV-1a hash of string str
================
*/
unsigned COM_HashString (const char *str)
{
	unsigned hash = 0x811c9dc5u;
	while (*str)
	{
		hash ^= *str++;
		hash *= 0x01000193u;
	}
	return hash;
}

/*
================
COM_HashBlock --  woods #modsmenu (iw)
Computes the FNV-1a hash of a memory block
================
*/
unsigned COM_HashBlock(const void* data, size_t size)
{
	const byte* ptr = (const byte*)data;
	unsigned hash = 0x811c9dc5u;
	while (size--)
	{
		hash ^= *ptr++;
		hash *= 0x01000193u;
	}
	return hash;
}

/*
================
LOC_LoadFile
================
*/
void LOC_LoadFile (const char *file)
{
	int i,lineno;
	char *cursor;

	// clear existing data
	if (localization.text)
	{
		free(localization.text);
		localization.text = NULL;
	}
	localization.numentries = 0;
	localization.numindices = 0;

	if (!file || !*file)
		return;

	Con_Printf("\nLanguage initialization\n");

	localization.text = (char*)COM_LoadFile(file, LOADFILE_MALLOC, NULL);
	if (!localization.text)
	{
		Con_Printf("Couldn't load '%s'\nfrom '%s'\n", file, com_basedir);
		return;
	}

	cursor = localization.text;

	// skip BOM
	if ((unsigned char)(cursor[0]) == 0xEF && (unsigned char)(cursor[1]) == 0xBB && (unsigned char)(cursor[2]) == 0xBF)
		cursor += 3;

	lineno = 0;
	while (*cursor)
	{
		char *line, *equals;

		lineno++;

		// skip leading whitespace
		while (q_isblank(*cursor))
			++cursor;

		line = cursor;
		equals = NULL;
		// find line end and first equals sign, if any
		while (*cursor && *cursor != '\n')
		{
			if (*cursor == '=' && !equals)
				equals = cursor;
			cursor++;
		}

		if (line[0] == '/')
		{
			if (line[1] != '/')
				Con_DPrintf("LOC_LoadFile: malformed comment on line %d\n", lineno);
		}
		else if (equals)
		{
			char *key_end = equals;
			qboolean leading_quote;
			qboolean trailing_quote;
			locentry_t *entry;
			char *value_src;
			char *value_dst;
			char *value;

			// trim whitespace before equals sign
			while (key_end != line && q_isspace(key_end[-1]))
				key_end--;
			*key_end = 0;

			value = equals + 1;
			// skip whitespace after equals sign
			while (value != cursor && q_isspace(*value))
				value++;

			leading_quote = (*value == '\"');
			trailing_quote = false;
			value += leading_quote;

			// transform escape sequences in-place
			value_src = value;
			value_dst = value;
			while (value_src != cursor)
			{
				if (*value_src == '\\' && value_src + 1 != cursor)
				{
					char c = value_src[1];
					value_src += 2;
					switch (c)
					{
						case 'n': *value_dst++ = '\n'; break;
						case 't': *value_dst++ = '\t'; break;
						case 'v': *value_dst++ = '\v'; break;
						case 'b': *value_dst++ = '\b'; break;
						case 'f': *value_dst++ = '\f'; break;

						case '"':
						case '\'':
							*value_dst++ = c;
							break;

						default:
							Con_Printf("LOC_LoadFile: unrecognized escape sequence \\%c on line %d\n", c, lineno);
							*value_dst++ = c;
							break;
					}
					continue;
				}

				if (*value_src == '\"')
				{
					trailing_quote = true;
					*value_dst = 0;
					break;
				}

				*value_dst++ = *value_src++;
			}

			// if not a quoted string, trim trailing whitespace
			if (!trailing_quote)
			{
				while (value_dst != value && q_isblank(value_dst[-1]))
				{
					*value_dst = 0;
					value_dst--;
				}
			}

			if (localization.numentries == localization.maxnumentries)
			{
				// grow by 50%
				localization.maxnumentries += localization.maxnumentries >> 1;
				localization.maxnumentries = q_max(localization.maxnumentries, 32);
				localization.entries = (locentry_t*) realloc(localization.entries, sizeof(*localization.entries) * localization.maxnumentries);
			}

			entry = &localization.entries[localization.numentries++];
			entry->key = line;
			entry->value = value;
		}

		if (*cursor)
			*cursor++ = 0; // terminate line and advance to next
	}

	// hash all entries

	localization.numindices = localization.numentries * 2; // 50% load factor
	if (localization.numindices == 0)
	{
		Con_Printf("No localized strings in file '%s'\n", file);
		return;
	}

	localization.indices = (unsigned*) realloc(localization.indices, localization.numindices * sizeof(*localization.indices));
	memset(localization.indices, 0, localization.numindices * sizeof(*localization.indices));

	for (i = 0; i < localization.numentries; i++)
	{
		locentry_t *entry = &localization.entries[i];
		unsigned pos = COM_HashString(entry->key) % localization.numindices, end = pos;

		for (;;)
		{
			if (!localization.indices[pos])
			{
				localization.indices[pos] = i + 1;
				break;
			}

			++pos;
			if (pos == localization.numindices)
				pos = 0;

			if (pos == end)
				Sys_Error("LOC_LoadFile failed");
		}
	}

	Con_Printf("Loaded %d strings from '%s'\n", localization.numentries, file);
}

/*
================
LOC_Init
================
*/
void LOC_Init(void)
{
	LOC_LoadFile("localization/loc_english.txt");
}

/*
================
LOC_Shutdown
================
*/
void LOC_Shutdown(void)
{
	free(localization.indices);
	free(localization.entries);
	free(localization.text);
}

/*
================
LOC_GetRawString

Returns localized string if available, or NULL otherwise
================
*/
const char* LOC_GetRawString (const char *key)
{
	unsigned pos, end;

	if (!localization.numindices || !key || !*key || *key != '$')
		return NULL;
	key++;

	pos = COM_HashString(key) % localization.numindices;
	end = pos;

	do
	{
		unsigned idx = localization.indices[pos];
		locentry_t *entry;
		if (!idx)
			return NULL;

		entry = &localization.entries[idx - 1];
		if (!Q_strcmp(entry->key, key))
			return entry->value;

		++pos;
		if (pos == localization.numindices)
			pos = 0;
	} while (pos != end);

	return NULL;
}

/*
================
LOC_GetString

Returns localized string if available, or input string otherwise
================
*/
const char* LOC_GetString (const char *key)
{
	const char* value = LOC_GetRawString(key);
	return value ? value : key;
}

/*
================
LOC_ParseArg

Returns argument index (>= 0) and advances the string if it starts with a placeholder ({} or {N}),
otherwise returns a negative value and leaves the pointer unchanged
================
*/
static int LOC_ParseArg (const char **pstr)
{
	int arg;
	const char *str = *pstr;

	// opening brace
	if (*str++ != '{')
		return -1;

	// optional index, defaulting to 0
	arg = 0;
	while (q_isdigit(*str))
		arg = arg * 10 + *str++ - '0';

	// closing brace
	if (*str != '}')
		return -1;
	*pstr = ++str;

	return arg;
}

/*
================
LOC_HasPlaceholders
================
*/
qboolean LOC_HasPlaceholders (const char *str)
{
	if (!localization.numindices)
		return false;
	while (*str)
	{
		if (LOC_ParseArg(&str) >= 0)
			return true;
		str++;
	}
	return false;
}

/*
================
LOC_Format

Replaces placeholders (of the form {} or {N}) with the corresponding arguments

Returns number of written chars, excluding the NUL terminator
If len > 0, output is always NUL-terminated
================
*/
size_t LOC_Format (const char *format, const char* (*getarg_fn) (int idx, void* userdata), void* userdata, char* out, size_t len)
{
	size_t written = 0;
	int numargs = 0;

	if (!len)
	{
		Con_DPrintf("LOC_Format: no output space\n");
		return 0;
	}
	--len; // reserve space for the terminator

	while (*format && written < len)
	{
		const char* insert;
		size_t space_left;
		size_t insert_len;
		int argindex = LOC_ParseArg(&format);

		if (argindex < 0)
		{
			out[written++] = *format++;
			continue;
		}

		insert = getarg_fn(argindex, userdata);
		space_left = len - written;
		insert_len = Q_strlen(insert);

		if (insert_len > space_left)
		{
			Con_DPrintf("LOC_Format: overflow at argument #%d\n", numargs);
			insert_len = space_left;
		}

		Q_memcpy(out + written, insert, insert_len);
		written += insert_len;
	}

	if (*format)
		Con_DPrintf("LOC_Format: overflow\n");

	out[written] = 0;

	return written;
}

// woods #demopercent (Baker Fitzquake Mark V)

int COM_Minutes(int seconds)
{
	return seconds / 60;
}

int COM_Seconds(int seconds)
{
	return seconds % 60;
}

/*
================
Write_Log -- woods -- write an arg to a log  // woods #serverlist
================
*/
void Write_Log (const char* log_message, const char* filename)
{
	char line[256];
	int found = 0;
	char fname[MAX_OSPATH];

	// Construct the full path of the file
	strncpy(fname, com_basedir, MAX_OSPATH - 1);
	fname[MAX_OSPATH - 1] = '\0';  // Ensure null termination
	strncat(fname, "/id1/backups/", MAX_OSPATH - strlen(fname) - 1);
	strncat(fname, filename, MAX_OSPATH - strlen(fname) - 1);

	// Open the file in append+read mode
	FILE* log_file = fopen(fname, "a+");
	if (!log_file) 
	{
		Con_DPrintf("Write_Log: Unable to open file %s for reading\n", fname);
		return;
	}

	// Go to the beginning of the file for reading
	rewind(log_file);

	// Iterate through each line of the file
	while (fgets(line, sizeof(line), log_file))
	{
		// Trim newline character
		line[strcspn(line, "\n")] = 0;

		// Check if the log message already exists in the file
		if (strcmp(line, log_message) == 0) 
		{
			found = 1;
			break;
		}
	}

	// If the log message does not already exist in the file, write it
	if (!found) {
		fprintf(log_file, "%s\n", log_message);
	}

	fclose(log_file);
}

void Write_List(filelist_item_t* list, const char* list_name) // woods #bookmarksmenu #historymenu #serverlist
{
	char fname[MAX_OSPATH];
	FILE* log_file;

	q_snprintf(fname, sizeof(fname), "%s/id1/backups/%s", com_basedir, list_name);

	log_file = fopen(fname, "w");
	if (!log_file)
	{
		Con_DPrintf("Write_Log: Unable to open file %s for writing\n", fname);
		return;
	}

	filelist_item_t* current_item = list;
	while (current_item != NULL)
	{
		if (strlen(current_item->data) > 0)
		{
			fprintf(log_file, "%s,%s\n", current_item->name, current_item->data);
		}
		else
		{
			fprintf(log_file, "%s\n", current_item->name);
		}
		current_item = current_item->next;
	}

	fclose(log_file);
}

qboolean isSpecialMap (const char* name) // woods for bmodels
{
	const char* specialMaps[] = 
	{
		"b_batt0", "b_batt1", "b_bh10", "b_bh100", "b_bh25",
		"b_explob", "b_nail0", "b_nail1", "b_rock0", "b_rock1",
		"b_shell0", "b_shell1", "b_lnail0", "b_lnail1", "b_mrock0", 
		"b_mrock1", "b_plas0", "b_plas1"
	};
	int numSpecialMaps = sizeof(specialMaps) / sizeof(specialMaps[0]);

	for (int i = 0; i < numSpecialMaps; ++i)
	{
		if (!strcmp(name, specialMaps[i])) 
			return true;
	}
	return false;
}

/*
==================
UTF8_WriteCodePoint -- woods #serversmenu (ironwail)

Writes a single Unicode code point using UTF-8

Returns the number of bytes written (up to 4),
or 0 on error (overflow or invalid code point)
==================
*/
size_t UTF8_WriteCodePoint(char* dst, size_t maxbytes, uint32_t codepoint)
{
	if (!maxbytes)
		return 0;

	if (codepoint < 0x80)
	{
		dst[0] = (char)codepoint;
		return 1;
	}

	if (codepoint < 0x800)
	{
		if (maxbytes < 2)
			return 0;
		dst[0] = 0xC0 | (codepoint >> 6);
		dst[1] = 0x80 | (codepoint & 63);
		return 2;
	}

	if (codepoint < 0x10000)
	{
		if (maxbytes < 3)
			return 0;
		dst[0] = 0xE0 | (codepoint >> 12);
		dst[1] = 0x80 | ((codepoint >> 6) & 63);
		dst[2] = 0x80 | (codepoint & 63);
		return 3;
	}

	if (codepoint < 0x110000)
	{
		if (maxbytes < 4)
			return 0;
		dst[0] = 0xF0 | (codepoint >> 18);
		dst[1] = 0x80 | ((codepoint >> 12) & 63);
		dst[2] = 0x80 | ((codepoint >> 6) & 63);
		dst[3] = 0x80 | (codepoint & 63);
		return 4;
	}

	return 0;
}

void SetChatInfo (int flags) // woods #chatinfo
{
	char command[16];

	int chat_value = (flags & CIF_AFK) ? CIF_AFK : (flags & CIF_CHAT) ? CIF_CHAT : 0;

	snprintf(command, sizeof(command), "setinfo chat %d\n", chat_value);
	Cbuf_AddText(command);
}

int LevenshteinDistance (const char* s, const char* t) // woods -- #smartquit -- function to calculate the Levenshtein Distance
{
	// Check for null pointers
	if (!s || !t) return -1;

	int len_s = strlen(s);
	int len_t = strlen(t);

	// Handle empty strings
	if (len_s == 0) return len_t;
	if (len_t == 0) return len_s;

	// Allocate a matrix dynamically
	int** matrix = (int**)calloc(len_s + 1, sizeof(int*));
	if (!matrix) return -1;  // Handle allocation failure

	int distance = -1;

	for (int i = 0; i <= len_s; i++) {
		matrix[i] = (int*)calloc(len_t + 1, sizeof(int));
		if (!matrix[i]) {
			// Clean up previously allocated memory if allocation fails
			for (int j = 0; j < i; j++) {
				free(matrix[j]);
			}
			free(matrix);
			return -1;
		}
	}

	// Initialize the matrix
	for (int i = 0; i <= len_s; i++) matrix[i][0] = i;
	for (int j = 0; j <= len_t; j++) matrix[0][j] = j;


	for (int i = 1; i <= len_s; i++) // Compute the Levenshtein distance
	{
		for (int j = 1; j <= len_t; j++) {
			if (tolower(s[i - 1]) == tolower(t[j - 1]))
				matrix[i][j] = matrix[i - 1][j - 1];
			else {
				int insert = matrix[i][j - 1] + 1;
				int delete = matrix[i - 1][j] + 1;
				int substitute = matrix[i - 1][j - 1] + 1;
				matrix[i][j] = insert < delete ? (insert < substitute ? insert : substitute) :
					(delete < substitute ? delete : substitute);
			}
		}
	}

	distance = matrix[len_s][len_t];

	// Free the matrix
	for (int i = 0; i <= len_s; i++) {
		free(matrix[i]);
	}
	free(matrix);

	return distance;
}

/*
==================
Determines if the current filesystem is case-sensitive -- woods #filesystemsens
==================
*/
qboolean FS_IsCaseSensitive(void)
{
	char testfile[MAX_OSPATH];
	char testfile_upper[MAX_OSPATH];
	FILE* f;
	qboolean is_case_sensitive;

	q_snprintf(testfile, sizeof(testfile), ".fs_case_test");
	q_snprintf(testfile_upper, sizeof(testfile_upper), ".FS_CASE_TEST");

	// Try to create lower case test file
	f = fopen(testfile, "wb");
	if (!f)
		return true; // If can't create file, assume case sensitive to be safe
	fclose(f);

	// Try to open the same file with upper case name
	f = fopen(testfile_upper, "rb");
	is_case_sensitive = (f == NULL);
	if (f)
		fclose(f);

	// Clean up
	remove(testfile);
	remove(testfile_upper); // Clean up uppercase version if it exists

	return is_case_sensitive;
}

void* q_memmem(const void* haystack, size_t haystack_len,
	const void* needle, size_t needle_len) // woods #botdetect
{
	const char* h = (const char*)haystack;
	const char* n = (const char*)needle;

	if (needle_len == 0)
		return (void*)haystack;
	if (haystack_len < needle_len)
		return NULL;

	for (size_t i = 0; i <= haystack_len - needle_len; i++) {
		size_t j;
		for (j = 0; j < needle_len; j++) {
			if (h[i + j] != n[j])
				break;
		}
		if (j == needle_len) {
			// Found a substring match, now check word boundaries

			// Check the character before the match
			if (i > 0 && isalnum((unsigned char)h[i - 1])) {
				// The previous character is alphanumeric, so this is not a whole word match
				continue;
			}

			// Check the character after the match
			if (i + needle_len < haystack_len && isalnum((unsigned char)h[i + needle_len])) {
				// The next character is alphanumeric, so this is not a whole word match
				continue;
			}

			// Passed the boundary checks, this is an exact whole word match
			return (void*)(h + i);
		}
	}

	return NULL;
}