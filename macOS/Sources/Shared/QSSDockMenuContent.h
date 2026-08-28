/*
 * QSSDockMenuContent.h -- the Dock menu's contents: server history, bookmark
 * and mod readers plus the section builders that turn them into menu items.
 * Shared by the launcher and the Dock tile plug-in, which build the same menu
 * from different processes. It also resolves their common host application
 * URL so both processes survive Gatekeeper App Translocation consistently.
 *
 * Copyright (C) 2026 QSS-M team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#import <Cocoa/Cocoa.h>

/* How many entries a section shows before the rest move under "More". */
enum { QSSServerSectionLimit = 3 };

/* Entry keys. Address is always present; the rest are absent when unknown:
   date only comes from the history, alias/pinned only from bookmarks. */
extern NSString * const QSSServerAddressKey;    /* NSString */
extern NSString * const QSSServerDateKey;       /* NSDate */
extern NSString * const QSSServerAliasKey;      /* NSString */
extern NSString * const QSSServerPinnedKey;     /* NSNumber (BOOL) */

/* Gatekeeper may run an application from an AppTranslocation mount. Returns
   its existing original bundle URL when Security.framework can resolve it,
   or applicationURL for ordinary launches and as a logged fallback. Ordinary
   paths return without loading Security.framework or calling its private SPI. */
NSURL *QSSOriginalApplicationURL(NSURL *applicationURL, NSString *logPrefix);

/* Rejects addresses the engine would refuse to record or that could not be
   passed safely on a command line (see ServerHistory_ValidAddress). */
BOOL QSSValidServerAddress(NSString *server);

NSString *QSSServerAddress(NSDictionary *entry);

/* Every validated, de-duplicated entry of id1/backups/servers.json under the
   given game folder, most recent first. Falls back to the pre-JSON history
   files so the Dock menu still works after an update but before the engine
   has migrated them. */
NSArray *QSSRecentServersForGameFolder(NSURL *gameFolderURL);

/* Validated entries of id1/backups/bookmarks.json, pinned entries first so
   they take the section's visible slots, each group in file order. */
NSArray *QSSBookmarksForGameFolder(NSURL *gameFolderURL);

/* Appends a section -- separator, greyed heading, the first
   QSSServerSectionLimit entries, and a "More" submenu holding every entry
   when there are more than that. Does nothing when entries is empty. Each
   item carries its address as representedObject and sends action to target,
   so one action handles every entry in every section. */
void QSSAddServersSection(NSMenu *menu, NSString *heading, NSArray *entries,
                          id target, SEL action);

/* Rejects names COM_Game_f would refuse ("gamedir should be a single
   directory name, not a path") or that could not be passed safely on a
   command line or through the console. */
BOOL QSSValidModName(NSString *name);

/* Game directory names found beside the game, plus its games/ and mods/
   subfolders -- the same places Modlist_Init looks. Unlike Modlist_Init this
   keeps only directories holding a .pak, progs.dat, or maps/, which drops
   support folders the engine's console listing tolerates but a menu should
   not. Bare names, naturally sorted; COM_ResolveGameDir finds the prefix. */
NSArray *QSSModsForGameFolder(NSURL *gameFolderURL);

/* Appends a single "Mods" item whose submenu lists every mod. Each item
   carries its name as representedObject and sends action to target. */
void QSSAddModsSection(NSMenu *menu, NSArray *mods, id target, SEL action);
