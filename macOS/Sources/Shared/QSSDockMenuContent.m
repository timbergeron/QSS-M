/*
 * QSSDockMenuContent.m -- the Dock menu's contents: server history, bookmark
 * and mod readers plus the section builders that turn them into menu items.
 * Shared by the launcher and the Dock tile plug-in, which build the same menu
 * from different processes.
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

#import "QSSDockMenuContent.h"

NSString * const QSSServerAddressKey = @"address";
NSString * const QSSServerDateKey = @"date";
NSString * const QSSServerAliasKey = @"alias";
NSString * const QSSServerPinnedKey = @"pinned";

static const unsigned long long QSSMaxContentFileSize = 4ULL * 1024ULL * 1024ULL;

static NSData *QSSBoundedDataAtURL(NSURL *url)
{
    NSDictionary *attributes;
    NSNumber *fileSize;
    NSData *data;

    if (!url)
        return nil;
    attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:[url path]
                                                                   error:NULL];
    fileSize = [attributes objectForKey:NSFileSize];
    if (!fileSize || [fileSize unsignedLongLongValue] > QSSMaxContentFileSize)
        return nil;
    data = [NSData dataWithContentsOfURL:url];
    return [data length] <= QSSMaxContentFileSize ? data : nil;
}

static NSString *QSSBoundedStringAtURL(NSURL *url)
{
    NSData *data = QSSBoundedDataAtURL(url);
    return data ? [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]
                   autorelease] : nil;
}

static id QSSBoundedJSONAtURL(NSURL *url)
{
    NSData *data = QSSBoundedDataAtURL(url);
    return data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
}

BOOL QSSValidServerAddress(NSString *server)
{
    if (!server || ![server isKindOfClass:[NSString class]] ||
        [server length] == 0 || [server length] >= 128 ||
        [server caseInsensitiveCompare:@"local"] == NSOrderedSame ||
        [server caseInsensitiveCompare:@"localhost"] == NSOrderedSame ||
        [server hasPrefix:@"+"] || [server hasPrefix:@"-"] ||
        [server rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@";\""]].location != NSNotFound)
        return NO;
    if ([server rangeOfCharacterFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]].location != NSNotFound ||
        [server rangeOfCharacterFromSet:[NSCharacterSet controlCharacterSet]].location != NSNotFound)
        return NO;
    /* Nothing resolvable is punctuation-only. Keeps stray JSON delimiters out
       of the menu while leaving bracketed IPv6 literals alone. */
    if ([server rangeOfCharacterFromSet:[NSCharacterSet alphanumericCharacterSet]].location == NSNotFound)
        return NO;
    return YES;
}

NSString *QSSServerAddress(NSDictionary *entry)
{
    NSString *address = [entry isKindOfClass:[NSDictionary class]] ?
        [entry objectForKey:QSSServerAddressKey] : nil;
    return QSSValidServerAddress(address) ? address : nil;
}

/* Aliases are display-only, but a stray newline or control character would
   still corrupt the menu, so fall back to the address in that case. */
static NSString *QSSValidAlias(id alias)
{
    if (![alias isKindOfClass:[NSString class]] || [alias length] == 0 ||
        [alias length] >= 128)
        return nil;
    if ([alias rangeOfCharacterFromSet:[NSCharacterSet controlCharacterSet]].location != NSNotFound ||
        [alias rangeOfCharacterFromSet:[NSCharacterSet newlineCharacterSet]].location != NSNotFound)
        return nil;
    return alias;
}

/* The engine writes UTC timestamps as "%Y-%m-%dT%H:%M:%SZ". */
static NSDate *QSSParseTimestamp(id value)
{
    static NSISO8601DateFormatter *formatter;

    if (![value isKindOfClass:[NSString class]])
        return nil;
    if (!formatter)
        formatter = [[NSISO8601DateFormatter alloc] init];
    return [formatter dateFromString:(NSString *)value];
}

static NSString *QSSFormatDate(NSDate *date)
{
    static NSDateFormatter *formatter;
    static NSDate *formatterBuilt;
    NSDate *now = [NSDate date];

    /* "Today"/"Yesterday" are relative to when the formatter was built, and
       the Dock keeps this plug-in loaded for days, so rebuild once the day
       rolls over. This also picks up locale changes made since. */
    if (formatter && ![[NSCalendar currentCalendar] isDate:formatterBuilt
                                          inSameDayAsDate:now]) {
        [formatter release];
        formatter = nil;
        [formatterBuilt release];
        formatterBuilt = nil;
    }
    if (!formatter) {
        formatter = [[NSDateFormatter alloc] init];
        [formatter setDateStyle:NSDateFormatterMediumStyle];
        [formatter setTimeStyle:NSDateFormatterNoStyle];
        [formatter setDoesRelativeDateFormatting:YES];
        formatterBuilt = [now retain];
    }
    return [formatter stringFromDate:date];
}

static void QSSAppendEntry(NSMutableArray *servers, NSString *server,
                           NSDate *date, NSString *alias, NSNumber *pinned)
{
    NSMutableDictionary *entry;

    if (!QSSValidServerAddress(server))
        return;
    for (NSDictionary *existing in servers) {
        if ([[existing objectForKey:QSSServerAddressKey]
             caseInsensitiveCompare:server] == NSOrderedSame)
            return;
    }

    entry = [NSMutableDictionary dictionaryWithCapacity:4];
    [entry setObject:server forKey:QSSServerAddressKey];
    if (date)
        [entry setObject:date forKey:QSSServerDateKey];
    if (alias)
        [entry setObject:alias forKey:QSSServerAliasKey];
    if (pinned)
        [entry setObject:pinned forKey:QSSServerPinnedKey];
    [servers addObject:entry];
}

static NSURL *QSSBackupsURL(NSURL *gameFolderURL)
{
    return [[gameFolderURL URLByAppendingPathComponent:@"id1" isDirectory:YES]
            URLByAppendingPathComponent:@"backups" isDirectory:YES];
}

NSArray *QSSRecentServersForGameFolder(NSURL *gameFolderURL)
{
    NSURL *backupsURL;
    id history;
    NSMutableArray *servers = [NSMutableArray array];

    if (!gameFolderURL)
        return servers;
    backupsURL = QSSBackupsURL(gameFolderURL);
    history = QSSBoundedJSONAtURL([backupsURL URLByAppendingPathComponent:@"servers.json"
                                                              isDirectory:NO]);
    if ([history isKindOfClass:[NSArray class]]) {
        for (id value in history) {
            if (![value isKindOfClass:[NSDictionary class]])
                continue;
            QSSAppendEntry(servers, [(NSDictionary *)value objectForKey:@"address"],
                           QSSParseTimestamp([(NSDictionary *)value objectForKey:@"last_connected"]),
                           nil, nil);
        }
    }
    if ([servers count] == 0) {
        /* Not migrated yet: lastserver.txt is the only pre-JSON file that
           records recency, so it is all the Dock can honestly show until the
           engine has run once and built servers.json. */
        NSString *lastServer = [QSSBoundedStringAtURL([backupsURL
            URLByAppendingPathComponent:@"lastserver.txt" isDirectory:NO])
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];

        QSSAppendEntry(servers, lastServer, nil, nil, nil);
    }
    return servers;
}

NSArray *QSSBookmarksForGameFolder(NSURL *gameFolderURL)
{
    id bookmarks;
    NSMutableArray *entries = [NSMutableArray array];
    NSMutableArray *pinned = [NSMutableArray array];
    NSMutableArray *unpinned = [NSMutableArray array];

    if (!gameFolderURL)
        return entries;
    bookmarks = QSSBoundedJSONAtURL([QSSBackupsURL(gameFolderURL)
        URLByAppendingPathComponent:@"bookmarks.json" isDirectory:NO]);
    if (![bookmarks isKindOfClass:[NSArray class]])
        return entries;

    for (id value in bookmarks) {
        id pinnedValue;

        if (![value isKindOfClass:[NSDictionary class]])
            continue;
        pinnedValue = [(NSDictionary *)value objectForKey:@"pinned"];
        QSSAppendEntry(entries, [(NSDictionary *)value objectForKey:@"address"], nil,
                       QSSValidAlias([(NSDictionary *)value objectForKey:@"alias"]),
                       [pinnedValue isKindOfClass:[NSNumber class]] ? pinnedValue : nil);
    }

    /* Split rather than sort: this keeps each group in file order, which a
       comparator sort would not guarantee. */
    for (NSDictionary *entry in entries) {
        [[[entry objectForKey:QSSServerPinnedKey] boolValue] ? pinned : unpinned
         addObject:entry];
    }
    [pinned addObjectsFromArray:unpinned];
    return pinned;
}

/* Bookmarks lead with their alias and explain it with the address; history
   entries lead with the address and qualify it with when it was last used.
   Pinned bookmarks are marked in place of a section of their own. */
static NSString *QSSEntryPrimaryLabel(NSDictionary *entry)
{
    NSString *alias = [entry objectForKey:QSSServerAliasKey];
    NSString *name = [alias length] ? alias : QSSServerAddress(entry);

    if ([[entry objectForKey:QSSServerPinnedKey] boolValue])
        return [NSString stringWithFormat:@"%@ (Pin)", name];
    return name;
}

static NSString *QSSEntryDetailLabel(NSDictionary *entry)
{
    NSDate *date = [entry objectForKey:QSSServerDateKey];

    if (date)
        return QSSFormatDate(date);
    if ([[entry objectForKey:QSSServerAliasKey] length])
        return QSSServerAddress(entry);
    return nil;
}

BOOL QSSValidModName(NSString *name)
{
    if (!name || ![name isKindOfClass:[NSString class]] ||
        [name length] == 0 || [name length] >= 64 ||
        [name isEqualToString:@"."] ||
        [name rangeOfString:@".."].location != NSNotFound ||
        [name hasPrefix:@"-"] || [name hasPrefix:@"+"])
        return NO;
    /* COM_Game_f takes a directory name, never a path, and the name also has
       to survive the console tokenizer when switching a running game. */
    if ([name rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"/\\:;\"'"]].location != NSNotFound)
        return NO;
    if ([name rangeOfCharacterFromSet:[NSCharacterSet controlCharacterSet]].location != NSNotFound ||
        [name rangeOfCharacterFromSet:[NSCharacterSet newlineCharacterSet]].location != NSNotFound)
        return NO;
    if ([name rangeOfCharacterFromSet:[NSCharacterSet alphanumericCharacterSet]].location == NSNotFound)
        return NO;
    return YES;
}

/* A game directory is one the engine could actually load content from. */
static BOOL QSSDirectoryIsMod(NSURL *dirURL)
{
    NSArray *contents = [[NSFileManager defaultManager]
        contentsOfDirectoryAtURL:dirURL
      includingPropertiesForKeys:@[ NSURLIsDirectoryKey ]
                         options:0
                           error:NULL];

    for (NSURL *item in contents) {
        NSString *name = [item lastPathComponent];
        NSNumber *isDirectory = nil;

        if ([[name pathExtension] caseInsensitiveCompare:@"pak"] == NSOrderedSame ||
            [name caseInsensitiveCompare:@"progs.dat"] == NSOrderedSame)
            return YES;
        if ([name caseInsensitiveCompare:@"maps"] == NSOrderedSame &&
            [item getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:NULL] &&
            [isDirectory boolValue])
            return YES;
    }
    return NO;
}

static void QSSAddModsInPath(NSMutableArray *mods, NSURL *pathURL)
{
    NSArray *contents = [[NSFileManager defaultManager]
        contentsOfDirectoryAtURL:pathURL
      includingPropertiesForKeys:@[ NSURLIsDirectoryKey ]
                         options:NSDirectoryEnumerationSkipsHiddenFiles
                           error:NULL];

    for (NSURL *item in contents) {
        NSString *name = [item lastPathComponent];
        NSNumber *isDirectory = nil;
        BOOL duplicate = NO;

        if (![item getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:NULL] ||
            ![isDirectory boolValue])
            continue;
        if ([[name pathExtension] caseInsensitiveCompare:@"app"] == NSOrderedSame ||
            !QSSValidModName(name))
            continue;
        for (NSString *existing in mods) {
            if ([existing caseInsensitiveCompare:name] == NSOrderedSame) {
                duplicate = YES;
                break;
            }
        }
        if (duplicate || !QSSDirectoryIsMod(item))
            continue;
        [mods addObject:name];
    }
}

NSArray *QSSModsForGameFolder(NSURL *gameFolderURL)
{
    NSMutableArray *mods = [NSMutableArray array];

    if (!gameFolderURL)
        return mods;
    QSSAddModsInPath(mods, gameFolderURL);
    QSSAddModsInPath(mods, [gameFolderURL URLByAppendingPathComponent:@"games"
                                                          isDirectory:YES]);
    QSSAddModsInPath(mods, [gameFolderURL URLByAppendingPathComponent:@"mods"
                                                          isDirectory:YES]);
    return [mods sortedArrayUsingSelector:@selector(localizedStandardCompare:)];
}

void QSSAddModsSection(NSMenu *menu, NSArray *mods, id target, SEL action)
{
    NSMenu *submenu;
    NSMenuItem *modsItem;

    if (!menu || [mods count] == 0)
        return;

    submenu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    [submenu setAutoenablesItems:NO];
    for (NSString *mod in mods) {
        NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:mod
                                                       action:action
                                                keyEquivalent:@""] autorelease];

        [item setTarget:target];
        [item setRepresentedObject:mod];
        [submenu addItem:item];
    }

    [menu addItem:[NSMenuItem separatorItem]];
    modsItem = [[[NSMenuItem alloc] initWithTitle:@"Mods"
                                           action:NULL
                                    keyEquivalent:@""] autorelease];
    [modsItem setSubmenu:submenu];
    [menu addItem:modsItem];
}

static NSMenuItem *QSSConnectItem(NSDictionary *entry, NSString *title, id target, SEL action)
{
    NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:title
                                                   action:action
                                            keyEquivalent:@""] autorelease];

    [item setTarget:target];
    /* Carrying the address on the item keeps the click bound to the entry the
       title named, even if the file changed since the menu was built. */
    [item setRepresentedObject:QSSServerAddress(entry)];
    return item;
}

static NSMenu *QSSFullListSubmenu(NSArray *entries, id target, SEL action)
{
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];

    [menu setAutoenablesItems:NO];
    for (NSDictionary *entry in entries) {
        NSString *primary = QSSEntryPrimaryLabel(entry);
        NSString *detail = QSSEntryDetailLabel(entry);
        NSString *title = detail ?
            [NSString stringWithFormat:@"%@ — %@", primary, detail] : primary;

        [menu addItem:QSSConnectItem(entry, title, target, action)];
    }
    return menu;
}

void QSSAddServersSection(NSMenu *menu, NSString *heading, NSArray *entries,
                          id target, SEL action)
{
    NSMenuItem *headingItem;
    NSUInteger shown;
    NSUInteger i;

    if (!menu || [entries count] == 0)
        return;

    [menu addItem:[NSMenuItem separatorItem]];
    headingItem = [[[NSMenuItem alloc] initWithTitle:heading
                                              action:NULL
                                       keyEquivalent:@""] autorelease];
    [headingItem setEnabled:NO];
    [menu addItem:headingItem];

    shown = MIN((NSUInteger)QSSServerSectionLimit, [entries count]);
    for (i = 0; i < shown; i++) {
        NSDictionary *entry = [entries objectAtIndex:i];
        NSString *title = [NSString stringWithFormat:@"Connect to %@",
                           QSSEntryPrimaryLabel(entry)];

        [menu addItem:QSSConnectItem(entry, title, target, action)];
    }

    if ([entries count] > shown) {
        NSMenuItem *moreItem = [[[NSMenuItem alloc] initWithTitle:@"More"
                                                           action:NULL
                                                    keyEquivalent:@""] autorelease];

        [moreItem setSubmenu:QSSFullListSubmenu(entries, target, action)];
        [menu addItem:moreItem];
    }
}
