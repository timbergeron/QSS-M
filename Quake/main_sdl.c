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

#include "quakedef.h"
#include "update.h"
#include "quake_data_import.h"
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif
#include <stdio.h>
#if defined(__linux__) || defined(__APPLE__) // woods #idlesleep
#include <sys/select.h>
#include <sys/time.h>
#endif

extern cvar_t sv_idlesleep; // woods #idlespeep

void Host_Reconnect_Con_f (void);

static Uint32 lastTime = 0; // woods #idle

static void Sys_AtExit (void)
{
	SDL_Quit();
}

static void Sys_InitSDL (void)
{
#if defined(USE_SDL2)
	SDL_version v;
	SDL_version *sdl_version = &v;
	SDL_GetVersion(&v);
#else
	const SDL_version *sdl_version = SDL_Linked_Version();
#endif

	Sys_Printf("Found SDL version %i.%i.%i\n",sdl_version->major,sdl_version->minor,sdl_version->patch);

	if (SDL_Init(0) < 0) {
		Sys_Error("Couldn't init SDL: %s", SDL_GetError());
	}
	atexit(Sys_AtExit);
}

#define DEFAULT_MEMORY (384 * 1024 * 1024) // ericw -- was 72MB (64-bit) / 64MB (32-bit)

static quakeparms_t	parms;

// On OS X we call SDL_main from the launcher, but SDL2 doesn't redefine main
// as SDL_main on OS X anymore, so we do it ourselves.
#if defined(USE_SDL2) && defined(__APPLE__)
#define main SDL_main
#endif

int main(int argc, char *argv[])
{
	int		t;
	double		time, oldtime, newtime;

	if (QuakeDataImport_IsSelfTestArg(argc, argv))
		return QuakeDataImport_RunSelfTests();
	if (argc >= 2 && M_Update_IsSelfTestArg(argv[1]))
		return 0;
	if (argc >= 2 && M_Update_IsHelperArg(argv[1]))
		return M_UpdateHelperMain(argc, argv);

	host_parms = &parms;
	parms.basedir = ".";

	parms.argc = argc;
	parms.argv = argv;

	parms.errstate = 0;

	COM_InitArgv(parms.argc, parms.argv);

	isDedicated = (COM_CheckParm("-dedicated") != 0);
	M_Update_RecoverAtStartup();

	Sys_InitSDL ();

	Sys_Init();

	Sys_Printf("Initializing "ENGINE_NAME_AND_VER"\n");

	parms.memsize = DEFAULT_MEMORY;
	if (COM_CheckParm("-heapsize"))
	{	//in kb
		t = COM_CheckParm("-heapsize") + 1;
		if (t < com_argc)
			parms.memsize = Q_atoi(com_argv[t]) * 1024;
	}
	else if (COM_CheckParm("-mem"))
	{	//in mb, matching vanilla's arg on dos or linux.
		t = COM_CheckParm("-mem") + 1;
		if (t < com_argc)
			parms.memsize = Q_atoi(com_argv[t]) * 1024*1024;
	}

	parms.membase = malloc (parms.memsize);

	if (!parms.membase)
		Sys_Error ("Not enough memory free; check disk space\n");

	Sys_Printf("Host_Init\n");
	Host_Init();
	M_Update_ConfirmStartup();
	M_Update_PruneStagingAtStartup();
	if (isDedicated)
		Sys_InstallDedicatedSignalHandlers ();

	oldtime = Sys_DoubleTime();
	if (isDedicated)
	{
		while (1)
		{
			if (Sys_HasDedicatedQuitRequest ())
				Sys_Quit ();

			newtime = Sys_DoubleTime ();
			time = newtime - oldtime;

			while (time < sys_ticrate.value)
			{
				if (Sys_HasDedicatedQuitRequest ())
					Sys_Quit ();

				int i;
				qboolean hasClients = false;
#if defined(__linux__) || defined(__APPLE__)
				struct timeval timeout = { 0, 0 };
#endif

				if (sv.active) // woods #idlesleep -- sleep longer if server is empty
				{
					for (i = 0; i < svs.maxclients; i++)
					{
						if (svs.clients[i].active ||
							(svs.clients[i].netconnection != NULL) ||  // Has an active network connection
							svs.clients[i].sendsignon != PRESPAWN_DONE)  // Is in the process of connecting
						{
							hasClients = true;
							break;
						}
					}

					if (!hasClients && sv_idlesleep.value > 0)
					{
#ifdef _WIN32
						SDL_Delay(CLAMP(1, (int)sv_idlesleep.value, 50));
#else
						int delay_ms = CLAMP(1, (int)sv_idlesleep.value, 50);
						timeout.tv_sec = 0;
						timeout.tv_usec = delay_ms * 1000;
						select(0, NULL, NULL, NULL, &timeout);
#endif
					}
					else
						SDL_Delay(1);
				}
				else
					SDL_Delay(1);

				if (Sys_HasDedicatedQuitRequest ())
					Sys_Quit ();

				newtime = Sys_DoubleTime ();
				time = newtime - oldtime;
			}

			if (Sys_HasDedicatedQuitRequest ())
				Sys_Quit ();

			Host_Frame (time);
			oldtime = newtime;
		}
	}
	else
	while (1)
	{
		/* If we have no input focus at all, sleep a bit */
		if ((!listening && !VID_HasMouseOrInputFocus()) || (cl.paused && !Pong_Enabled())) // woods #listens #pong
		{
			if (sys_throttle.value >= 0)
				SDL_Delay(16);
		}
		/* If we're minimised, sleep a bit more */
		if (!listening && VID_IsMinimized()) // woods #listens
		{
			scr_skipupdate = 1;
			SDL_Delay(32);
		}
		else
		{
			scr_skipupdate = 0;
		}

		if (cl_idle.value > 0 && cls.state == ca_disconnected) // woods #idle
		{
			Uint32 currentTime = SDL_GetTicks();
			int clampedValue = CLAMP(1, cl_idle.value, 60); // don't spam servers
			Uint32 idleInterval = 60000 * clampedValue; // 60000 ms = 1 minute

			if (lastTime == 0) // first reconnect attempt after disconnection
			{
				Con_Printf("\nattempting reconnect... will retry every ^m%d^m %s if this fails\n\n",
					clampedValue, (clampedValue == 1) ? "minute" : "minutes");
				Cbuf_AddText("reconnect\n");
				lastTime = currentTime;
			}
			else if ((currentTime - lastTime) >= idleInterval)
			{
				Con_Printf("\nidle reconnect triggered after ^m%d^m %s\n\n",
					clampedValue, (clampedValue == 1) ? "minute" : "minutes");
				Cbuf_AddText("reconnect\n");
				lastTime = currentTime; // update lastTime to prevent continuous reconnection attempts
			}
		}
		else
		{
			lastTime = 0; // reset lastTime when connected or idle reconnect is disabled
		}

		newtime = Sys_DoubleTime ();
		time = newtime - oldtime;

		Host_Frame (time);

		if (time < sys_throttle.value && !cls.timedemo)
			SDL_Delay(1);

		oldtime = newtime;
	}

	return 0;
}
