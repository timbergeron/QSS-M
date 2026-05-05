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
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif
#import <Cocoa/Cocoa.h>

void PL_SetWindowIcon (void)
{
/* nothing to do on OS X */
}

void PL_VID_Shutdown (void)
{
}

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060
#define NSPasteboardTypeString NSStringPboardType
#endif
#define QSSPasteboardTypeFileURL @"public.file-url"
#define MAX_CLIPBOARDTXT	MAXCMDLINE	/* 256 */
char *PL_GetClipboardData (void)
{
    char *data			= NULL;
    NSPasteboard* pasteboard	= [NSPasteboard generalPasteboard];
    NSArray* types		= [pasteboard types];

    if ([types containsObject: NSPasteboardTypeString]) {
        NSString* clipboardString = [pasteboard stringForType: NSPasteboardTypeString];
        if (clipboardString != NULL && [clipboardString length] > 0) {
            NSData *ansiData = [clipboardString dataUsingEncoding:NSWindowsCP1252StringEncoding allowLossyConversion:YES];
            if (ansiData) {
                size_t sz = [ansiData length] + 1;
                sz = q_min((size_t)(MAX_CLIPBOARDTXT), sz);
                data = (char *) Z_Malloc((int)sz);
                memcpy(data, [ansiData bytes], sz - 1);
                data[sz - 1] = '\0';
            }
        }
    }
    return data;
}

static char *PL_CopyNSStringPath(NSString *clipboardPath)
{
    char *data = NULL;

    if (clipboardPath != nil && [clipboardPath length] > 0) {
        const char *path = [clipboardPath fileSystemRepresentation];
        if (path != NULL && path[0]) {
            size_t size = strlen(path) + 1;
            if (size <= (size_t)Q_MAXINT) {
                data = (char *) Z_Malloc((int)size);
                q_strlcpy(data, path, size);
            }
        }
    }

    return data;
}

char **PL_GetClipboardFilePaths (int *count)
{
    char **paths		= NULL;
    int local_count		= 0;
    int local_capacity		= 0;
    NSPasteboard* pasteboard	= [NSPasteboard generalPasteboard];
    NSArray* types		= [pasteboard types];

    if ([types containsObject: NSFilenamesPboardType]) {
        id filenames = [pasteboard propertyListForType: NSFilenamesPboardType];
        if ([filenames isKindOfClass: [NSArray class]]) {
            NSUInteger i, num_files = [filenames count];
            for (i = 0; i < num_files && local_count < Q_MAXINT; ++i) {
                id filePath = [filenames objectAtIndex: i];
                if ([filePath isKindOfClass: [NSString class]]) {
                    char *path = PL_CopyNSStringPath(filePath);
                    if (path)
                        PL_AddClipboardFilePath(&paths, &local_count, &local_capacity, path);
                }
            }
        }
    }

    if (local_count == 0 && [types containsObject: QSSPasteboardTypeFileURL]) {
        NSString* fileURLString = [pasteboard stringForType: QSSPasteboardTypeFileURL];
        if (fileURLString != NULL) {
            NSURL* fileURL = [NSURL URLWithString: fileURLString];
            if (fileURL != NULL && [fileURL isFileURL])
                PL_AddClipboardFilePath(&paths, &local_count, &local_capacity, PL_CopyNSStringPath([fileURL path]));
        }
    }

    if (count)
        *count = local_count;
    return paths;
}

void PL_FreeClipboardFilePaths (char **paths, int count)
{
    int i;

    if (!paths)
        return;
    for (i = 0; i < count; ++i) {
        if (paths[i])
            Z_Free(paths[i]);
    }
    Z_Free(paths);
}

char *PL_GetClipboardFilePath (void)
{
    char **paths;
    char *data = NULL;
    int count = 0;

    paths = PL_GetClipboardFilePaths(&count);
    if (paths && count > 0) {
        data = paths[0];
        paths[0] = NULL;
    }
    PL_FreeClipboardFilePaths(paths, count);
    return data;
}

#ifndef MAC_OS_X_VERSION_10_12
#define NSAlertStyleCritical NSCriticalAlertStyle
#endif
void PL_ErrorDialog(const char *errorMsg)
{
#if (MAC_OS_X_VERSION_MIN_REQUIRED < 1040)	/* ppc builds targeting 10.3 and older */
    NSString* msg = [NSString stringWithCString:errorMsg];
#else
    NSString* msg = [NSString stringWithCString:errorMsg encoding:NSASCIIStringEncoding];
#endif
#if MAC_OS_X_VERSION_MIN_REQUIRED < 1030
    NSRunCriticalAlertPanel (@"Quake Error", @"%@", @"OK", nil, nil, msg);
#else
    NSAlert *alert = [[[NSAlert alloc] init] autorelease];
    alert.alertStyle = NSAlertStyleCritical;
    alert.messageText = @"Quake Error";
    alert.informativeText = msg;
    [alert runModal];
#endif
}

