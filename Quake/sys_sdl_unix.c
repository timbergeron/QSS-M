/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2005 John Fitzgibbons and others
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

#include "arch_def.h"
#include "quakedef.h"

#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#if defined(PLATFORM_OSX) || defined(PLATFORM_HAIKU)
#include <libgen.h>	/* dirname() and basename() */
#endif
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#ifdef DO_USERDIRS
#include <pwd.h>
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

#include <termios.h> // woods #arrowkeys
#include <unistd.h> // woods #arrowkeys
#include <sys/ioctl.h> // scrollback terminal size

qboolean		isDedicated;
cvar_t		sys_throttle = {"sys_throttle", "0.02", CVAR_ARCHIVE};
cvar_t		sys_dedmouse_capture = {"sys_dedmouse_capture", "0", CVAR_ARCHIVE};

static size_t	sys_handles_max;	/* spike -- removed limit, was 32 (johnfitz -- was 10) */
static FILE		**sys_handles;

static qboolean		stdinIsATTY;	/* from ioquake3 source */

static int findhandle (void)
{
	size_t i, n;

	for (i = 1; i < sys_handles_max; i++)
	{
		if (!sys_handles[i])
			return i;
	}
	n = sys_handles_max+10;
	sys_handles = realloc(sys_handles, sizeof(*sys_handles)*n);
	if (!sys_handles)
		Sys_Error ("out of handles");
	while (sys_handles_max < n)
		sys_handles[sys_handles_max++] = NULL;
	return i;
}

qofs_t Sys_filelength (FILE *f)
{
	long		pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return end;
}

int Sys_remove (const char *path)
{
	return remove (path);
}

qboolean Sys_GetExecutablePath(char *out, size_t outsize)
{
	if (!out || outsize == 0)
		return false;

#if defined(__linux__)
	{
		ssize_t len = readlink("/proc/self/exe", out, outsize - 1);
		if (len > 0 && (size_t)len < outsize)
		{
			out[len] = '\0';
			return true;
		}
	}
#elif defined(__APPLE__)
	{
		char raw[MAX_OSPATH];
		uint32_t size = (uint32_t)sizeof(raw);

		/* _NSGetExecutablePath may return a relative path containing ".." or
		 * unresolved symlinks; canonicalize it so callers get a stable
		 * absolute path. realpath(path, NULL) avoids a PATH_MAX buffer. */
		if (_NSGetExecutablePath(raw, &size) == 0)
		{
			char *resolved = realpath(raw, NULL);

			if (resolved)
			{
				qboolean ok = q_strlcpy(out, resolved, outsize) < outsize;

				free(resolved);
				if (ok)
					return true;
			}
			if (q_strlcpy(out, raw, outsize) < outsize)
				return true;
		}
	}
#endif

	out[0] = '\0';
	return false;
}

unsigned long Sys_GetProcessId(void)
{
	return (unsigned long)getpid();
}

qboolean Sys_MakeExecutable(const char *path)
{
	struct stat st;

	if (!path || stat(path, &st) != 0)
		return false;

	return chmod(path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0;
}

static void Sys_PathDirName(const char *path, char *out, size_t outsize)
{
	const char *slash;
	size_t len;

	if (out && outsize)
		out[0] = '\0';
	if (!path || !*path || !out || !outsize)
		return;

	slash = strrchr(path, '/');
	if (!slash)
	{
		q_strlcpy(out, ".", outsize);
		return;
	}

	len = (size_t)(slash - path);
	if (len == 0)
		len = 1;
	if (len >= outsize)
		len = outsize - 1;
	memcpy(out, path, len);
	out[len] = '\0';
}

qboolean Sys_LaunchUpdateHelper(const char *helper_path,
	const char *helper_arg, const char *manifest_path, char *error,
	size_t error_size)
{
	pid_t pid;
	int status_pipe[2] = {-1, -1};
	int parent_pipe[2] = {-1, -1};
	char helper_dir[MAX_OSPATH];
	char helper_exec[MAX_OSPATH];
	const char *helper_name;
	int child_errno = 0;
	ssize_t nread;

	if (error && error_size)
		error[0] = '\0';

	Sys_PathDirName(helper_path, helper_dir, sizeof(helper_dir));
	helper_name = strrchr(helper_path, '/');
	helper_name = helper_name ? helper_name + 1 : helper_path;
	if ((size_t)q_snprintf(helper_exec, sizeof(helper_exec), "./%s",
		helper_name) >= sizeof(helper_exec))
	{
		q_strlcpy(error, "helper executable path too long", error_size);
		return false;
	}

	if (pipe(status_pipe) != 0)
	{
		q_snprintf(error, error_size, "pipe failed: %s", strerror(errno));
		return false;
	}
	if (fcntl(status_pipe[1], F_SETFD, FD_CLOEXEC) == -1)
	{
		q_snprintf(error, error_size, "fcntl failed: %s", strerror(errno));
		close(status_pipe[0]);
		close(status_pipe[1]);
		return false;
	}
	if (pipe(parent_pipe) != 0)
	{
		q_snprintf(error, error_size, "pipe failed: %s", strerror(errno));
		close(status_pipe[0]);
		close(status_pipe[1]);
		return false;
	}
	if (fcntl(parent_pipe[1], F_SETFD, FD_CLOEXEC) == -1)
	{
		q_snprintf(error, error_size, "fcntl failed: %s", strerror(errno));
		close(status_pipe[0]);
		close(status_pipe[1]);
		close(parent_pipe[0]);
		close(parent_pipe[1]);
		return false;
	}

	pid = fork();
	if (pid < 0)
	{
		q_snprintf(error, error_size, "fork failed: %s", strerror(errno));
		close(status_pipe[0]);
		close(status_pipe[1]);
		close(parent_pipe[0]);
		close(parent_pipe[1]);
		return false;
	}
	if (pid == 0)
	{
		char parent_token[32];
		ssize_t written;

		close(status_pipe[0]);
		close(parent_pipe[1]);
		setsid();
		if (helper_dir[0] && chdir(helper_dir) != 0)
		{
			child_errno = errno;
			written = write(status_pipe[1], &child_errno,
				sizeof(child_errno));
			(void)written;
			_exit(126);
		}
		q_snprintf(parent_token, sizeof(parent_token), "%d",
			parent_pipe[0]);
		execl(helper_exec, helper_path, helper_arg, manifest_path,
			parent_token, (char *)NULL);
		child_errno = errno;
		written = write(status_pipe[1], &child_errno,
			sizeof(child_errno));
		(void)written;
		_exit(127);
	}

	close(status_pipe[1]);
	close(parent_pipe[0]);
	do
	{
		nread = read(status_pipe[0], &child_errno, sizeof(child_errno));
	} while (nread < 0 && errno == EINTR);
	close(status_pipe[0]);

	if (nread > 0)
	{
		close(parent_pipe[1]);
		waitpid(pid, NULL, 0);
		q_snprintf(error, error_size, "exec update helper failed: %s",
			strerror(child_errno));
		return false;
	}
	if (nread < 0)
	{
		close(parent_pipe[1]);
		q_snprintf(error, error_size,
			"unable to verify update helper start: %s", strerror(errno));
		return false;
	}

	return true;
}

qboolean Sys_LaunchProgram(const char *exe_path, const char *working_dir,
	char *error, size_t error_size)
{
	pid_t pid;
	int status_pipe[2] = {-1, -1};
	int child_errno = 0;
	ssize_t nread;

	if (error && error_size)
		error[0] = '\0';

	if (pipe(status_pipe) != 0)
	{
		q_snprintf(error, error_size, "pipe failed: %s", strerror(errno));
		return false;
	}
	if (fcntl(status_pipe[1], F_SETFD, FD_CLOEXEC) == -1)
	{
		q_snprintf(error, error_size, "fcntl failed: %s", strerror(errno));
		close(status_pipe[0]);
		close(status_pipe[1]);
		return false;
	}

	pid = fork();
	if (pid < 0)
	{
		q_snprintf(error, error_size, "fork failed: %s", strerror(errno));
		close(status_pipe[0]);
		close(status_pipe[1]);
		return false;
	}
	if (pid == 0)
	{
		ssize_t written;

		close(status_pipe[0]);
		if (working_dir && *working_dir && chdir(working_dir) != 0)
		{
			child_errno = errno;
			written = write(status_pipe[1], &child_errno,
				sizeof(child_errno));
			(void)written;
			_exit(126);
		}
		execl(exe_path, exe_path, (char *)NULL);
		child_errno = errno;
		written = write(status_pipe[1], &child_errno,
			sizeof(child_errno));
		(void)written;
		_exit(127);
	}

	close(status_pipe[1]);
	do
	{
		nread = read(status_pipe[0], &child_errno, sizeof(child_errno));
	} while (nread < 0 && errno == EINTR);
	close(status_pipe[0]);

	if (nread > 0)
	{
		waitpid(pid, NULL, 0);
		q_snprintf(error, error_size, "exec launch failed: %s",
			strerror(child_errno));
		return false;
	}
	if (nread < 0)
	{
		q_snprintf(error, error_size,
			"unable to verify launch: %s", strerror(errno));
		return false;
	}

	return true;
}

qboolean Sys_RunUpdateSelfTest(const char *exe_path, const char *working_dir,
	const char *selftest_arg, unsigned int timeout_ms, char *error,
	size_t error_size)
{
	pid_t pid;
	int status_pipe[2] = {-1, -1};
	int child_errno = 0;
	ssize_t nread;
	unsigned int waited = 0;
	int status = 0;

	if (error && error_size)
		error[0] = '\0';
	if (!exe_path || !*exe_path || !selftest_arg || !*selftest_arg)
	{
		q_strlcpy(error, "invalid self-test command", error_size);
		return false;
	}

	if (pipe(status_pipe) != 0)
	{
		q_snprintf(error, error_size, "pipe failed: %s", strerror(errno));
		return false;
	}
	if (fcntl(status_pipe[1], F_SETFD, FD_CLOEXEC) == -1)
	{
		q_snprintf(error, error_size, "fcntl failed: %s", strerror(errno));
		close(status_pipe[0]);
		close(status_pipe[1]);
		return false;
	}

	pid = fork();
	if (pid < 0)
	{
		q_snprintf(error, error_size, "fork failed: %s", strerror(errno));
		close(status_pipe[0]);
		close(status_pipe[1]);
		return false;
	}
	if (pid == 0)
	{
		ssize_t written;

		close(status_pipe[0]);
		if (working_dir && *working_dir && chdir(working_dir) != 0)
		{
			child_errno = errno;
			written = write(status_pipe[1], &child_errno,
				sizeof(child_errno));
			(void)written;
			_exit(126);
		}
		execl(exe_path, exe_path, selftest_arg, (char *)NULL);
		child_errno = errno;
		written = write(status_pipe[1], &child_errno,
			sizeof(child_errno));
		(void)written;
		_exit(127);
	}

	close(status_pipe[1]);
	do
	{
		nread = read(status_pipe[0], &child_errno, sizeof(child_errno));
	} while (nread < 0 && errno == EINTR);
	close(status_pipe[0]);

	if (nread > 0)
	{
		waitpid(pid, NULL, 0);
		q_snprintf(error, error_size, "exec self-test failed: %s",
			strerror(child_errno));
		return false;
	}
	if (nread < 0)
	{
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		q_snprintf(error, error_size,
			"unable to verify self-test start: %s", strerror(errno));
		return false;
	}

	for (;;)
	{
		pid_t result;

		do
		{
			result = waitpid(pid, &status, WNOHANG);
		} while (result < 0 && errno == EINTR);

		if (result == pid)
			break;
		if (result < 0)
		{
			q_snprintf(error, error_size, "self-test wait failed: %s",
				strerror(errno));
			return false;
		}
		if (waited >= timeout_ms)
		{
			kill(pid, SIGKILL);
			waitpid(pid, NULL, 0);
			q_strlcpy(error, "self-test timed out", error_size);
			return false;
		}
		{
			unsigned int step = q_min(100, timeout_ms - waited);

			usleep(step * 1000);
			waited += step;
		}
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		q_snprintf(error, error_size, "self-test exited with status %d",
			WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return false;
	}

	return true;
}

static qboolean Sys_ProcessExitedOrZombie(pid_t pid)
{
	if (kill(pid, 0) != 0)
		return errno == ESRCH;

#if defined(__linux__)
	{
		char path[64];
		char statbuf[256];
		char *end_name;
		FILE *f;

		q_snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
		f = fopen(path, "r");
		if (!f)
			return false;
		if (!fgets(statbuf, sizeof(statbuf), f))
		{
			fclose(f);
			return false;
		}
		fclose(f);

		end_name = strrchr(statbuf, ')');
		if (end_name && end_name[1] == ' ' &&
			(end_name[2] == 'Z' || end_name[2] == 'X'))
			return true;
	}
#endif

	return false;
}

static qboolean Sys_UpdateWaitForParentFd(int fd, unsigned int timeout_ms)
{
	unsigned int waited = 0;

	if (fd < 0)
		return false;

	while (waited < timeout_ms)
	{
		fd_set readfds;
		struct timeval tv;
		unsigned int step = q_min(100, timeout_ms - waited);
		int result;

		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = step * 1000;

		result = select(fd + 1, &readfds, NULL, NULL, &tv);
		if (result > 0 && FD_ISSET(fd, &readfds))
		{
			char buffer[64];
			ssize_t nread;

			do
			{
				nread = read(fd, buffer, sizeof(buffer));
			} while (nread < 0 && errno == EINTR);

			if (nread == 0)
			{
				close(fd);
				return true;
			}
			if (nread < 0)
			{
				close(fd);
				return false;
			}
		}
		else if (result < 0 && errno != EINTR)
		{
			close(fd);
			return false;
		}
		if (result >= 0)
			waited += step;
	}

	close(fd);
	return false;
}

qboolean Sys_UpdateWaitForParentExit(uintptr_t wait_token,
	unsigned long fallback_pid, unsigned int timeout_ms)
{
	unsigned int waited;

	if (wait_token > 0 && wait_token <= (uintptr_t)INT_MAX)
		return Sys_UpdateWaitForParentFd((int)wait_token, timeout_ms);

	if (!fallback_pid)
		return true;

	for (waited = 0; waited < timeout_ms; waited += 100)
	{
		if (Sys_ProcessExitedOrZombie((pid_t)fallback_pid))
			return true;
		usleep(100000);
	}

	return Sys_ProcessExitedOrZombie((pid_t)fallback_pid);
}

qofs_t Sys_FileOpenRead (const char *path, int *hndl)
{
	FILE	*f;
	int	i;
	qofs_t retval;

	i = findhandle ();
	f = fopen(path, "rb");

	if (!f)
	{
		*hndl = -1;
		retval = -1;
	}
	else
	{
		sys_handles[i] = f;
		*hndl = i;
		retval = Sys_filelength(f);
	}

	return retval;
}

int Sys_FileOpenWrite (const char *path)
{
	FILE	*f;
	int		i;

	i = findhandle ();
	f = fopen(path, "wb");

	if (!f)
		Sys_Error ("Error opening %s: %s", path, strerror(errno));

	sys_handles[i] = f;
	return i;
}

int Sys_FileOpenStdio (FILE *file)
{
	int		i;
	i = findhandle ();
	sys_handles[i] = file;
	return i;
}

void Sys_FileClose (int handle)
{
	fclose (sys_handles[handle]);
	sys_handles[handle] = NULL;
}

void Sys_FileSeek (int handle, qofs_t position)
{
	fseek (sys_handles[handle], position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	return fread (dest, 1, count, sys_handles[handle]);
}

int Sys_FileWrite (int handle, const void *data, int count)
{
	return fwrite (data, 1, count, sys_handles[handle]);
}

int Sys_FileType (const char *path)
{
	/*
	if (access(path, R_OK) == -1)
		return 0;
	*/
	struct stat	st;

	if (stat(path, &st) != 0)
		return FS_ENT_NONE;
	if (S_ISDIR(st.st_mode))
		return FS_ENT_DIRECTORY;
	if (S_ISREG(st.st_mode))
		return FS_ENT_FILE;

	return FS_ENT_NONE;
}

qboolean Sys_GetFileTime (const char *path, time_t *out)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return false;

	*out = st.st_mtime;
	return true;
}


#if defined(__linux__) || defined(__sun) || defined(sun) || defined(_AIX)
static int Sys_NumCPUs (void)
{
	int numcpus = sysconf(_SC_NPROCESSORS_ONLN);
	return (numcpus < 1) ? 1 : numcpus;
}

#elif defined(PLATFORM_OSX)
#include <sys/sysctl.h>
#if !defined(HW_AVAILCPU)	/* using an ancient SDK? */
#define HW_AVAILCPU		25	/* needs >= 10.2 */
#endif
static int Sys_NumCPUs (void)
{
	int numcpus;
	int mib[2];
	size_t len;

#if defined(_SC_NPROCESSORS_ONLN)	/* needs >= 10.5 */
	numcpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (numcpus != -1)
		return (numcpus < 1) ? 1 : numcpus;
#endif
	len = sizeof(numcpus);
	mib[0] = CTL_HW;
	mib[1] = HW_AVAILCPU;
	sysctl(mib, 2, &numcpus, &len, NULL, 0);
	if (sysctl(mib, 2, &numcpus, &len, NULL, 0) == -1)
	{
		mib[1] = HW_NCPU;
		if (sysctl(mib, 2, &numcpus, &len, NULL, 0) == -1)
			return 1;
	}
	return (numcpus < 1) ? 1 : numcpus;
}

#elif defined(__sgi) || defined(sgi) || defined(__sgi__) /* IRIX */
static int Sys_NumCPUs (void)
{
	int numcpus = sysconf(_SC_NPROC_ONLN);
	if (numcpus < 1)
		numcpus = 1;
	return numcpus;
}

#elif defined(PLATFORM_BSD)
#include <sys/sysctl.h>
static int Sys_NumCPUs (void)
{
	int numcpus;
	int mib[2];
	size_t len;

#if defined(_SC_NPROCESSORS_ONLN)
	numcpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (numcpus != -1)
		return (numcpus < 1) ? 1 : numcpus;
#endif
	len = sizeof(numcpus);
	mib[0] = CTL_HW;
	mib[1] = HW_NCPU;
	if (sysctl(mib, 2, &numcpus, &len, NULL, 0) == -1)
		return 1;
	return (numcpus < 1) ? 1 : numcpus;
}

#elif defined(__hpux) || defined(__hpux__) || defined(_hpux)
#include <sys/mpctl.h>
static int Sys_NumCPUs (void)
{
	int numcpus = mpctl(MPC_GETNUMSPUS, NULL, NULL);
	return numcpus;
}

#else /* unknown OS */
static int Sys_NumCPUs (void)
{
	return -2;
}
#endif

static char	cwd[MAX_OSPATH];
#ifdef DO_USERDIRS
static char	userdir[MAX_OSPATH];
#ifdef PLATFORM_OSX
#define SYS_USERDIR	"Library/Application Support/QuakeSpasm"
#elif defined(PLATFORM_HAIKU)
#define SYS_USERDIR	"QuakeSpasm"
#else
#define SYS_USERDIR	".quakespasm"
#endif

#ifdef PLATFORM_HAIKU
#include <FindDirectory.h>
#include <fs_info.h>

static void Sys_GetUserdir (char *dst, size_t dstsize)
{
	dev_t volume = dev_for_path("/boot");
	char buffer[B_PATH_NAME_LENGTH];
	status_t result;

	result = find_directory(B_USER_NONPACKAGED_DATA_DIRECTORY, volume, false, buffer, sizeof(buffer));
	if (result != B_OK)
		Sys_Error ("Couldn't determine userspace directory");

	q_snprintf (dst, dstsize, "%s/%s", buffer, SYS_USERDIR);
}
#else
static void Sys_GetUserdir (char *dst, size_t dstsize)
{
	size_t		n;
	const char	*home_dir = NULL;
	struct passwd	*pwent;

	pwent = getpwuid( getuid() );
	if (pwent == NULL)
		perror("getpwuid");
	else
		home_dir = pwent->pw_dir;
	if (home_dir == NULL)
		home_dir = getenv("HOME");
	if (home_dir == NULL)
		Sys_Error ("Couldn't determine userspace directory");

/* what would be a maximum path for a file in the user's directory...
 * $HOME/SYS_USERDIR/game_dir/dirname1/dirname2/dirname3/filename.ext
 * still fits in the MAX_OSPATH == 256 definition, but just in case :
 */
	n = strlen(home_dir) + strlen(SYS_USERDIR) + 50;
	if (n >= dstsize)
		Sys_Error ("Insufficient array size for userspace directory");

	q_snprintf (dst, dstsize, "%s/%s", home_dir, SYS_USERDIR);
}
#endif	/* PLATFORM_HAIKU */
#endif	/* DO_USERDIRS */

#ifdef PLATFORM_OSX
static char *OSX_StripAppBundle (char *dir)
{ /* based on the ioquake3 project at icculus.org. */
	static char	osx_path[MAX_OSPATH];

	q_strlcpy (osx_path, dir, sizeof(osx_path));
	if (strcmp(basename(osx_path), "MacOS"))
		return dir;
	q_strlcpy (osx_path, dirname(osx_path), sizeof(osx_path));
	if (strcmp(basename(osx_path), "Contents"))
		return dir;
	q_strlcpy (osx_path, dirname(osx_path), sizeof(osx_path));
	if (!strstr(basename(osx_path), ".app"))
		return dir;
	q_strlcpy (osx_path, dirname(osx_path), sizeof(osx_path));
	return osx_path;
}

static void Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	char	*tmp;

	if (realpath(argv0, dst) == NULL)
	{
		perror("realpath");
		if (getcwd(dst, dstsize - 1) == NULL)
	_fail:		Sys_Error ("Couldn't determine current directory");
	}
	else
	{
		/* strip off the binary name */
		if (! (tmp = strdup (dst))) goto _fail;
		q_strlcpy (dst, dirname(tmp), dstsize);
		free (tmp);
	}

	tmp = OSX_StripAppBundle(dst);
	if (tmp != dst)
		q_strlcpy (dst, tmp, dstsize);
}
#else
static void Sys_GetBasedir (char *argv0, char *dst, size_t dstsize)
{
	char	*tmp;

	#ifdef PLATFORM_HAIKU
	if (realpath(argv0, dst) == NULL)
	{
		perror("realpath");
		if (getcwd(dst, dstsize - 1) == NULL)
	_fail:		Sys_Error ("Couldn't determine current directory");
	}
	else
	{
		/* strip off the binary name */
		if (! (tmp = strdup (dst))) goto _fail;
		q_strlcpy (dst, dirname(tmp), dstsize);
		free (tmp);
	}
	#else
	if (getcwd(dst, dstsize - 1) == NULL)
		Sys_Error ("Couldn't determine current directory");

	tmp = dst;
	while (*tmp != 0)
		tmp++;
	while (*tmp == 0 && tmp != dst)
	{
		--tmp;
		if (tmp != dst && *tmp == '/')
			*tmp = 0;
	}
	#endif
}
#endif

void Sys_Init (void)
{
	const char* term = getenv("TERM");
	stdinIsATTY = isatty(STDIN_FILENO) &&
			!(term && (!strcmp(term, "raw") || !strcmp(term, "dumb")));
	if (!stdinIsATTY)
		Sys_Printf("Terminal input not available.\n");

	memset (cwd, 0, sizeof(cwd));
	Sys_GetBasedir(host_parms->argv[0], cwd, sizeof(cwd));
	host_parms->basedir = cwd;
#ifndef DO_USERDIRS
	host_parms->userdir = host_parms->basedir; /* code elsewhere relies on this ! */
#else
	if (COM_CheckParm("-nohome"))
		host_parms->userdir = host_parms->basedir;
	else
	{
		memset (userdir, 0, sizeof(userdir));
		Sys_GetUserdir(userdir, sizeof(userdir));
		Sys_mkdir (userdir);
		host_parms->userdir = userdir;
	}
#endif
	host_parms->numcpus = Sys_NumCPUs ();
	Sys_Printf("Detected %d CPUs.\n", host_parms->numcpus);
}

void Sys_mkdir (const char *path)
{
	int rc = mkdir (path, 0777);
	if (rc != 0 && errno == EEXIST)
	{
		struct stat st;
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
			rc = 0;
	}
	if (rc != 0)
	{
		rc = errno;
		Sys_Error("Unable to create directory %s: %s", path, strerror(rc));
	}
}

static qboolean Sys_FileURLCharIsSafe (unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ||
	       (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') ||
	       c == '/' || c == '-' || c == '.' || c == '_' || c == '~';
}

static qboolean Sys_BuildFileURL (const char *path, char *url, size_t urlsize)
{
	static const char	hex[] = "0123456789ABCDEF";
	const unsigned char	*src;
	char			*dst, *end;

	if (!path || q_strlcpy(url, "file://", urlsize) >= urlsize)
		return false;

	dst = url + strlen(url);
	end = url + urlsize;
	for (src = (const unsigned char *)path; *src; src++)
	{
		unsigned char c = *src;
		if (Sys_FileURLCharIsSafe(c))
		{
			if (dst + 1 >= end)
				return false;
			*dst++ = (char)c;
		}
		else
		{
			if (dst + 3 >= end)
				return false;
			*dst++ = '%';
			*dst++ = hex[c >> 4];
			*dst++ = hex[c & 15];
		}
	}
	*dst = '\0';
	return true;
}

qboolean Sys_Explore (const char *path)
{
	char	url[MAX_OSPATH * 3 + 8];
	char	dir[MAX_OSPATH];
	char	*s;
	int	type;

	type = Sys_FileType (path);
	if (type == FS_ENT_NONE)
		return false;

	q_strlcpy (dir, path, sizeof(dir));
	if (!(type & FS_ENT_DIRECTORY))
	{
		s = Q_strrchr (dir, '/');
		if (!s)
			return false;
		if (s == dir)
			s[1] = '\0';
		else
			*s = '\0';
	}

	if (!Sys_BuildFileURL(dir, url, sizeof(url)))
		return false;
	return SDL_OpenURL (url) == 0;
}

static const char errortxt1[] = "\nERROR-OUT BEGIN\n\n";
static const char errortxt2[] = "\nQUAKE ERROR: ";
static const char sys_console_reset_seq[] = "\033[0m\033[?1l\033[?7h\033[?25h\033[?1000l\033[?1002l\033[?1003l\033[?1004l\033[?1005l\033[?1006l\033[?1015l\033[?1049l\033[?2004l";
static const char sys_console_quit_reset_seq[] =
	"\033[!p"      /* DECSTR soft terminal reset */
	"\033>"        /* normal keypad mode */
	"\033[?1l"     /* normal cursor keys */
	"\033[?7h"     /* autowrap on */
	"\033[?25h"    /* cursor visible */
	"\033[0m"
	"\033[?1000l\033[?1002l\033[?1003l\033[?1004l\033[?1005l\033[?1006l\033[?1015l"
	"\033[?1049l"
	"\033[?2004l"
	"\r\n";

static struct termios orig_termios_saved;
static qboolean term_initialized = false;
static volatile sig_atomic_t dedicated_shutdown_signal = 0;
static volatile sig_atomic_t dedicated_shutdown_hard_exit = 0;
static int console_signal_ttyfd = -1;
static void Sys_ConsoleCleanup (void); // forward decl
static int Sys_ConsoleOpenTTY (int flags);

static void Sys_ConsoleEnsureSignalTTY (void)
{
	if (console_signal_ttyfd != -1)
		return;

	console_signal_ttyfd = Sys_ConsoleOpenTTY (O_WRONLY);
	if (console_signal_ttyfd == -1 && isatty (STDOUT_FILENO))
		console_signal_ttyfd = dup (STDOUT_FILENO);
	if (console_signal_ttyfd == -1 && isatty (STDIN_FILENO))
		console_signal_ttyfd = dup (STDIN_FILENO);
}

static void Sys_DedicatedSignalHandler (int sig)
{
	const char *text = sys_console_reset_seq;
	size_t len = sizeof (sys_console_reset_seq) - 1;
	int saved_errno = errno;

	if (dedicated_shutdown_hard_exit)
		_exit (128 + sig);

	dedicated_shutdown_hard_exit = 1;
	dedicated_shutdown_signal = sig;

	if (console_signal_ttyfd != -1)
	{
		while (len > 0)
		{
			ssize_t nwritten = write (console_signal_ttyfd, text, len);

			if (nwritten > 0)
			{
				text += nwritten;
				len -= (size_t)nwritten;
				continue;
			}

			if (nwritten < 0 && errno == EINTR)
				continue;

			break;
		}
	}

	errno = saved_errno;
}

void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	host_parms->errstate++;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	fputs (errortxt1, stderr);
	Con_Redirect(NULL);
	PR_SwitchQCVM(NULL);
	Host_Shutdown ();
	fputs (errortxt2, stderr);
	fputs (text, stderr);
	fputs ("\n\n", stderr);
	Sys_ConsoleCleanup ();
	if (!isDedicated)
		PL_ErrorDialog(text);

	exit (1);
}

void Sys_InstallDedicatedSignalHandlers (void)
{
	struct sigaction sa;

	if (!isDedicated)
		return;

	memset (&sa, 0, sizeof(sa));
	sa.sa_handler = Sys_DedicatedSignalHandler;
	sigemptyset (&sa.sa_mask);

	sigaction (SIGTERM, &sa, NULL);
	sigaction (SIGINT, &sa, NULL);
	sigaction (SIGHUP, &sa, NULL);
#ifdef SIGQUIT
	sigaction (SIGQUIT, &sa, NULL);
#endif
}

qboolean Sys_HasDedicatedQuitRequest (void)
{
	return dedicated_shutdown_signal != 0;
}

/* ============================================================
   Dedicated console scrollback
   ============================================================ */
#define SCROLLBACK_MAXLINES 2048
#define SCROLLBACK_LINESIZE 1024
#define DED_CHAT_COLOR_ON   0x1d
#define DED_CHAT_COLOR_OFF  0x1e

static char  scrollback_lines[SCROLLBACK_MAXLINES][SCROLLBACK_LINESIZE];
static char  scrollback_partial[SCROLLBACK_LINESIZE];
static int   scrollback_partial_len = 0;
static int   scrollback_head = 0;   // next write slot (ring)
static int   scrollback_count = 0;  // lines stored
static int   scrollback_offset = 0; // 0 = bottom, >0 = scrolled up
static qboolean scrollback_active = false;
static int   scrollback_enter_head = 0; // head position when we entered scrollback
static int   scrollback_enter_partial_len = 0;
static qboolean scrollback_wrapped = false; // buffer wrapped fully during scrollback
static qboolean mouse_reporting_enabled = false;

static qboolean Sys_DedConsoleWantsMouseReporting (void)
{
	return (sys_dedmouse_capture.value != 0) || scrollback_active;
}

static void Sys_ConsoleSetMouseReporting (qboolean enabled)
{
	if (!term_initialized || mouse_reporting_enabled == enabled)
		return;

	printf (enabled ? "\033[?1000h\033[?1006h" : "\033[?1000l\033[?1006l");
	fflush (stdout);
	mouse_reporting_enabled = enabled;
}

static void Sys_DrainConsoleInput (void)
{
	fd_set set;
	struct timeval timeout;
	char junk[64];
	ssize_t nread;
	int flags, restore_flags = -1;
	int loops;

	flags = fcntl (0, F_GETFL, 0);
	if (flags != -1 && !(flags & O_NONBLOCK))
	{
		if (fcntl (0, F_SETFL, flags | O_NONBLOCK) != -1)
			restore_flags = flags;
	}

	/* Bounded drain: never wait forever if input keeps arriving. */
	for (loops = 0; loops < 20; loops++) /* ~200ms max */
	{
		FD_ZERO (&set);
		FD_SET (0, &set);
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000;

		if (select (1, &set, NULL, NULL, &timeout) > 0)
		{
			do
			{
				nread = read (0, junk, sizeof(junk));
			} while (nread > 0);
		}
	}

	if (restore_flags != -1)
		fcntl (0, F_SETFL, restore_flags);
}

static void Sys_ConsoleForceSaneTermios (int fd)
{
	struct termios sane;

	if (tcgetattr (fd, &sane) == -1)
		return;

	sane.c_lflag |= (ICANON | ECHO | IEXTEN | ISIG);
	sane.c_iflag |= (BRKINT | ICRNL | IXON);
	sane.c_iflag &= ~(INLCR | IGNCR | ISTRIP | IXOFF);
	sane.c_oflag |= OPOST;
	sane.c_cc[VMIN] = 1;
	sane.c_cc[VTIME] = 0;
	tcsetattr (fd, TCSANOW, &sane);
}

static int Sys_ConsoleOpenTTY (int flags)
{
	return open ("/dev/tty", flags | O_NOCTTY);
}

static void Sys_ConsoleWriteTTY (const char *text, size_t len)
{
	int ttyfd = Sys_ConsoleOpenTTY (O_WRONLY);

	if (ttyfd != -1)
	{
		while (len > 0)
		{
			ssize_t nwritten = write (ttyfd, text, len);

			if (nwritten > 0)
			{
				text += nwritten;
				len -= (size_t)nwritten;
				continue;
			}

			if (nwritten < 0 && errno == EINTR)
				continue;

			break;
		}
		tcdrain (ttyfd);
		close (ttyfd);
		return;
	}

	fwrite (text, 1, len, stdout);
	fflush (stdout);
	tcdrain (STDOUT_FILENO);
}

static void Sys_ConsoleRestoreSavedTermios (void)
{
	int ttyfd, fd, rc;

	if (!term_initialized)
		return;

	ttyfd = Sys_ConsoleOpenTTY (O_RDWR);
	fd = (ttyfd != -1) ? ttyfd : 0;
	rc = tcsetattr (fd, TCSANOW, &orig_termios_saved);
	if (rc == -1)
		Sys_ConsoleForceSaneTermios (fd);

	if (ttyfd != -1)
		close (ttyfd);
}


static void Sys_ConsoleCleanup (void)
{
	qboolean need_drain;
	int rc, ttyfd;

	if (!term_initialized)
		return;

	need_drain = mouse_reporting_enabled;
	Sys_ConsoleSetMouseReporting (false);
	Sys_ConsoleWriteTTY (sys_console_reset_seq, sizeof(sys_console_reset_seq) - 1);
	if (need_drain)
		Sys_DrainConsoleInput ();
	tcflush (0, TCIFLUSH);
	ttyfd = Sys_ConsoleOpenTTY (O_RDWR);
	if (ttyfd == -1)
		ttyfd = 0;
	rc = tcsetattr (ttyfd, TCSAFLUSH, &orig_termios_saved);
	if (rc == -1)
		Sys_ConsoleForceSaneTermios (ttyfd);
	if (ttyfd != 0)
		close (ttyfd);
	if (console_signal_ttyfd != -1)
	{
		close (console_signal_ttyfd);
		console_signal_ttyfd = -1;
	}
	mouse_reporting_enabled = false;
	term_initialized = false;
}

static void Scrollback_CommitLine (void)
{
	scrollback_partial[scrollback_partial_len] = '\0';
	q_strlcpy (scrollback_lines[scrollback_head], scrollback_partial, SCROLLBACK_LINESIZE);
	scrollback_head = (scrollback_head + 1) % SCROLLBACK_MAXLINES;
	if (scrollback_count < SCROLLBACK_MAXLINES)
		scrollback_count++;
	else if (scrollback_active)
		scrollback_wrapped = true; // ring wrapped past enter_head
	scrollback_partial_len = 0;
	scrollback_partial[0] = '\0';
}

static void Scrollback_Feed (const char *text)
{
	while (*text)
	{
		if (*text == '\n')
		{
			Scrollback_CommitLine ();
		}
		else if (*text != '\r' && scrollback_partial_len < SCROLLBACK_LINESIZE - 1)
		{
			scrollback_partial[scrollback_partial_len++] = *text;
			scrollback_partial[scrollback_partial_len] = '\0';
		}
		text++;
	}
}

static int Scrollback_TermHeight (void)
{
	struct winsize ws;
	if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
		return ws.ws_row;
	return 24;
}

/* Dedicated console input state (file-scope so Sys_Printf and scrollback can access) */
static char	ded_input[MAXCMDLINE];
static int	ded_input_len;
static int	ded_input_cursor;

static int Scrollback_TermWidth (void)
{
	struct winsize ws;
	if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return ws.ws_col;
	return 80;
}

static qboolean Scrollback_ShowCaretRow (void)
{
	return scrollback_offset > 0;
}

static int Scrollback_VisibleRowsForOffset (int offset)
{
	int reserved = 1; // input row is always visible

	if (offset > 0)
		reserved++; // caret row only while actually scrolled up

	{
		int visible = Scrollback_TermHeight () - reserved;
		return (visible < 1) ? 1 : visible;
	}
}

static int Scrollback_VisibleRows (void)
{
	return Scrollback_VisibleRowsForOffset (scrollback_offset);
}

static int Scrollback_TotalLines (void)
{
	return scrollback_count + ((scrollback_partial_len > 0) ? 1 : 0);
}

static int Scrollback_LineRows (const char *line, int width)
{
	int cols = 0;

	if (width < 1)
		width = 1;

	while (*line)
	{
		if ((unsigned char)*line == 27 && line[1] == '[')
		{
			line += 2;
			while (*line && !(*line >= '@' && *line <= '~'))
				line++;
			if (*line)
				line++;
			continue;
		}

		if (((unsigned char)*line & 0xC0) == 0x80)
		{
			line++;
			continue;
		}

		cols++;
		line++;
	}

	return (cols <= 0) ? 1 : ((cols - 1) / width) + 1;
}

static int Scrollback_MaxOffset (void)
{
	int maxoff = Scrollback_TotalLines () - Scrollback_VisibleRowsForOffset (1);
	return (maxoff < 0) ? 0 : maxoff;
}

static qboolean Scrollback_AtBottom (void)
{
	return scrollback_offset <= 0;
}

static qboolean Scrollback_Frozen (void)
{
	return scrollback_active && !Scrollback_AtBottom ();
}

static void Scrollback_Redraw (void)
{
	int height = Scrollback_TermHeight ();
	int width = Scrollback_TermWidth ();
	int total = Scrollback_TotalLines ();
	int visible;
	int first, end, rows, i;

	// Clamp offset
	if (scrollback_offset > Scrollback_MaxOffset ())
		scrollback_offset = Scrollback_MaxOffset ();
	if (scrollback_offset < 0)
		scrollback_offset = 0;
	Sys_ConsoleSetMouseReporting (Sys_DedConsoleWantsMouseReporting ());

	visible = Scrollback_VisibleRows ();
	end = total - scrollback_offset;
	if (end < 0)
		end = 0;
	if (end > total)
		end = total;

	first = end;
	rows = 0;
	while (first > 0)
	{
		const char *line;
		int line_rows;

		if (first - 1 == scrollback_count)
			line = scrollback_partial;
		else
		{
			int idx = (scrollback_head - scrollback_count + first - 1 + SCROLLBACK_MAXLINES) % SCROLLBACK_MAXLINES;
			line = scrollback_lines[idx];
		}

		line_rows = Scrollback_LineRows (line, width);

		if (rows + line_rows > visible && rows > 0)
			break;

		rows += line_rows;
		first--;
	}

	printf ("\033[2J\033[H"); // clear screen, cursor home

	for (i = first; i < end; i++)
	{
		if (i == scrollback_count)
			printf ("%s", scrollback_partial);
		else
		{
			int idx = (scrollback_head - scrollback_count + i + SCROLLBACK_MAXLINES) % SCROLLBACK_MAXLINES;
			printf ("%s\n", scrollback_lines[idx]);
		}
	}

	// Caret row — only when scrolled above the live bottom
	if (Scrollback_ShowCaretRow ())
	{
		int j;
		printf ("\033[%d;1H", height - 1);
		for (j = 0; j < width; j++)
			putchar ((j % 4 == 1) ? '^' : ' ');
	}

	// Input line on the bottom row
	printf ("\033[%d;1H\033[2K", height);
	if (ded_input_len > 0)
		fwrite (ded_input, 1, ded_input_len, stdout);
	if (ded_input_cursor < ded_input_len)
		printf ("\033[%d;%dH", height, ded_input_cursor + 1);
	fflush (stdout);
}

static void Scrollback_Enter (void)
{
	scrollback_active = true;
	scrollback_enter_head = scrollback_head;
	scrollback_enter_partial_len = scrollback_partial_len;
	scrollback_offset = Scrollback_VisibleRowsForOffset (1); // start one page up
	if (scrollback_offset > Scrollback_MaxOffset ())
		scrollback_offset = Scrollback_MaxOffset ();
	Sys_ConsoleSetMouseReporting (Sys_DedConsoleWantsMouseReporting ());
	printf ("\033[?1049h"); // alternate screen buffer
	fflush (stdout);
	Scrollback_Redraw ();
}

static void Ded_ClearInputLine (void);  // forward decl
static void Ded_RestoreInputLine (void); // forward decl

static void Scrollback_Exit (void)
{
	int missed, i;

	printf ("\033[?1049l"); // restore main screen buffer
	fflush (stdout);
	scrollback_active = false;
	scrollback_offset = 0;
	Sys_ConsoleSetMouseReporting (Sys_DedConsoleWantsMouseReporting ());

	// Clear the pending input line before replaying, so replay
	// doesn't append onto partially-typed text
	Ded_ClearInputLine ();

	// Replay any lines that arrived while we were scrolled up
	if (scrollback_wrapped)
	{
		// Ring buffer wrapped fully — replay everything we have
		missed = scrollback_count;
	}
	else
	{
		missed = (scrollback_head - scrollback_enter_head + SCROLLBACK_MAXLINES) % SCROLLBACK_MAXLINES;
		if (missed > scrollback_count)
			missed = scrollback_count;
	}
	scrollback_wrapped = false;

	if (missed > 0)
	{
		for (i = missed; i > 0; i--)
		{
			int idx = (scrollback_head - i + SCROLLBACK_MAXLINES) % SCROLLBACK_MAXLINES;
			printf ("%s\n", scrollback_lines[idx]);
		}
	}

	// Flush any partial line fragment that hasn't been newline-terminated
	if (scrollback_partial_len > scrollback_enter_partial_len)
	{
		scrollback_partial[scrollback_partial_len] = '\0';
		printf ("%s", scrollback_partial + scrollback_enter_partial_len);
	}

	fflush (stdout);

	// Restore the input line after all replay output
	Ded_RestoreInputLine ();
}

static void Scrollback_PageUp (void)
{
	scrollback_offset += Scrollback_VisibleRowsForOffset (1);
	Scrollback_Redraw (); // clamp happens inside
}

static void Scrollback_PageDown (void)
{
	if (Scrollback_AtBottom ())
	{
		Scrollback_Exit ();
		return;
	}
	scrollback_offset -= Scrollback_VisibleRows ();
	if (scrollback_offset < 0)
		scrollback_offset = 0;
	Scrollback_Redraw ();
}

static void Scrollback_LineUp (void)
{
	scrollback_offset++;
	Scrollback_Redraw ();
}

static void Scrollback_LineDown (void)
{
	if (Scrollback_AtBottom ())
	{
		Scrollback_Exit ();
		return;
	}
	scrollback_offset--;
	if (scrollback_offset < 0)
		scrollback_offset = 0;
	Scrollback_Redraw ();
}

static void Scrollback_MouseUp (void)
{
	if (!scrollback_active)
		Scrollback_Enter ();
	else
	{
		scrollback_offset += 3;
		Scrollback_Redraw ();
	}
}

static void Scrollback_MouseDown (void)
{
	if (!scrollback_active)
		return;
	if (Scrollback_AtBottom ())
	{
		Scrollback_Exit ();
		return;
	}
	scrollback_offset -= 3;
	if (scrollback_offset < 0)
		scrollback_offset = 0;
	Scrollback_Redraw ();
}

static void Scrollback_HandleMouseButton (int btn)
{
	if (btn & 64)
	{
		if (btn & 1)
			Scrollback_MouseDown ();
		else
			Scrollback_MouseUp ();
	}
}

static qboolean Scrollback_ReadLegacyMouseSequence (fd_set *set)
{
	char mouse[3] = {0};
	int got = 0;

	while (got < 3)
	{
		struct timeval mt;
		mt.tv_sec = 0;
		mt.tv_usec = 10000;
		FD_ZERO(set);
		FD_SET(0, set);
		if (select(1, set, NULL, NULL, &mt) <= 0)
			return false;
		if (read(0, &mouse[got], 1) != 1)
			return false;
		got++;
	}

	Scrollback_HandleMouseButton (mouse[0] - 32);
	return true;
}

static qboolean Scrollback_ReadSGRMouseSequence (fd_set *set)
{
	char mouse[32];
	int got = 0;
	int btn;

	while (got < (int)sizeof(mouse) - 1)
	{
		struct timeval mt;
		mt.tv_sec = 0;
		mt.tv_usec = 10000;
		FD_ZERO(set);
		FD_SET(0, set);
		if (select(1, set, NULL, NULL, &mt) <= 0)
			return false;
		if (read(0, &mouse[got], 1) != 1)
			return false;
		if (mouse[got] == 'M' || mouse[got] == 'm')
		{
			got++;
			break;
		}
		got++;
	}

	mouse[got] = '\0';
	if (got <= 0)
		return false;
	if (sscanf(mouse, "%d;%*d;%*d%*c", &btn) != 1)
		return false;

	Scrollback_HandleMouseButton (btn);
	return true;
}

static void safe_write(int fd, const void* buf, size_t count); // forward decl

static char	ded_output_hold[8192];
static size_t	ded_output_hold_len;

static void Ded_ClearInputLine (void)
{
	int i;
	if (ded_input_len <= 0)
		return;
	fflush (stdout); // drain buffered printf before raw writes
	safe_write (1, "\r", 1);
	for (i = 0; i < ded_input_len; i++)
		safe_write (1, " ", 1);
	safe_write (1, "\r", 1);
}

static void Ded_RestoreInputLine (void)
{
	int i;
	if (ded_input_len <= 0)
		return;
	fflush (stdout); // drain buffered printf before raw writes
	safe_write (1, ded_input, ded_input_len);
	for (i = ded_input_len; i > ded_input_cursor; i--)
		safe_write (1, "\b", 1);
}

static void Ded_WriteOutput (const char *text, size_t len)
{
	if (len == 0)
		return;
	fwrite (text, 1, len, stdout);
	fflush (stdout);
}

static void Ded_FlushBufferedOutput (qboolean allow_partial)
{
	size_t emit = 0;

	if (!ded_output_hold_len)
		return;

	if (allow_partial)
	{
		emit = ded_output_hold_len;
	}
	else
	{
		size_t i;
		for (i = ded_output_hold_len; i > 0; i--)
		{
			if (ded_output_hold[i - 1] == '\n')
			{
				emit = i;
				break;
			}
		}
		if (!emit)
			return;
	}

	if (ded_input_len > 0)
		Ded_ClearInputLine ();

	Ded_WriteOutput (ded_output_hold, emit);

	if (emit < ded_output_hold_len)
		memmove (ded_output_hold, ded_output_hold + emit, ded_output_hold_len - emit);
	ded_output_hold_len -= emit;

	if (ded_input_len > 0)
		Ded_RestoreInputLine ();
}

static void Ded_HandleOutput (const char *text)
{
	size_t len = strlen (text);

	if (!len)
		return;

	Scrollback_Feed (text);
	if (Scrollback_Frozen ())
		return;

	if (ded_input_len <= 0 && !ded_output_hold_len)
	{
		Ded_WriteOutput (text, len);
		return;
	}

	if (len > sizeof(ded_output_hold) - ded_output_hold_len)
	{
		Ded_FlushBufferedOutput (ded_input_len <= 0);
		if (len > sizeof(ded_output_hold) - ded_output_hold_len && ded_input_len <= 0)
			Ded_FlushBufferedOutput (true);
	}

	if (len > sizeof(ded_output_hold) - ded_output_hold_len)
	{
		size_t keep = sizeof(ded_output_hold) - 1;
		if (len >= keep)
		{
			if (ded_input_len > 0)
			{
				memcpy (ded_output_hold, text + len - keep, keep);
				ded_output_hold_len = keep;
			}
			else
			{
				Ded_WriteOutput (text, len);
				ded_output_hold_len = 0;
			}
			return;
		}
	}

	memcpy (ded_output_hold + ded_output_hold_len, text, len);
	ded_output_hold_len += len;

	Ded_FlushBufferedOutput (ded_input_len <= 0);
}

void Sys_Printf (const char *fmt, ...)
{
	va_list argptr;
	char text[1024];
	static int use_color = -1;

	va_start(argptr, fmt);
	q_vsnprintf (text, sizeof (text), fmt, argptr);
	va_end(argptr);

	if (use_color == -1)
		use_color = isatty(fileno(stdout));

	if (!use_color)
	{
		// No color: strip high bits and dequake in-place, middle dot for bullet chars
		char nocolor[2048];
		unsigned char *ch = (unsigned char *)text;
		char *dst = nocolor;
		char *nc_end = nocolor + sizeof(nocolor) - 4;
		while (*ch && dst < nc_end)
		{
			if (*ch == DED_CHAT_COLOR_ON || *ch == DED_CHAT_COLOR_OFF)
			{
				ch++;
				continue;
			}
			if (*ch == '^' && *(ch + 1) != '\0' &&
				(*(ch + 1) == 'm' || *(ch + 1) == 'g' || *(ch + 1) == 'd'))
			{
				ch += 2;
				continue;
			}
			if (*ch == 5 || *ch == 14 || *ch == 15 || *ch == 28 ||
			    *ch == 133 || *ch == 142 || *ch == 143 || *ch == 156)
			{
				*dst++ = (char)0xC2;
				*dst++ = (char)0xB7; // UTF-8 middle dot ·
				ch++;
			}
			else if (*ch == 11 || *ch == 139)
			{
				*dst++ = (char)0xE2;
				*dst++ = (char)0x96;
				*dst++ = (char)0xA0; // UTF-8 black square ■
				ch++;
			}
			else if (*ch == 141)
			{
				*dst++ = (char)0xE2;
				*dst++ = (char)0x96;
				*dst++ = (char)0xB6; // UTF-8 play arrow ▶
				ch++;
			}
			else
			{
				*dst++ = dequake[*ch++];
			}
		}
		*dst = '\0';
		Ded_HandleOutput (nocolor);
		return;
	}

		// Color mode: 24-bit ANSI true color
		// 0=normal, 1=red #a85c4c, 2=gold #8d7039, 3=brackets #c97d49
		{
			char output[8192];
			unsigned char *ch = (unsigned char *)text;
			char *dst = output;
			char *end = output + sizeof(output) - 32;
			int cur_color = 0;
			qboolean chat_color = false;
			qboolean gold_digits = false;

			while (*ch && dst < end)
			{
				int want;

				if (*ch == DED_CHAT_COLOR_ON)
				{
					chat_color = true;
					ch++;
					continue;
				}
				if (*ch == DED_CHAT_COLOR_OFF)
				{
					chat_color = false;
					ch++;
					continue;
				}
				if (*ch == '^' && *(ch + 1) != '\0' && *(ch + 1) == 'm')
				{
					ch += 2;
					continue;
				}
				if (*ch == '^' && *(ch + 1) != '\0' && *(ch + 1) == 'd')
				{
					gold_digits = false;
					ch += 2;
					continue;
				}
				if (*ch == '^' && *(ch + 1) != '\0' && *(ch + 1) == 'g')
				{
					gold_digits ^= true;
					ch += 2;
					continue;
				}

				if (chat_color)
					want = 4; // say / say_team message text
				else if (gold_digits && *ch >= '0' && *ch <= '9')
					want = 2; // explicit gold digit markup
				else if ((*ch >= 18 && *ch <= 27) || (*ch >= 146 && *ch <= 155) ||
				    *ch == 133 || *ch == 142 || *ch == 143 || *ch == 156)
					want = 2; // gold digits / gold dots
				else if ((*ch >= 16 && *ch <= 17) || (*ch >= 144 && *ch <= 145))
					want = 3; // brackets
			else if (*ch == 11 || *ch == 139)
				want = 1; // red squares
			else if (*ch > 127)
				want = 1; // red
			else
				want = 0; // normal

			if (want != cur_color)
			{
				const char *esc;
				int esc_len;
				if (want == 0)
				{
					esc = "\033[0m";
				}
					else if (want == 1) // #a85c4c via ANSI 256 color 95
					{
						esc = "\033[38;5;95m";
					}
					else if (want == 2) // #8d7039 via ANSI 256 color 136
					{
						esc = "\033[38;5;136m";
					}
					else if (want == 3) // #c97d49 via ANSI 256 color 173
					{
						esc = "\033[38;5;173m";
					}
					else // chat text via ANSI 256 color 247
					{
						esc = "\033[38;5;247m";
					}
					esc_len = (int)strlen (esc);
					memcpy (dst, esc, esc_len);
					dst += esc_len;
				cur_color = want;
			}

			if (*ch == 5 || *ch == 14 || *ch == 15 || *ch == 28 ||
			    *ch == 133 || *ch == 142 || *ch == 143 || *ch == 156)
			{
				*dst++ = (char)0xC2;
				*dst++ = (char)0xB7; // UTF-8 middle dot ·
				ch++;
			}
			else if (*ch == 11 || *ch == 139)
			{
				*dst++ = (char)0xE2;
				*dst++ = (char)0x96;
				*dst++ = (char)0xA0; // UTF-8 black square ■
				ch++;
			}
			else if (*ch == 141)
			{
				*dst++ = (char)0xE2;
				*dst++ = (char)0x96;
				*dst++ = (char)0xB6; // UTF-8 play arrow ▶
				ch++;
			}
			else
			{
				*dst++ = dequake[*ch++];
			}
		}

		if (cur_color)
		{
			memcpy (dst, "\033[0m", 4);
			dst += 4;
		}

		*dst = '\0';
		Ded_HandleOutput (output);
	}
}

void Sys_Quit (void)
{
	int exit_status = 0;

	if (!isDedicated)
	{
		Host_Shutdown();
		exit (0);
	}

	if (dedicated_shutdown_signal != 0)
		exit_status = 128 + dedicated_shutdown_signal;

	Sys_ConsoleSetMouseReporting (false);
	Host_Shutdown();

	/* For normal quit, leave tty mode handoff to the shell, but
	   explicitly disable the private terminal modes we turned on. */
	Sys_ConsoleWriteTTY (sys_console_quit_reset_seq, sizeof(sys_console_quit_reset_seq) - 1);
	Sys_ConsoleRestoreSavedTermios ();
	fflush (stdout);
	_exit (exit_status);
}

double Sys_DoubleTime (void)
{
	return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static void safe_write(int fd, const void* buf, size_t count) // woods #arrowkeys
{
	ssize_t result = write(fd, buf, count);
	if (result == -1) {
	}
}

static void Dedicated_RedrawInputLine(const char* text, int textlen, int cursor_pos, int previous_len)
{
	const char carriage = '\r';
	const char space = ' ';

	safe_write(1, &carriage, 1);
	if (textlen > 0)
		safe_write(1, text, (size_t)textlen);

	if (previous_len > textlen)
	{
		int diff = previous_len - textlen;
		for (int i = 0; i < diff; ++i)
			safe_write(1, &space, 1);
	}

	safe_write(1, &carriage, 1);
	if (cursor_pos > 0)
		safe_write(1, text, (size_t)cursor_pos);
}

static void Sys_RewriteInputLine(const char* newline, char* con_text, size_t con_text_size, int* textlen, int* cursor_pos) // woods #serverhistory
{
	int oldlen = *textlen;
	int oldpos = *cursor_pos;
	size_t newlen;

	for (int i = 0; i < oldpos; i++)
		safe_write(1, "\b", 1);
	for (int i = 0; i < oldlen; i++)
		safe_write(1, " ", 1);
	for (int i = 0; i < oldlen; i++)
		safe_write(1, "\b", 1);

	newlen = q_strlcpy(con_text, newline ? newline : "", con_text_size);
	if (newlen)
		safe_write(1, con_text, newlen);

	*textlen = (int)newlen;
	*cursor_pos = *textlen;
}

const char *Sys_ConsoleInput (void) // woods #arrowkeys #serverhistory
{
	// Input state is in file-scope ded_input / ded_input_len / ded_input_cursor
	static qboolean	con_eof = false;
	char		c;
	fd_set		set;
	struct timeval	timeout;
    static struct termios orig_termios, raw_termios;
    static qboolean term_setup = false;

	if (!stdinIsATTY || con_eof)
		return NULL;

    // Set up terminal once
    if (!term_setup)
    {
	        if (tcgetattr(0, &orig_termios) != -1)
	        {
	            raw_termios = orig_termios;
	            raw_termios.c_lflag &= ~(ICANON | ECHO);  // Disable canonical mode and echo
	            raw_termios.c_cc[VMIN] = 1;
	            raw_termios.c_cc[VTIME] = 0;
            tcsetattr(0, TCSANOW, &raw_termios);
	            orig_termios_saved = orig_termios;
		            term_initialized = true;
		            Sys_ConsoleEnsureSignalTTY ();
		            term_setup = true;
		            ded_input_cursor = 0;
		    }
	    }

	    Sys_ConsoleSetMouseReporting (Sys_DedConsoleWantsMouseReporting ());
	    FD_ZERO (&set);
		FD_SET (0, &set);	// stdin
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    while (select (1, &set, NULL, NULL, &timeout))
    {
        ssize_t len = read(0, &c, 1);
        if (len != 1)
        {
			con_eof = true;
			if (ded_input_len <= 0)
				return NULL;
			c = '\n';
        }

        // Handle escape sequences for arrow keys, PageUp/PageDown
        if (c == 27) // ESC character
        {
            char seq[4] = {0};
            struct timeval seq_timeout;
            qboolean exit_scrollback = scrollback_active;
            seq_timeout.tv_sec = 0;
            seq_timeout.tv_usec = 10000;

            FD_ZERO(&set);
            FD_SET(0, &set);
            if (select(1, &set, NULL, NULL, &seq_timeout) > 0)
            {
                len = read(0, seq, 1);
                if (len == 1 && seq[0] == '[')
                {
                    FD_ZERO(&set);
                    FD_SET(0, &set);
                    if (select(1, &set, NULL, NULL, &seq_timeout) > 0)
                    {
                        len = read(0, seq + 1, 1);
	                        if (len == 1)
	                        {
	                            // Mouse event: ESC [ M btn x y
	                            if (seq[1] == 'M')
	                            {
	                                if (Scrollback_ReadLegacyMouseSequence (&set))
	                                    exit_scrollback = false;
	                                else if (exit_scrollback)
	                                    Scrollback_Exit ();
	                                continue;
	                            }

	                            // SGR mouse event: ESC [ < btn ; x ; y M/m
	                            if (seq[1] == '<')
	                            {
	                                if (Scrollback_ReadSGRMouseSequence (&set))
	                                    exit_scrollback = false;
	                                else if (exit_scrollback)
	                                    Scrollback_Exit ();
	                                continue;
	                            }

                            // Extended sequences: ESC [ 5 ~ (PageUp), ESC [ 6 ~ (PageDown)
                            if (seq[1] == '5' || seq[1] == '6')
                            {
                                FD_ZERO(&set);
                                FD_SET(0, &set);
                                if (select(1, &set, NULL, NULL, &seq_timeout) > 0)
                                {
                                    len = read(0, seq + 2, 1);
                                    if (len == 1 && seq[2] == '~')
                                    {
                                        if (seq[1] == '5') // PageUp
                                        {
                                            if (!scrollback_active)
                                                Scrollback_Enter ();
                                            else
                                                Scrollback_PageUp ();
                                        }
                                        else // PageDown
                                        {
                                            if (scrollback_active)
                                                Scrollback_PageDown ();
                                        }
                                        exit_scrollback = false;
                                        continue;
                                    }
                                }
                                if (exit_scrollback)
                                    Scrollback_Exit ();
                                continue;
                            }

                            // In scrollback mode, arrow keys scroll instead of editing
	                            if (Scrollback_Frozen ())
	                            {
	                                switch (seq[1])
	                                {
                                    case 'A': Scrollback_LineUp (); break;   // Up
                                    case 'B': Scrollback_LineDown (); break; // Down
                                }
                                exit_scrollback = false;
                                continue;
                            }

                            switch (seq[1])
                            {
                                case 'D': // Left arrow
                                    if (ded_input_cursor > 0)
                                    {
                                        ded_input_cursor--;
                                        safe_write(1, "\b", 1);
                                    }
                                    continue;
                                case 'C': // Right arrow
                                    if (ded_input_cursor < ded_input_len)
                                    {
                                        safe_write(1, &ded_input[ded_input_cursor], 1);
                                        ded_input_cursor++;
                                    }
                                    continue;
                                case 'A': // Up arrow
                                {
                                    char history_line[MAXCMDLINE];
                                    if (History_GetPrevious(ded_input, history_line, sizeof(history_line)))
                                        Sys_RewriteInputLine(history_line, ded_input, sizeof(ded_input), &ded_input_len, &ded_input_cursor);
                                    continue;
                                }
                                case 'B': // Down arrow
                                {
                                    char history_line[MAXCMDLINE];
                                    if (History_GetNext(ded_input, history_line, sizeof(history_line)))
                                        Sys_RewriteInputLine(history_line, ded_input, sizeof(ded_input), &ded_input_len, &ded_input_cursor);
                                    continue;
                                }
                            }
                        }
                    }
                }
            }
            if (exit_scrollback)
            {
                Scrollback_Exit ();
            }
            continue;
        }

        // Ignore all other keys while in scrollback mode
	        if (Scrollback_Frozen ())
	            continue;

        if (c == 21) // Ctrl-U
        {
            Sys_RewriteInputLine(NULL, ded_input, sizeof(ded_input), &ded_input_len, &ded_input_cursor);
            Con_DedicatedResetTabState();
            if (!ded_input_len)
                Ded_FlushBufferedOutput (true);
            continue;
        }

        if (c == '\n' || c == '\r')
        {
			safe_write(1, "\n", 1);
            ded_input[ded_input_len] = '\0';
			History_StoreCommand(ded_input);
            ded_input_len = 0;
            ded_input_cursor = 0;
            Con_DedicatedResetTabState();
            Ded_FlushBufferedOutput (true);
            return ded_input;
        }
        else if (c == '\t')
        {
			ded_input[ded_input_len] = '\0';
            int previous_len = ded_input_len;
            Con_DedicatedTabComplete(ded_input, sizeof(ded_input), &ded_input_len, &ded_input_cursor);
            Dedicated_RedrawInputLine(ded_input, ded_input_len, ded_input_cursor, previous_len);
            continue;
        }
        else if (c == 8 || c == 127)    // backspace or delete
        {
            if (ded_input_cursor > 0)
            {
                memmove(&ded_input[ded_input_cursor - 1], &ded_input[ded_input_cursor], ded_input_len - ded_input_cursor);
                ded_input_cursor--;
                ded_input_len--;

				safe_write(1, "\b", 1);
                if (ded_input_cursor < ded_input_len)
                {
					safe_write(1, &ded_input[ded_input_cursor], ded_input_len - ded_input_cursor);
					safe_write(1, " ", 1);
                    for (int i = 0; i < ded_input_len - ded_input_cursor + 1; i++)
						safe_write(1, "\b", 1);
                }
                else
                {
					safe_write(1, " \b", 2);
                }
                Con_DedicatedResetTabState();
                if (!ded_input_len)
                    Ded_FlushBufferedOutput (true);
            }
            continue;
        }

        if (ded_input_len < (int)sizeof(ded_input)-1 && c >= 32 && c < 127)
        {
            if (ded_input_cursor < ded_input_len)
            {
                memmove(&ded_input[ded_input_cursor + 1], &ded_input[ded_input_cursor], ded_input_len - ded_input_cursor);
                ded_input[ded_input_cursor] = c;
                ded_input_len++;

				safe_write(1, &ded_input[ded_input_cursor], ded_input_len - ded_input_cursor);

                ded_input_cursor++;
                for (int i = 0; i < ded_input_len - ded_input_cursor; i++)
					safe_write(1, "\b", 1);
            }
            else
            {
                ded_input[ded_input_len] = c;
				safe_write(1, &c, 1);
                ded_input_len++;
                ded_input_cursor++;
            }
            Con_DedicatedResetTabState();
        }
    }

    return NULL;
}

void Sys_Sleep (unsigned long msecs)
{
/*	usleep (msecs * 1000);*/
	SDL_Delay (msecs);
}

void Sys_SendKeyEvents (void)
{
	IN_Commands();		//ericw -- allow joysticks to add keys so they can be used to confirm SCR_ModalMessage
	IN_SendKeyEvents();
}

#ifndef __APPLE__
// macOS provides a real implementation in SDLMain.m (dock icon progress ring).
void Sys_SetDockProgress (float fraction)
{
	(void)fraction;
}

void Sys_IncrementDockNotificationBadge (void)
{
}

void Sys_ClearDockNotificationBadge (void)
{
}
#endif

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

void Sys_Image_BGRA_To_Clipboard(byte* buffer, int width, int height, int buffersize) // woods #screenshotcopy
{
	CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, buffer, buffersize, NULL);

	CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

	CGImageRef imageRef = CGImageCreate(
		width,
		height,
		8,                      // bits per component
		32,                     // bits per pixel
		width * 4,              // bytes per row
		colorSpace,
		kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst, // woods #screenshotcopy -- ignore framebuffer alpha (menu fade lowers dst alpha, premultiplied-first would render it dimmed)
		provider,
		NULL,
		false,
		kCGRenderingIntentDefault
	);

	// Create an image destination to hold the image data
	CFMutableDataRef imageData = CFDataCreateMutable(NULL, 0);
	CGImageDestinationRef destination = CGImageDestinationCreateWithData(imageData, kUTTypeTIFF, 1, NULL);
	if (!destination) {
		// Handle error
		CGImageRelease(imageRef);
		CGColorSpaceRelease(colorSpace);
		CGDataProviderRelease(provider);
		CFRelease(imageData);
		return;
	}

	CGImageDestinationAddImage(destination, imageRef, NULL);

	if (!CGImageDestinationFinalize(destination)) {
		// Handle error
		CFRelease(destination);
		CGImageRelease(imageRef);
		CGColorSpaceRelease(colorSpace);
		CGDataProviderRelease(provider);
		CFRelease(imageData);
		return;
	}

	// Get the pasteboard
	PasteboardRef pasteboard;
	OSStatus status = PasteboardCreate(kPasteboardClipboard, &pasteboard);
	if (status != noErr) {
		// Handle error
		CFRelease(destination);
		CGImageRelease(imageRef);
		CGColorSpaceRelease(colorSpace);
		CGDataProviderRelease(provider);
		CFRelease(imageData);
		return;
	}

	// Clear the pasteboard
	status = PasteboardClear(pasteboard);
	if (status != noErr) {
		// Handle error
		CFRelease(pasteboard);
		CFRelease(destination);
		CGImageRelease(imageRef);
		CGColorSpaceRelease(colorSpace);
		CGDataProviderRelease(provider);
		CFRelease(imageData);
		return;
	}

	// Put the image data onto the pasteboard
	PasteboardSynchronize(pasteboard);
	status = PasteboardPutItemFlavor(pasteboard, (PasteboardItemID)1, CFSTR("public.tiff"), imageData, 0);
	if (status != noErr) {
		// Handle error
	}

	// Release resources
	CFRelease(pasteboard);
	CFRelease(destination);
	CGImageRelease(imageRef);
	CGColorSpaceRelease(colorSpace);
	CGDataProviderRelease(provider);
	CFRelease(imageData);
}
#endif
