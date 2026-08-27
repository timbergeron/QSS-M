/*
Copyright (C) 2007-2008 Kristian Duske

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
#import "AppController.h"
#import "QSSDockMenuContent.h"
#import <ApplicationServices/ApplicationServices.h>
#import <IOKit/hid/IOHIDLib.h>
#import <IOKit/hid/IOHIDUsageTables.h>
#import <IOKit/hidsystem/IOHIDLib.h>
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#import <SDL2/SDL.h>
#else
#import <SDL/SDL.h>
#endif
#else
#import "SDL.h"
#endif
#import "SDLMain.h"

NSString *FQPrefCommandLineKey = @"CommandLine";
static NSString *QSSPrefRawMouseInputEnabledKey = @"RawMouseInputEnabled";
static NSString *QSSPrefWarnBeforeQuittingKey = @"WarnBeforeQuitting";
static const NSTimeInterval QSSCommandQHoldDuration = 1.0;
static const NSTimeInterval QSSQuitHoldCancelledDisplayDuration = 1.0;
static const NSTimeInterval QSSQuitHoldCompletionDisplayDuration = 0.08;
/* Half-size point measurements from the supplied 2x Retina reference. */
static const CGFloat QSSQuitHoldOverlayWidth = 356.0f;
static const CGFloat QSSQuitHoldOverlayHeight = 72.0f;
static const CGFloat QSSQuitHoldCornerRadius = 12.0f;

@interface QSSQuitHoldOverlayView : NSView
{
    CGFloat progress;
}
- (void)setProgress:(CGFloat)value;
@end

@implementation QSSQuitHoldOverlayView

- (BOOL)isOpaque
{
    return NO;
}

- (void)setProgress:(CGFloat)value
{
    progress = MAX(0.0f, MIN(1.0f, value));
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    NSRect bounds = [self bounds];
    NSBezierPath *background;
    NSBezierPath *borderPath;
    NSString *text = @"Hold \u2318Q to Quit";
    NSDictionary *attributes;
    NSSize textSize;
    NSPoint textOrigin;
    NSRect borderBounds;
    CGFloat radius;
    CGFloat perimeter;
    CGFloat dashPattern[2];
    CGFloat progressAlpha;
    CGFloat glowAlpha;
    NSShadow *progressShadow;

    (void)dirtyRect;
    background = [NSBezierPath bezierPathWithRoundedRect:bounds
                                                 xRadius:QSSQuitHoldCornerRadius
                                                 yRadius:QSSQuitHoldCornerRadius];
    [[NSColor colorWithCalibratedWhite:0.16f alpha:0.75f] setFill];
    [background fill];

    /* The outline is a clockwise progress indicator for the hold duration. */
    borderBounds = NSInsetRect(bounds, 1.5f, 1.5f);
    radius = QSSQuitHoldCornerRadius - 1.5f;
    perimeter = 2.0f * (borderBounds.size.width - 2.0f * radius) +
                2.0f * (borderBounds.size.height - 2.0f * radius) +
                2.0f * (CGFloat)M_PI * radius;
    borderPath = [NSBezierPath bezierPath];
    [borderPath moveToPoint:NSMakePoint(NSMidX(borderBounds), NSMaxY(borderBounds))];
    [borderPath lineToPoint:NSMakePoint(NSMaxX(borderBounds) - radius, NSMaxY(borderBounds))];
    [borderPath appendBezierPathWithArcWithCenter:NSMakePoint(NSMaxX(borderBounds) - radius,
                                                               NSMaxY(borderBounds) - radius)
                                           radius:radius startAngle:90.0f endAngle:0.0f clockwise:YES];
    [borderPath lineToPoint:NSMakePoint(NSMaxX(borderBounds), NSMinY(borderBounds) + radius)];
    [borderPath appendBezierPathWithArcWithCenter:NSMakePoint(NSMaxX(borderBounds) - radius,
                                                               NSMinY(borderBounds) + radius)
                                           radius:radius startAngle:0.0f endAngle:-90.0f clockwise:YES];
    [borderPath lineToPoint:NSMakePoint(NSMinX(borderBounds) + radius, NSMinY(borderBounds))];
    [borderPath appendBezierPathWithArcWithCenter:NSMakePoint(NSMinX(borderBounds) + radius,
                                                               NSMinY(borderBounds) + radius)
                                           radius:radius startAngle:-90.0f endAngle:-180.0f clockwise:YES];
    [borderPath lineToPoint:NSMakePoint(NSMinX(borderBounds), NSMaxY(borderBounds) - radius)];
    [borderPath appendBezierPathWithArcWithCenter:NSMakePoint(NSMinX(borderBounds) + radius,
                                                               NSMaxY(borderBounds) - radius)
                                           radius:radius startAngle:180.0f endAngle:90.0f clockwise:YES];
    [borderPath lineToPoint:NSMakePoint(NSMidX(borderBounds), NSMaxY(borderBounds))];
    [borderPath setLineWidth:1.0f];
    [borderPath setLineCapStyle:NSLineCapStyleRound];
    [[NSColor colorWithCalibratedWhite:1.0f alpha:0.16f] setStroke];
    [borderPath stroke];

    if (progress > 0.0f) {
        progressAlpha = progress >= 1.0f ? 1.0f : 0.66f;
        glowAlpha = progress >= 1.0f ? 0.42f : 0.21f;
        dashPattern[0] = MAX(0.01f, perimeter * progress);
        dashPattern[1] = perimeter;
        [borderPath setLineDash:dashPattern count:2 phase:0.0f];
        progressShadow = [[[NSShadow alloc] init] autorelease];
        [progressShadow setShadowOffset:NSZeroSize];
        [progressShadow setShadowBlurRadius:2.5f];
        [progressShadow setShadowColor:[NSColor colorWithCalibratedWhite:1.0f alpha:0.22f]];
        [NSGraphicsContext saveGraphicsState];
        [progressShadow set];
        [borderPath setLineWidth:3.0f];
        [[NSColor colorWithCalibratedWhite:1.0f alpha:glowAlpha] setStroke];
        [borderPath stroke];
        [NSGraphicsContext restoreGraphicsState];
        [borderPath setLineWidth:2.0f];
        [[NSColor colorWithCalibratedWhite:1.0f alpha:progressAlpha] setStroke];
        [borderPath stroke];
    }

    attributes = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSFont boldSystemFontOfSize:24.0f], NSFontAttributeName,
        [NSColor whiteColor], NSForegroundColorAttributeName,
        nil];
    textSize = [text sizeWithAttributes:attributes];
    textOrigin = NSMakePoint(NSMidX(bounds) - textSize.width * 0.5f,
                             NSMidY(bounds) - textSize.height * 0.5f);
    [text drawAtPoint:textOrigin withAttributes:attributes];
}

@end

#ifndef MAC_OS_X_VERSION_10_13
#define NSControlStateValueOff NSOffState
#define NSControlStateValueOn NSOnState
#endif

typedef struct {
    CGRect frame;
    CGRect visibleFrame;
    BOOL valid;
} QSSSystemSettingsWindowSnapshot;

static const CGFloat QSSRawMouseOverlayWidth = 530.0f;
static const CGFloat QSSRawMouseOverlayHeight = 109.0f;
static const CGFloat QSSLauncherWindowWidth = 580.0f;
static const CGFloat QSSLauncherWindowHeight = 460.0f;
static const CGFloat QSSLauncherCardInset = 24.0f;
static const CGFloat QSSChipHeight = 24.0f;
static const CGFloat QSSLaunchOptionsBaseHeight = 112.0f;
static const CGFloat QSSSettingsCardHeight = 102.0f;
static const CGFloat QSSRawMouseSwitchRightInsetExtra = 8.0f;
static const CGFloat QSSShortcutTableRightGutter = 18.0f;

extern void Cbuf_AddText (const char *text); /* engine command buffer (cmd.c) */

static NSURL *QSSGameFolderURL(void)
{
    NSURL *bundleURL = [[NSBundle mainBundle] bundleURL];
    return [bundleURL URLByDeletingLastPathComponent];
}

static void QSSLaunchNewInstance(void)
{
    NSURL *applicationURL = [[NSBundle mainBundle] bundleURL];
    NSError *error = nil;

    if (!applicationURL)
        return;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    /* NSWorkspaceOpenConfiguration needs macOS 10.15; QSS-M supports 10.13. */
    if (![[NSWorkspace sharedWorkspace] launchApplicationAtURL:applicationURL
                                                       options:(NSWorkspaceLaunchDefault | NSWorkspaceLaunchNewInstance)
                                                 configuration:@{
                                                     NSWorkspaceLaunchConfigurationArguments : @[ @"-nolauncher" ]
                                                 }
                                                         error:&error])
        NSLog(@"QSS-M: unable to launch new instance: %@", error);
#pragma clang diagnostic pop
}

typedef struct {
    NSString *title;
    NSString *insert;
} QSSPresetArgumentEntry;

static QSSPresetArgumentEntry QSSPresetArgumentEntries[] = {
    { @"-condebug (log console to file)", @"-condebug" },
    { @"-safe (disable optional subsystems)", @"-safe" },
    { @"-nohome (ignore ~/.quakespasm)", @"-nohome" },
    { @"-fitz (FitzQuake emulation)", @"-fitz" },
    { @"-qmenu (classic menu)", @"-qmenu" },
    { nil, nil },
    { @"-game <gamedir>", @"-game " },
    { @"-basegame <gamedir>", @"-basegame " },
    { @"-basedir <path>", @"-basedir " },
    { @"-rogue (Dissolution of Eternity)", @"-rogue" },
    { @"-hipnotic (Scourge of Armagon)", @"-hipnotic" },
    { @"-quoth", @"-quoth" },
    { @"-nowildpaks (disable wildcard pak loading)", @"-nowildpaks" },
    { nil, nil },
    { @"-heapsize <kb> (engine heap)", @"-heapsize " },
    { @"-mem <mb> (engine heap)", @"-mem " },
    { @"-zone <kb>", @"-zone " },
    { @"-minmemory", @"-minmemory" },
    { nil, nil },
    { @"-width <pixels>", @"-width " },
    { @"-height <pixels>", @"-height " },
    { @"-bpp <bits>", @"-bpp " },
    { @"-refreshrate <hz>", @"-refreshrate " },
    { @"-fsaa <samples>", @"-fsaa " },
    { @"-window (windowed)", @"-window" },
    { @"-w (windowed alias)", @"-w" },
    { @"-fullscreen", @"-fullscreen" },
    { @"-f (fullscreen alias)", @"-f" },
    { @"-novbo (disable VBOs)", @"-novbo" },
    { @"-nomtex (disable multitexture)", @"-nomtex" },
    { @"-nocombine (disable combine)", @"-nocombine" },
    { @"-noadd (disable additive blends)", @"-noadd" },
    { @"-notexturenpot (no NPOT textures)", @"-notexturenpot" },
    { @"-noglsl (disable GLSL)", @"-noglsl" },
    { @"-noglslgamma (disable GLSL gamma)", @"-noglslgamma" },
    { @"-noglslalias (disable GLSL alias)", @"-noglslalias" },
    { @"-nopackedpixels", @"-nopackedpixels" },
    { @"-nowarpmipmaps", @"-nowarpmipmaps" },
    { @"-current (use current display mode)", @"-current" },
    { @"-particles <count>", @"-particles " },
    { nil, nil },
    { @"-nosound (disable sound)", @"-nosound" },
    { @"-noextmusic (disable external music)", @"-noextmusic" },
    { @"-sndspeed <hz>", @"-sndspeed " },
    { @"-mixspeed <hz>", @"-mixspeed " },
    { @"-nocdaudio (disable CD audio)", @"-nocdaudio" },
    { nil, nil },
    { @"-nojoy (disable joystick)", @"-nojoy" },
    { @"-nomouse (disable mouse)", @"-nomouse" },
    { nil, nil },
    { @"-dedicated (dedicated server)", @"-dedicated" },
    { @"-listen (listen server)", @"-listen" },
    { @"-protocol <15|666|999>", @"-protocol " },
    { @"-port <port>", @"-port " },
    { @"-udpport <port>", @"-udpport " },
    { @"-ip <address>", @"-ip " },
    { @"-ip6 <address>", @"-ip6 " },
    { @"-nolan (disable LAN broadcast)", @"-nolan" },
    { @"-noudp (disable UDP)", @"-noudp" },
    { @"-noudp4 (disable IPv4 UDP)", @"-noudp4" },
    { @"-noudp6 (disable IPv6 UDP)", @"-noudp6" },
    { @"-useice (enable ICE networking)", @"-useice" },
    { @"-noice (disable ICE networking)", @"-noice" },
    { @"-privkey <file>", @"-privkey " },
    { @"-pubkey <file>", @"-pubkey " },
    { @"-certhost <host>", @"-certhost " },
    { nil, nil },
    { @"-consize <kb> (console buffer)", @"-consize " },
    { nil, nil },
    { @"-cddev <device>", @"-cddev " },
    { nil, nil },
    { @"+map <mapname>", @"+map " },
    { @"+skill <0-3>", @"+skill " },
    { @"+name <playername>", @"+name " },
    { @"+connect <address>", @"+connect " },
    { @"+exec <cfg>", @"+exec " },
    { @"+developer 1", @"+developer 1" },
};

static NSString * const QSSShortcutRowHeaderKey = @"header";
static NSString * const QSSShortcutRowTitleKey = @"title";
static NSString * const QSSShortcutRowActionKey = @"action";
static NSString * const QSSShortcutRowKeysKey = @"keys";
static NSString * const QSSShortcutRowSearchKey = @"search";
static NSString * const QSSShortcutActionColumnIdentifier = @"action";
static NSString * const QSSShortcutKeysColumnIdentifier = @"keys";

static NSDictionary *QSSKeyboardShortcutHeader(NSString *title)
{
    return @{
        QSSShortcutRowHeaderKey: [NSNumber numberWithBool:YES],
        QSSShortcutRowTitleKey: title,
        QSSShortcutRowSearchKey: [title lowercaseString]
    };
}

static NSDictionary *QSSKeyboardShortcutItem(NSString *category, NSString *action, NSString *keys)
{
    NSString *search = [[NSString stringWithFormat:@"%@ %@ %@", category, action, keys] lowercaseString];

    return @{
        QSSShortcutRowHeaderKey: [NSNumber numberWithBool:NO],
        QSSShortcutRowActionKey: action,
        QSSShortcutRowKeysKey: keys,
        QSSShortcutRowSearchKey: search
    };
}

static NSString *QSSLaunchOptionsDefaultHelperText(void)
{
    return @"Type a switch, then Tab to complete. Blank launches normally.";
}

static NSColor *QSSResolvedSystemColor(NSColor *color, NSWindow *window, NSColor *fallback)
{
    NSColor *resolved;

    if (!color)
        return fallback;

    if (@available(macOS 10.14, *)) {
        NSAppearance *appearance = window ? [window effectiveAppearance] : [NSAppearance currentAppearance];
        if (!appearance)
            appearance = [NSApp appearance];
        if (appearance) {
            __block NSColor *resolvedInAppearance = nil;
            if (@available(macOS 11.0, *)) {
                [appearance performAsCurrentDrawingAppearance:^{
                    resolvedInAppearance = [[color colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]] retain];
                }];
            } else {
                NSAppearance *previousAppearance = [NSAppearance currentAppearance];
                [NSAppearance setCurrentAppearance:appearance];
                resolvedInAppearance = [[color colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]] retain];
                [NSAppearance setCurrentAppearance:previousAppearance];
            }
            if (resolvedInAppearance)
                return [resolvedInAppearance autorelease];
        }
    }

    resolved = [color colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    return resolved ? resolved : fallback;
}

static NSColor *QSSSystemColorWithAlpha(NSColor *color, NSWindow *window, NSColor *fallback, CGFloat alpha)
{
    NSColor *resolved = QSSResolvedSystemColor(color, window, fallback);
    return [resolved colorWithAlphaComponent:alpha];
}

static BOOL QSSWindowUsesDarkAppearance(NSWindow *window)
{
    if (@available(macOS 10.14, *)) {
        NSAppearance *appearance = window ? [window effectiveAppearance] : [NSAppearance currentAppearance];
        NSString *match;

        if (!appearance)
            appearance = [NSApp appearance];
        if (!appearance)
            return NO;

        match = [appearance bestMatchFromAppearancesWithNames:
                 @[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
        return [match isEqualToString:NSAppearanceNameDarkAqua];
    }

    return NO;
}

static NSColor *QSSSeparatorColorForWindow(NSWindow *window)
{
    if (QSSWindowUsesDarkAppearance(window))
        return [NSColor colorWithCalibratedWhite:0.70f alpha:0.12f];

    return [NSColor colorWithCalibratedWhite:0.0f alpha:0.14f];
}

static NSColor *QSSLauncherWindowBackgroundColorForWindow(NSWindow *window)
{
    return QSSSystemColorWithAlpha([NSColor windowBackgroundColor], window,
        [NSColor colorWithCalibratedWhite:0.94f alpha:1.0f], 0.98f);
}

static NSColor *QSSLauncherSurfaceColorForWindow(NSWindow *window, CGFloat alpha)
{
    return QSSSystemColorWithAlpha([NSColor controlBackgroundColor], window,
        [NSColor colorWithCalibratedWhite:1.0f alpha:1.0f], alpha);
}

static NSColor *QSSLauncherTextInputColorForWindow(NSWindow *window, CGFloat alpha)
{
    return QSSSystemColorWithAlpha([NSColor textBackgroundColor], window,
        [NSColor colorWithCalibratedWhite:1.0f alpha:1.0f], alpha);
}

static NSColor *QSSAboutLinkColor(void)
{
    return [NSColor colorWithSRGBRed:139.0f/255.0f green:95.0f/255.0f blue:71.0f/255.0f alpha:1.0f];
}

static NSColor *QSSShortcutPillFillColorForWindow(NSWindow *window)
{
    if (QSSWindowUsesDarkAppearance(window))
        return [NSColor colorWithCalibratedWhite:1.0f alpha:0.11f];

    return [NSColor colorWithCalibratedWhite:0.0f alpha:0.08f];
}

static NSColor *QSSShortcutPillTextColorForWindow(NSWindow *window)
{
    return QSSSystemColorWithAlpha([NSColor labelColor], window,
        [NSColor colorWithCalibratedWhite:0.16f alpha:1.0f], 0.82f);
}

static NSString *QSSTrimmedShortcutToken(NSString *token)
{
    return [token stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

static NSString *QSSShortcutCapitalizedWords(NSString *string)
{
    NSMutableString *capitalized;
    NSCharacterSet *letters = [NSCharacterSet letterCharacterSet];
    BOOL capitalizeNext = YES;

    if (!string)
        return @"";

    capitalized = [NSMutableString stringWithCapacity:[string length]];
    for (NSUInteger i = 0; i < [string length]; i++) {
        unichar c = [string characterAtIndex:i];
        NSString *character = [NSString stringWithCharacters:&c length:1];

        if ([letters characterIsMember:c]) {
            [capitalized appendString:capitalizeNext ? [character uppercaseString] : character];
            capitalizeNext = NO;
        } else {
            [capitalized appendString:character];
            capitalizeNext = YES;
        }
    }

    return capitalized;
}

static NSString *QSSDisplayShortcutToken(NSString *token)
{
    NSString *trimmed = QSSTrimmedShortcutToken(token);
    NSArray *parts = [trimmed componentsSeparatedByString:@"+"];
    NSMutableArray *keyParts = [NSMutableArray array];
    BOOL hasCommand = NO;
    BOOL hasControl = NO;
    BOOL hasOption = NO;
    BOOL hasShift = NO;

    if ([trimmed caseInsensitiveCompare:@"Alt/Option"] == NSOrderedSame)
        return @"⌥";

    for (NSString *rawPart in parts) {
        NSString *part = QSSTrimmedShortcutToken(rawPart);

        if ([part caseInsensitiveCompare:@"Cmd"] == NSOrderedSame ||
            [part caseInsensitiveCompare:@"Command"] == NSOrderedSame) {
            hasCommand = YES;
        } else if ([part caseInsensitiveCompare:@"Ctrl"] == NSOrderedSame ||
                   [part caseInsensitiveCompare:@"Control"] == NSOrderedSame) {
            hasControl = YES;
        } else if ([part caseInsensitiveCompare:@"Option"] == NSOrderedSame ||
                   [part caseInsensitiveCompare:@"Alt"] == NSOrderedSame) {
            hasOption = YES;
        } else if ([part caseInsensitiveCompare:@"Shift"] == NSOrderedSame) {
            hasShift = YES;
        } else if ([part length] > 0) {
            [keyParts addObject:part];
        }
    }

    if (!hasCommand && !hasControl && !hasOption && !hasShift)
        return trimmed;

    NSMutableString *display = [NSMutableString string];
    if (hasControl)
        [display appendString:@"⌃"];
    if (hasOption)
        [display appendString:@"⌥"];
    if (hasShift)
        [display appendString:@"⇧"];
    if (hasCommand)
        [display appendString:@"⌘"];
    [display appendString:[keyParts componentsJoinedByString:@"+"]];
    return display;
}

static NSString *QSSShortcutDedupeSignature(NSString *token, BOOL *commandOnly, BOOL *controlOnly)
{
    NSString *trimmed = QSSTrimmedShortcutToken(token);
    NSArray *parts = [trimmed componentsSeparatedByString:@"+"];
    NSMutableArray *signatureParts = [NSMutableArray array];
    NSMutableArray *keyParts = [NSMutableArray array];
    BOOL hasCommand = NO;
    BOOL hasControl = NO;
    BOOL hasOption = NO;
    BOOL hasShift = NO;

    for (NSString *rawPart in parts) {
        NSString *part = QSSTrimmedShortcutToken(rawPart);

        if ([part caseInsensitiveCompare:@"Cmd"] == NSOrderedSame ||
            [part caseInsensitiveCompare:@"Command"] == NSOrderedSame) {
            hasCommand = YES;
        } else if ([part caseInsensitiveCompare:@"Ctrl"] == NSOrderedSame ||
                   [part caseInsensitiveCompare:@"Control"] == NSOrderedSame) {
            hasControl = YES;
        } else if ([part caseInsensitiveCompare:@"Option"] == NSOrderedSame ||
                   [part caseInsensitiveCompare:@"Alt"] == NSOrderedSame) {
            hasOption = YES;
        } else if ([part caseInsensitiveCompare:@"Shift"] == NSOrderedSame) {
            hasShift = YES;
        } else if ([part length] > 0) {
            [keyParts addObject:[part lowercaseString]];
        }
    }

    if (commandOnly)
        *commandOnly = hasCommand && !hasControl;
    if (controlOnly)
        *controlOnly = hasControl && !hasCommand;

    if (!hasCommand && !hasControl)
        return nil;

    if (hasOption)
        [signatureParts addObject:@"option"];
    if (hasShift)
        [signatureParts addObject:@"shift"];
    [signatureParts addObjectsFromArray:keyParts];
    return [signatureParts componentsJoinedByString:@"+"];
}

static NSArray *QSSDisplayShortcutTokensForKeys(NSString *keys)
{
    NSArray *parts = [keys componentsSeparatedByString:@" / "];
    NSMutableArray *tokens = [NSMutableArray array];
    NSMutableSet *commandSignatures = [NSMutableSet set];

    for (NSString *part in parts) {
        BOOL commandOnly = NO;
        NSString *signature = QSSShortcutDedupeSignature(part, &commandOnly, NULL);

        if (commandOnly && [signature length] > 0)
            [commandSignatures addObject:signature];
    }

    for (NSString *part in parts) {
        BOOL controlOnly = NO;
        NSString *signature = QSSShortcutDedupeSignature(part, NULL, &controlOnly);
        NSString *display;

        if (controlOnly && [signature length] > 0 && [commandSignatures containsObject:signature])
            continue;

        display = QSSDisplayShortcutToken(part);
        if ([display length] > 0)
            [tokens addObject:display];
    }

    return tokens;
}

static BOOL QSSSupportsInputMonitoring(void)
{
    if (@available(macOS 10.15, *))
        return YES;
    return NO;
}

static IOHIDAccessType QSSInputMonitoringAccessType(void)
{
    if (@available(macOS 10.15, *))
        return IOHIDCheckAccess(kIOHIDRequestTypeListenEvent);
    return kIOHIDAccessTypeGranted;
}

static BOOL QSSCanOpenHIDMouseInput(void)
{
    IOHIDManagerRef manager = NULL;
    CFMutableDictionaryRef mice = NULL;
    CFNumberRef pageRef = NULL;
    CFNumberRef usageRef = NULL;
    UInt32 page = kHIDPage_GenericDesktop;
    UInt32 usage = kHIDUsage_GD_Mouse;
    IOReturn ret;
    BOOL granted = NO;

    if (!QSSSupportsInputMonitoring())
        return YES;

    manager = IOHIDManagerCreate(kCFAllocatorSystemDefault, kIOHIDOptionsTypeNone);
    if (!manager)
        goto cleanup;

    mice = CFDictionaryCreateMutable(kCFAllocatorSystemDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!mice)
        goto cleanup;

    pageRef = CFNumberCreate(kCFAllocatorSystemDefault, kCFNumberIntType, &page);
    usageRef = CFNumberCreate(kCFAllocatorSystemDefault, kCFNumberIntType, &usage);
    if (!pageRef || !usageRef)
        goto cleanup;

    CFDictionarySetValue(mice, CFSTR(kIOHIDDeviceUsagePageKey), pageRef);
    CFDictionarySetValue(mice, CFSTR(kIOHIDDeviceUsageKey), usageRef);
    IOHIDManagerSetDeviceMatching(manager, mice);

    ret = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    granted = (ret == kIOReturnSuccess);
    if (granted)
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);

cleanup:
    if (pageRef)
        CFRelease(pageRef);
    if (usageRef)
        CFRelease(usageRef);
    if (mice)
        CFRelease(mice);
    if (manager)
        CFRelease(manager);

    return granted;
}

static BOOL QSSInputMonitoringIsGranted(void)
{
    if (!QSSSupportsInputMonitoring())
        return YES;

    if (QSSInputMonitoringAccessType() == kIOHIDAccessTypeGranted)
        return YES;

    if (@available(macOS 10.15, *))
    {
        if (CGPreflightListenEventAccess())
            return YES;
    }

    if (QSSCanOpenHIDMouseInput())
        return YES;

    return NO;
}

static NSURL *QSSInputMonitoringSettingsURL(void)
{
    NSString *settingsPath;

    if (@available(macOS 13.0, *))
        settingsPath = @"x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_ListenEvent";
    else
        settingsPath = @"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent";

    return [NSURL URLWithString:settingsPath];
}

static BOOL QSSOpenInputMonitoringSettings(void)
{
    NSURL *settingsURL = QSSInputMonitoringSettingsURL();

    if (settingsURL && [[NSWorkspace sharedWorkspace] openURL:settingsURL])
        return YES;

    settingsURL = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent"];
    if (settingsURL && [[NSWorkspace sharedWorkspace] openURL:settingsURL])
        return YES;

    return NO;
}

static BOOL QSSIsSystemSettingsOwnerName(NSString *ownerName)
{
    return [ownerName isEqualToString:@"System Settings"] ||
        [ownerName isEqualToString:@"System Preferences"];
}

static BOOL QSSFrontmostAppIsSystemSettings(void)
{
    NSRunningApplication *frontmostApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
    NSString *bundleIdentifier = [frontmostApp bundleIdentifier];
    NSString *localizedName = [frontmostApp localizedName];

    if ([bundleIdentifier isEqualToString:@"com.apple.systempreferences"] ||
        [bundleIdentifier isEqualToString:@"com.apple.SystemSettings"])
        return YES;

    return QSSIsSystemSettingsOwnerName(localizedName);
}

static BOOL QSSConvertWindowFrameToAppKit(CGRect cgFrame, CGRect *frame, CGRect *visibleFrame)
{
    NSArray *screens = [NSScreen screens];
    NSScreen *matchedScreen = nil;
    CGRect matchedBounds = CGRectZero;
    CGFloat bestArea = 0.0f;

    for (NSScreen *screen in screens)
    {
        NSNumber *screenNumber = [[screen deviceDescription] objectForKey:@"NSScreenNumber"];
        if (!screenNumber)
            continue;

        CGDirectDisplayID displayID = (CGDirectDisplayID)[screenNumber unsignedIntValue];
        CGRect displayBounds = CGDisplayBounds(displayID);
        CGRect intersection = CGRectIntersection(displayBounds, cgFrame);
        CGFloat area;

        if (CGRectIsNull(intersection) || CGRectIsEmpty(intersection))
            continue;

        area = CGRectGetWidth(intersection) * CGRectGetHeight(intersection);
        if (area > bestArea)
        {
            bestArea = area;
            matchedScreen = screen;
            matchedBounds = displayBounds;
        }
    }

    if (!matchedScreen)
    {
        if (frame)
            *frame = cgFrame;
        if (visibleFrame)
            *visibleFrame = [[NSScreen mainScreen] visibleFrame];
        return YES;
    }

    if (frame)
    {
        CGFloat localX = CGRectGetMinX(cgFrame) - CGRectGetMinX(matchedBounds);
        CGFloat localY = CGRectGetMinY(cgFrame) - CGRectGetMinY(matchedBounds);
        NSRect screenFrame = [matchedScreen frame];

        *frame = CGRectMake(NSMinX(screenFrame) + localX,
            NSMaxY(screenFrame) - localY - CGRectGetHeight(cgFrame),
            CGRectGetWidth(cgFrame), CGRectGetHeight(cgFrame));
    }

    if (visibleFrame)
        *visibleFrame = [matchedScreen visibleFrame];

    return YES;
}

static BOOL QSSCopySystemSettingsWindowSnapshot(QSSSystemSettingsWindowSnapshot *snapshot)
{
    CFArrayRef windowInfoRef;
    NSArray *windowInfo;
    NSDictionary *info;
    CGFloat bestArea = 0.0f;

    if (!snapshot || !QSSFrontmostAppIsSystemSettings())
        return NO;

    snapshot->valid = NO;
    windowInfoRef = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!windowInfoRef)
        return NO;

    windowInfo = (NSArray *)windowInfoRef;
    for (info in windowInfo)
    {
        NSString *ownerName = [info objectForKey:(id)kCGWindowOwnerName];
        NSNumber *layer = [info objectForKey:(id)kCGWindowLayer];
        NSDictionary *bounds = [info objectForKey:(id)kCGWindowBounds];
        CGRect cgFrame;
        CGRect frame;
        CGRect visibleFrame;
        CGFloat area;

        if (!QSSIsSystemSettingsOwnerName(ownerName))
            continue;
        if ([layer intValue] != 0)
            continue;
        if (![bounds isKindOfClass:[NSDictionary class]])
            continue;
        if (!CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)bounds, &cgFrame))
            continue;
        if (CGRectGetWidth(cgFrame) <= 320.0f || CGRectGetHeight(cgFrame) <= 240.0f)
            continue;

        QSSConvertWindowFrameToAppKit(cgFrame, &frame, &visibleFrame);
        area = CGRectGetWidth(frame) * CGRectGetHeight(frame);
        if (area <= bestArea)
            continue;

        bestArea = area;
        snapshot->frame = frame;
        snapshot->visibleFrame = visibleFrame;
        snapshot->valid = YES;
    }

    CFRelease(windowInfoRef);
    return snapshot->valid;
}

static NSPoint QSSRawMouseOverlayOrigin(QSSSystemSettingsWindowSnapshot snapshot)
{
    CGFloat sidebarWidth = 170.0f;
    CGFloat contentMinX = NSMinX(snapshot.frame) + sidebarWidth;
    CGFloat contentWidth = MAX(NSWidth(snapshot.frame) - sidebarWidth, QSSRawMouseOverlayWidth);
    CGFloat preferredX = contentMinX + ((contentWidth - QSSRawMouseOverlayWidth) / 2.0f) - 8.0f;
    CGFloat preferredY = NSMinY(snapshot.frame) + 14.0f;
    CGFloat minX = NSMinX(snapshot.visibleFrame) + 8.0f;
    CGFloat maxX = NSMaxX(snapshot.visibleFrame) - QSSRawMouseOverlayWidth - 8.0f;
    CGFloat minY = NSMinY(snapshot.visibleFrame) + 8.0f;
    CGFloat maxY = NSMaxY(snapshot.visibleFrame) - QSSRawMouseOverlayHeight - 8.0f;

    if (maxX < minX)
        maxX = minX;
    if (maxY < minY)
        maxY = minY;

    return NSMakePoint(MIN(MAX(preferredX, minX), maxX),
        MIN(MAX(preferredY, minY), maxY));
}

static NSURL *QSSHostAppBundleURL(void)
{
    return [[NSBundle mainBundle] bundleURL];
}

static NSString *QSSHostAppDisplayName(void)
{
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *displayName = [bundle objectForInfoDictionaryKey:@"CFBundleDisplayName"];

    if (!displayName || [displayName length] == 0)
        displayName = [bundle objectForInfoDictionaryKey:(NSString *)kCFBundleNameKey];
    if (!displayName || [displayName length] == 0)
        displayName = [[[bundle bundleURL] URLByDeletingPathExtension] lastPathComponent];

    return displayName ? displayName : @"QSS-M";
}

static NSImage *QSSHostAppIcon(void)
{
    NSImage *icon = [[NSWorkspace sharedWorkspace] iconForFile:[[QSSHostAppBundleURL() path] stringByStandardizingPath]];
    [icon setSize:NSMakeSize(48.0f, 48.0f)];
    return icon;
}

@interface QSSAppDragSourceView : NSView <NSDraggingSource> {
    NSURL *bundleURL;
    NSString *displayName;
    NSImage *appIcon;
    NSView *rowView;
    void (^successfulDropHandler)(void);
}

- (id)initWithFrame:(NSRect)frameRect
          bundleURL:(NSURL *)appBundleURL
        displayName:(NSString *)appDisplayName
               icon:(NSImage *)icon
   onSuccessfulDrop:(void (^)(void))handler;

@end

@implementation QSSAppDragSourceView

- (id)initWithFrame:(NSRect)frameRect
          bundleURL:(NSURL *)appBundleURL
        displayName:(NSString *)appDisplayName
               icon:(NSImage *)icon
   onSuccessfulDrop:(void (^)(void))handler
{
    NSView *iconChrome;
    NSImageView *iconView;
    NSTextField *label;

    self = [super initWithFrame:frameRect];
    if (!self)
        return nil;

    bundleURL = [appBundleURL retain];
    displayName = [appDisplayName copy];
    appIcon = [icon retain];
    successfulDropHandler = [handler copy];

    [self setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [self setWantsLayer:YES];

    rowView = [[[NSView alloc] initWithFrame:[self bounds]] autorelease];
    [rowView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [rowView setWantsLayer:YES];
    [[rowView layer] setCornerRadius:7.0f];
    [[rowView layer] setBorderWidth:1.0f];
    [[rowView layer] setBorderColor:[QSSSeparatorColorForWindow(nil) CGColor]];
    [[rowView layer] setBackgroundColor:[[NSColor controlBackgroundColor] colorWithAlphaComponent:0.96f].CGColor];
    [self addSubview:rowView];

    iconChrome = [[[NSView alloc] initWithFrame:NSMakeRect(10.0f, 8.0f, 26.0f, 26.0f)] autorelease];
    [iconChrome setWantsLayer:YES];
    [[iconChrome layer] setCornerRadius:6.0f];
    [[iconChrome layer] setBackgroundColor:[[NSColor whiteColor] colorWithAlphaComponent:0.9f].CGColor];
    [iconChrome setAutoresizingMask:NSViewMaxXMargin];
    [rowView addSubview:iconChrome];

    iconView = [[[NSImageView alloc] initWithFrame:NSMakeRect(2.0f, 2.0f, 22.0f, 22.0f)] autorelease];
    [iconView setImage:appIcon];
    [iconView setImageScaling:NSImageScaleProportionallyUpOrDown];
    [iconChrome addSubview:iconView];

    label = [[[NSTextField alloc] initWithFrame:NSMakeRect(47.0f, 11.0f, NSWidth(frameRect) - 59.0f, 20.0f)] autorelease];
    [label setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin | NSViewMaxYMargin)];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setFont:[NSFont boldSystemFontOfSize:15.0f]];
    [label setTextColor:[NSColor labelColor]];
    [label setStringValue:displayName];
    [rowView addSubview:label];

    return self;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    (void)event;
    return YES;
}

- (void)mouseDown:(NSEvent *)event
{
    NSDraggingItem *draggingItem;
    NSDraggingSession *session;

    if (!bundleURL)
        return;

    draggingItem = [[[NSDraggingItem alloc] initWithPasteboardWriter:bundleURL] autorelease];
    [draggingItem setDraggingFrame:[self draggingFrame] contents:[self draggingImage]];

    session = [self beginDraggingSessionWithItems:[NSArray arrayWithObject:draggingItem]
                                            event:event
                                           source:self];
    [session setAnimatesToStartingPositionsOnCancelOrFail:YES];
}

- (NSDragOperation)draggingSession:(NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    (void)session;
    (void)context;
    return NSDragOperationCopy;
}

- (void)draggingSession:(NSDraggingSession *)session willBeginAtPoint:(NSPoint)screenPoint
{
    (void)session;
    (void)screenPoint;
    [rowView setHidden:YES];
}

- (void)draggingSession:(NSDraggingSession *)session endedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation
{
    (void)session;
    (void)screenPoint;

    if (operation == NSDragOperationNone)
        [rowView setHidden:NO];
    else if (successfulDropHandler)
        successfulDropHandler();
}

- (NSRect)draggingFrame
{
    return [self convertRect:[rowView bounds] fromView:rowView];
}

- (NSImage *)draggingImage
{
    NSBitmapImageRep *rep;
    NSImage *image;

    rep = [[[rowView bitmapImageRepForCachingDisplayInRect:[rowView bounds]] retain] autorelease];
    [rowView cacheDisplayInRect:[rowView bounds] toBitmapImageRep:rep];

    image = [[[NSImage alloc] initWithSize:[rowView bounds].size] autorelease];
    [image addRepresentation:rep];
    return image;
}

- (void)dealloc
{
    [bundleURL release];
    [displayName release];
    [appIcon release];
    [successfulDropHandler release];
    [super dealloc];
}

@end

@interface QSSCenteredTextFieldCell : NSTextFieldCell
@end

@implementation QSSCenteredTextFieldCell

- (NSRect)qssCenteredTextRectForBounds:(NSRect)rect
{
    NSRect centeredRect = [super drawingRectForBounds:rect];
    NSFont *font = [self font];
    CGFloat textHeight;
    CGFloat delta;
    CGFloat horizontalInset = 8.0f;

    if (!font)
        font = [NSFont systemFontOfSize:[NSFont systemFontSize]];

    if (NSWidth(centeredRect) > horizontalInset * 2.0f) {
        centeredRect.origin.x += horizontalInset;
        centeredRect.size.width -= horizontalInset * 2.0f;
    }

    textHeight = ceilf([font ascender] - [font descender]);
    delta = NSHeight(centeredRect) - textHeight;

    if (delta > 0.0f)
        centeredRect.origin.y += floorf(delta / 2.0f) - 1.0f;

    return centeredRect;
}

- (NSRect)drawingRectForBounds:(NSRect)rect
{
    return [self qssCenteredTextRectForBounds:rect];
}

- (void)editWithFrame:(NSRect)rect
               inView:(NSView *)view
               editor:(NSText *)editor
             delegate:(id)delegate
                event:(NSEvent *)event
{
    [super editWithFrame:[self qssCenteredTextRectForBounds:rect]
                  inView:view
                  editor:editor
                delegate:delegate
                   event:event];
}

- (void)selectWithFrame:(NSRect)rect
                 inView:(NSView *)view
                 editor:(NSText *)editor
               delegate:(id)delegate
                  start:(NSInteger)start
                 length:(NSInteger)length
{
    [super selectWithFrame:[self qssCenteredTextRectForBounds:rect]
                    inView:view
                    editor:editor
                  delegate:delegate
                     start:start
                    length:length];
}

@end

@interface QSSPassthroughTextField : NSTextField
@end

@implementation QSSPassthroughTextField

- (NSView *)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}

@end

@interface QSSAppearanceView : NSView {
    id __unsafe_unretained owner;
    SEL appearanceAction;
}

- (void)setAppearanceOwner:(id)newOwner action:(SEL)newAction;

@end

@implementation QSSAppearanceView

- (void)setAppearanceOwner:(id)newOwner action:(SEL)newAction
{
    owner = newOwner;
    appearanceAction = newAction;
}

- (void)viewDidChangeEffectiveAppearance
{
    void (*appearanceIMP)(id, SEL, id);

    if (@available(macOS 10.14, *))
        [super viewDidChangeEffectiveAppearance];

    if (owner && appearanceAction && [owner respondsToSelector:appearanceAction]) {
        appearanceIMP = (void (*)(id, SEL, id))[owner methodForSelector:appearanceAction];
        appearanceIMP(owner, appearanceAction, self);
    }
}

@end

@interface QSSArgumentChipView : NSView {
    NSString *argumentString;
    NSTextField *label;
    NSButton *closeButton;
    id __unsafe_unretained owner;
    SEL removeAction;
}

- (id)initWithArgument:(NSString *)arg owner:(id)ownerObject removeAction:(SEL)action;
- (NSString *)argumentString;
- (void)refreshAppearance;
- (void)sizeChipToFit;

@end

@implementation QSSArgumentChipView

- (id)initWithArgument:(NSString *)arg owner:(id)ownerObject removeAction:(SEL)action
{
    self = [super initWithFrame:NSMakeRect(0.0f, 0.0f, 80.0f, QSSChipHeight)];
    if (!self)
        return nil;

    argumentString = [arg copy];
    owner = ownerObject;
    removeAction = action;

    [self setWantsLayer:YES];
    [[self layer] setCornerRadius:QSSChipHeight / 2.0f];
    [[self layer] setBorderWidth:0.5f];

    label = [[NSTextField alloc] initWithFrame:NSMakeRect(10.0f, 0.0f, 60.0f, QSSChipHeight)];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setFont:[NSFont systemFontOfSize:11.5f weight:NSFontWeightMedium]];
    [label setTextColor:[NSColor labelColor]];
    [label setStringValue:argumentString ? argumentString : @""];
    [self addSubview:label];
    [label release];

    closeButton = [[NSButton alloc] initWithFrame:NSMakeRect(0.0f, 0.0f, 18.0f, 18.0f)];
    [closeButton setBordered:NO];
    [closeButton setTitle:@"×"];
    [closeButton setFont:[NSFont systemFontOfSize:14.0f weight:NSFontWeightSemibold]];
    [closeButton setButtonType:NSButtonTypeMomentaryChange];
    [closeButton setTarget:self];
    [closeButton setAction:@selector(closeButtonPressed:)];
    [[closeButton cell] setHighlightsBy:NSContentsCellMask];
    [self addSubview:closeButton];
    [closeButton release];

    [self sizeChipToFit];
    [self refreshAppearance];
    return self;
}

- (NSString *)argumentString
{
    return argumentString;
}

- (void)refreshAppearance
{
    NSWindow *window = [self window];

    [[self layer] setBackgroundColor:[QSSLauncherSurfaceColorForWindow(window, 0.92f) CGColor]];
    [[self layer] setBorderColor:[QSSSeparatorColorForWindow(window) CGColor]];
    [label setTextColor:[NSColor labelColor]];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self refreshAppearance];
}

- (void)viewDidChangeEffectiveAppearance
{
    if (@available(macOS 10.14, *))
        [super viewDidChangeEffectiveAppearance];
    [self refreshAppearance];
}

- (void)sizeChipToFit
{
    NSRect labelFrame;
    NSRect closeFrame;
    NSRect selfFrame;

    [label sizeToFit];
    labelFrame = [label frame];
    labelFrame.origin.x = 10.0f;
    labelFrame.origin.y = (QSSChipHeight - NSHeight(labelFrame)) / 2.0f;
    [label setFrame:labelFrame];

    closeFrame.size.width = 18.0f;
    closeFrame.size.height = 18.0f;
    closeFrame.origin.x = NSMaxX(labelFrame) + 2.0f;
    closeFrame.origin.y = (QSSChipHeight - NSHeight(closeFrame)) / 2.0f;
    [closeButton setFrame:closeFrame];

    selfFrame = [self frame];
    selfFrame.size.width = NSMaxX(closeFrame) + 6.0f;
    selfFrame.size.height = QSSChipHeight;
    [self setFrame:selfFrame];
}

- (void)closeButtonPressed:(id)sender
{
    void (*removeIMP)(id, SEL, id);

    (void)sender;
    if (owner && removeAction && [owner respondsToSelector:removeAction])
    {
        removeIMP = (void (*)(id, SEL, id))[owner methodForSelector:removeAction];
        removeIMP(owner, removeAction, self);
    }
}

- (void)dealloc
{
    [argumentString release];
    [super dealloc];
}

@end

@interface QSSShortcutHeaderView : NSView {
    NSString *title;
}

- (void)setTitle:(NSString *)newTitle;

@end

@implementation QSSShortcutHeaderView

- (void)setTitle:(NSString *)newTitle
{
    if (title == newTitle || [title isEqualToString:newTitle])
        return;

    [title release];
    title = [newTitle copy];
    [self setNeedsDisplay:YES];
}

- (void)viewDidChangeEffectiveAppearance
{
    if (@available(macOS 10.14, *))
        [super viewDidChangeEffectiveAppearance];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    NSDictionary *attributes;
    NSFont *font;
    NSColor *textColor;
    NSSize textSize;
    NSPoint textOrigin;

    (void)dirtyRect;
    if ([title length] == 0)
        return;

    font = [NSFont boldSystemFontOfSize:15.0f];
    textColor = [NSColor labelColor];
    attributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: textColor
    };
    textSize = [title sizeWithAttributes:attributes];
    textOrigin.x = 0.0f;
    textOrigin.y = 10.0f;
    if (textOrigin.y + textSize.height > NSHeight([self bounds]))
        textOrigin.y = floorf((float)((NSHeight([self bounds]) - textSize.height) / 2.0f));

    [title drawAtPoint:textOrigin withAttributes:attributes];
}

- (void)dealloc
{
    [title release];
    [super dealloc];
}

@end

@interface QSSShortcutKeysView : NSView {
    NSArray *shortcutTokens;
}

- (void)setShortcutKeysString:(NSString *)keys;

@end

@implementation QSSShortcutKeysView

- (void)setShortcutKeysString:(NSString *)keys
{
    NSArray *tokens = QSSDisplayShortcutTokensForKeys(keys ? keys : @"");

    if ([tokens isEqualToArray:shortcutTokens])
        return;

    [shortcutTokens release];
    shortcutTokens = [tokens copy];
    [self setNeedsDisplay:YES];
}

- (void)viewDidChangeEffectiveAppearance
{
    if (@available(macOS 10.14, *))
        [super viewDidChangeEffectiveAppearance];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    NSDictionary *attributes;
    NSMutableArray *tokenWidths;
    NSColor *fillColor;
    NSColor *textColor;
    NSFont *font;
    CGFloat totalWidth = 0.0f;
    CGFloat gap = 7.0f;
    CGFloat horizontalPadding = 9.0f;
    CGFloat pillHeight = 22.0f;
    CGFloat x;
    CGFloat y;
    NSRect bounds = [self bounds];

    (void)dirtyRect;
    if ([shortcutTokens count] == 0)
        return;

    font = [NSFont systemFontOfSize:13.0f weight:NSFontWeightMedium];
    fillColor = QSSShortcutPillFillColorForWindow([self window]);
    textColor = QSSShortcutPillTextColorForWindow([self window]);
    attributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: textColor
    };

    tokenWidths = [NSMutableArray arrayWithCapacity:[shortcutTokens count]];
    for (NSString *token in shortcutTokens) {
        NSSize textSize = [token sizeWithAttributes:attributes];
        CGFloat width = ceilf((float)textSize.width) + horizontalPadding * 2.0f;

        [tokenWidths addObject:[NSNumber numberWithDouble:width]];
        totalWidth += width;
    }

    totalWidth += gap * ([shortcutTokens count] - 1);
    x = floorf((float)(NSMaxX(bounds) - totalWidth - QSSShortcutTableRightGutter));
    y = floorf((float)(NSMidY(bounds) - pillHeight / 2.0f));

    for (NSUInteger i = 0; i < [shortcutTokens count]; i++) {
        NSString *token = [shortcutTokens objectAtIndex:i];
        CGFloat width = [[tokenWidths objectAtIndex:i] doubleValue];
        NSRect pillRect = NSMakeRect(x, y, width, pillHeight);
        NSSize textSize = [token sizeWithAttributes:attributes];
        NSPoint textOrigin;

        [fillColor setFill];
        [[NSBezierPath bezierPathWithRoundedRect:pillRect
                                         xRadius:pillHeight / 2.0f
                                         yRadius:pillHeight / 2.0f] fill];

        textOrigin.x = floorf((float)(NSMidX(pillRect) - textSize.width / 2.0f));
        textOrigin.y = floorf((float)(NSMidY(pillRect) - textSize.height / 2.0f));
        [token drawAtPoint:textOrigin withAttributes:attributes];
        x += width + gap;
    }
}

- (void)dealloc
{
    [shortcutTokens release];
    [super dealloc];
}

@end

@interface AppController ()
- (NSText *)activeArgumentFieldEditor;
- (NSString *)currentArgumentText;
- (void)installQuitKeyMonitor;
- (void)handleCommandQEventDown:(BOOL)down;
- (void)beginQuitHold;
- (void)cancelQuitHold;
- (void)cancelQuitHoldAfterDelay;
- (void)quitHoldDismissTimerFired:(NSTimer *)timer;
- (void)quitHoldTimerFired:(NSTimer *)timer;
- (void)updateQuitHoldProgress:(NSTimer *)timer;
- (void)showQuitHoldOverlay;
- (void)hideQuitHoldOverlay;
- (void)quitNow;
- (BOOL)warnBeforeQuittingEnabled;
- (void)hideArgumentCompletionGhost;
- (void)updateArgumentCompletionGhostWithText:(NSString *)text
                                   completion:(NSString *)completion
                                   tokenRange:(NSRange)tokenRange;
- (void)updateArgumentCompletionHint;
- (void)launcherAppearanceDidChange:(id)sender;
- (void)refreshLauncherAppearance;
@end

@implementation AppController

+(void) initialize {
    NSMutableDictionary *defaults = [NSMutableDictionary dictionary];

    [defaults setObject:@"" forKey:FQPrefCommandLineKey];
    [defaults setObject:[NSNumber numberWithBool:NO] forKey:QSSPrefRawMouseInputEnabledKey];
    [defaults setObject:[NSNumber numberWithBool:YES] forKey:QSSPrefWarnBeforeQuittingKey];

    [[NSUserDefaults standardUserDefaults] registerDefaults:defaults];
}

- (id)init {
    self = [super init];
    if (!self)
        return nil;

    arguments = [[QuakeArguments alloc] initWithArguments:gArgv + 1 count:gArgc - 1];
    argumentChips = [[NSMutableArray alloc] init];
    return self;
}

#pragma mark - Command-line helpers

- (NSString *)sanitizeCommandLine:(NSString *)args
{
    if (!args)
        return @"";

    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    NSArray *tokens = [args componentsSeparatedByCharactersInSet:ws];
    NSMutableArray *kept = [NSMutableArray arrayWithCapacity:[tokens count]];

    for (NSString *tok in tokens) {
        if ([tok length] == 0)
            continue;
        if ([tok isEqualToString:@"-nolauncher"])
            continue;
        [kept addObject:tok];
    }

    return [kept componentsJoinedByString:@" "];
}

- (NSArray *)chipStringsFromCommandLine:(NSString *)line
{
    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    NSArray *tokens;
    NSMutableArray *chips = [NSMutableArray array];
    NSMutableString *current = nil;

    if (!line)
        return chips;

    tokens = [line componentsSeparatedByCharactersInSet:ws];
    for (NSString *tok in tokens) {
        unichar c;
        if ([tok length] == 0)
            continue;
        c = [tok characterAtIndex:0];
        if (c == '-' || c == '+') {
            if (current)
                [chips addObject:[[current copy] autorelease]];
            current = [NSMutableString stringWithString:tok];
        } else if (current) {
            [current appendFormat:@" %@", tok];
        } else {
            current = [NSMutableString stringWithString:tok];
        }
    }
    if (current)
        [chips addObject:[[current copy] autorelease]];

    return chips;
}

- (NSString *)joinedArgumentString
{
    NSMutableArray *strings = [NSMutableArray arrayWithCapacity:[argumentChips count]];
    for (QSSArgumentChipView *chip in argumentChips) {
        NSString *s = [chip argumentString];
        if (s && [s length] > 0)
            [strings addObject:s];
    }
    return [strings componentsJoinedByString:@" "];
}

- (NSString *)effectiveCommandLineWithLaunchOptions:(NSString *)launchOptions
{
    NSString *initialArgs = [self sanitizeCommandLine:[arguments description]];
    NSString *uiArgs = [self sanitizeCommandLine:launchOptions];

    if ([initialArgs length] == 0)
        return uiArgs ? uiArgs : @"";
    if ([uiArgs length] == 0)
        return initialArgs;

    return [NSString stringWithFormat:@"%@ %@", initialArgs, uiArgs];
}

#pragma mark - Menu configuration

- (NSArray *)keyboardShortcutRows
{
    NSMutableArray *rows;

    if (keyboardShortcutRows)
        return keyboardShortcutRows;

    rows = [NSMutableArray array];

    [rows addObject:QSSKeyboardShortcutHeader(@"App and System")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Show keyboard shortcuts", @"Cmd+/")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Toggle fullscreen", @"Option+Enter")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Minimize from fullscreen", @"Cmd+Tab")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Show command history", @"Cmd+Y")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Cancel active download, port probe, or connection", @"Cmd+.")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Paste clipboard file", @"Cmd+V")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Mute or unmute sound", @"Cmd+M")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Increase UI scale", @"Cmd+Shift+Wheel Up")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Decrease UI scale", @"Cmd+Shift+Wheel Down")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Increase volume", @"Option+Shift+Wheel Up")];
    [rows addObject:QSSKeyboardShortcutItem(@"App and System", @"Decrease volume", @"Option+Shift+Wheel Down")];

    [rows addObject:QSSKeyboardShortcutHeader(@"Movement")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Move forward", @"W / Up Arrow")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Move backward", @"S / Down Arrow")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Move left", @"A / ,")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Move right", @"D / .")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Turn left", @"Left Arrow")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Turn right", @"Right Arrow")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Strafe", @"Alt/Option")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Run", @"Shift")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Jump / swim up", @"Space / Mouse2 / Left Trigger")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Swim up", @"E")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Swim down", @"C")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Look up", @"Page Down")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Look down", @"Delete")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Center view", @"End")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Mouse look", @"Backslash")];
    [rows addObject:QSSKeyboardShortcutItem(@"Movement", @"Keyboard look", @"Insert")];

    [rows addObject:QSSKeyboardShortcutHeader(@"Combat and Weapons")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Attack", @"Ctrl / Mouse1 / Right Trigger")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Next weapon", @"Slash / Wheel Down / Right Shoulder")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Previous weapon", @"Wheel Up / Left Shoulder")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Axe", @"1")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Shotgun", @"2")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Super Shotgun", @"3")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Nailgun", @"4")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Super Nailgun", @"5")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Grenade Launcher", @"6")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Rocket Launcher", @"7")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Thunderbolt", @"8")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Impulse 0", @"0")];
    [rows addObject:QSSKeyboardShortcutItem(@"Combat and Weapons", @"Weapon wheel", @"Y Button")];

    [rows addObject:QSSKeyboardShortcutHeader(@"Menus and HUD")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Show scores", @"Tab")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Help", @"F1")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Save menu", @"F2")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Load menu", @"F3")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Options menu", @"F4")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Multiplayer menu", @"F5")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Quicksave", @"F6")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Quickload", @"F9")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Quit prompt", @"F10")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Screenshot", @"F12 / Print Screen")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Toggle zoom", @"F11")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Pause", @"Pause")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Main menu", @"Esc")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Larger view", @"+ / =")];
    [rows addObject:QSSKeyboardShortcutItem(@"Menus and HUD", @"Smaller view", @"-")];

    [rows addObject:QSSKeyboardShortcutHeader(@"Console")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Toggle console", @"` / ~")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Force console", @"Shift+Esc")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Autocomplete", @"Tab")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Previous or next command", @"Up Arrow / Down Arrow")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Scroll console", @"Page Up / Page Down / Wheel")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Page console scroll", @"Cmd+Page Up / Cmd+Page Down")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Jump to top or bottom", @"Cmd+Home / Cmd+End")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Move cursor by word", @"Cmd+Left / Cmd+Right")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Adjust console height", @"Cmd+Up / Cmd+Down")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Extend selection", @"Shift+Arrow")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Delete previous or next word", @"Cmd+Backspace / Cmd+Delete")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Clear line", @"Cmd+U")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Paste text", @"Cmd+V / Shift+Insert")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Select all", @"Cmd+A")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Copy console", @"Cmd+C")];
    [rows addObject:QSSKeyboardShortcutItem(@"Console", @"Abort line", @"Cmd+D")];

    [rows addObject:QSSKeyboardShortcutHeader(@"Chat")];
    [rows addObject:QSSKeyboardShortcutItem(@"Chat", @"Open chat", @"T")];
    [rows addObject:QSSKeyboardShortcutItem(@"Chat", @"Send or cancel chat", @"Enter / Esc")];
    [rows addObject:QSSKeyboardShortcutItem(@"Chat", @"Send chat as team chat", @"Cmd+Enter")];
    [rows addObject:QSSKeyboardShortcutItem(@"Chat", @"Delete previous word", @"Cmd+Backspace")];
    [rows addObject:QSSKeyboardShortcutItem(@"Chat", @"Clear message", @"Cmd+U")];
    [rows addObject:QSSKeyboardShortcutItem(@"Chat", @"Paste message", @"Cmd+V")];

    [rows addObject:QSSKeyboardShortcutHeader(@"Demo Playback")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Pause or resume", @"Space")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Increase speed", @"Up Arrow / Shift+.")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Decrease speed", @"Down Arrow / Shift+,")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Rewind or fast-forward", @"Left Arrow / Right Arrow")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Fine rewind or fast-forward", @"Cmd+Left / Cmd+Right")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Single-frame step while paused", @", / .")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Seek to 10-90 percent", @"1-9")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Restart demo", @"0 / Home")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Jump to end", @"End")];
    [rows addObject:QSSKeyboardShortcutItem(@"Demo Playback", @"Jump backward or forward 10 seconds", @"J / L")];

    keyboardShortcutRows = [rows copy];
    return keyboardShortcutRows;
}

- (NSArray *)keyboardShortcutSearchTerms
{
    NSString *text = [[keyboardShortcutsSearchField stringValue] lowercaseString];
    NSArray *parts = [text componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSMutableArray *terms = [NSMutableArray array];

    for (NSString *part in parts) {
        if ([part length] > 0)
            [terms addObject:part];
    }

    return terms;
}

- (BOOL)keyboardShortcutRow:(NSDictionary *)row matchesSearchTerms:(NSArray *)terms
{
    NSString *searchText = [row objectForKey:QSSShortcutRowSearchKey];

    for (NSString *term in terms) {
        if ([searchText rangeOfString:term].location == NSNotFound)
            return NO;
    }

    return YES;
}

- (void)filterKeyboardShortcuts
{
    NSArray *terms = [self keyboardShortcutSearchTerms];
    NSMutableArray *rows = [NSMutableArray array];
    NSDictionary *pendingHeader = nil;

    if ([terms count] == 0) {
        [filteredKeyboardShortcutRows release];
        filteredKeyboardShortcutRows = [[self keyboardShortcutRows] retain];
        [keyboardShortcutsTableView reloadData];
        return;
    }

    for (NSDictionary *row in [self keyboardShortcutRows]) {
        if ([[row objectForKey:QSSShortcutRowHeaderKey] boolValue]) {
            pendingHeader = row;
            continue;
        }

        if ([self keyboardShortcutRow:row matchesSearchTerms:terms]) {
            if (pendingHeader) {
                [rows addObject:pendingHeader];
                pendingHeader = nil;
            }
            [rows addObject:row];
        }
    }

    [filteredKeyboardShortcutRows release];
    filteredKeyboardShortcutRows = [rows copy];
    [keyboardShortcutsTableView reloadData];
}

- (void)configureAboutMenu {
    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenuItem *appMenuItem = ([mainMenu numberOfItems] > 0) ? [mainMenu itemAtIndex:0] : nil;
    NSMenu *appMenu = [appMenuItem submenu];
    NSMenuItem *aboutItem = [appMenu itemWithTitle:@"About QSS-M"];
    if (!aboutItem && [appMenu numberOfItems] > 0)
        aboutItem = [appMenu itemAtIndex:0];

    if (aboutItem) {
        NSString *aboutIconPath = [[NSBundle mainBundle] pathForResource:@"QSSAboutMenuIcon"
                                                                     ofType:@"png"];
        NSImage *aboutIcon = [[[NSImage alloc] initWithContentsOfFile:aboutIconPath] autorelease];

        [aboutItem setTitle:@"About QSS-M"];
        [aboutItem setTarget:self];
        [aboutItem setAction:@selector(showAboutPanel:)];
        [aboutItem setEnabled:YES];

        /* Replace macOS's default info glyph with the supplied white QSS-M emblem. */
        [aboutIcon setSize:NSMakeSize(16.0f, 16.0f)];
        [aboutItem setImage:aboutIcon];
    }
}

- (void)configureSettingsMenu
{
    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenuItem *appMenuItem = ([mainMenu numberOfItems] > 0) ? [mainMenu itemAtIndex:0] : nil;
    NSMenu *appMenu = [appMenuItem submenu];
    NSMenuItem *settingsItem = [appMenu itemWithTitle:@"Settings…"];

    if (!settingsItem)
        settingsItem = [appMenu itemWithTitle:@"Preferences…"];

    if (settingsItem) {
        [settingsItem setTitle:@"Settings…"];
        [settingsItem setTarget:self];
        [settingsItem setAction:@selector(showSettings:)];
        [settingsItem setKeyEquivalent:@","];
        [settingsItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        [settingsItem setEnabled:YES];
    }
}

- (void)configureHelpMenu
{
    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenuItem *helpMenuItem;
    NSMenu *helpMenu;
    NSMenuItem *shortcutsItem;

    if (!mainMenu)
        return;

    helpMenuItem = [mainMenu itemWithTitle:@"Help"];
    if (!helpMenuItem) {
        helpMenuItem = [[[NSMenuItem alloc] initWithTitle:@"Help" action:NULL keyEquivalent:@""] autorelease];
        [mainMenu addItem:helpMenuItem];
    }

    helpMenu = [helpMenuItem submenu];
    if (!helpMenu) {
        helpMenu = [[[NSMenu alloc] initWithTitle:@"Help"] autorelease];
        [helpMenuItem setSubmenu:helpMenu];
    }

    shortcutsItem = [helpMenu itemWithTitle:@"Keyboard Shortcuts..."];
    if (!shortcutsItem) {
        BOOL addSeparator = [helpMenu numberOfItems] > 0 && ![[helpMenu itemAtIndex:0] isSeparatorItem];

        shortcutsItem = [[[NSMenuItem alloc] initWithTitle:@"Keyboard Shortcuts..."
                                                    action:@selector(showKeyboardShortcutsPanel:)
                                             keyEquivalent:@"/"] autorelease];
        [helpMenu insertItem:shortcutsItem atIndex:0];
        if (addSeparator)
            [helpMenu insertItem:[NSMenuItem separatorItem] atIndex:1];
    }

    [shortcutsItem setTarget:self];
    [shortcutsItem setAction:@selector(showKeyboardShortcutsPanel:)];
    [shortcutsItem setKeyEquivalent:@"/"];
    [shortcutsItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [shortcutsItem setEnabled:YES];
}

- (void)configureQuitMenu {
    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenuItem *appMenuItem = ([mainMenu numberOfItems] > 0) ? [mainMenu itemAtIndex:0] : nil;
    NSMenu *appMenu = [appMenuItem submenu];
    NSMenuItem *quitItem = [appMenu itemWithTitle:@"Quit QSS-M"];
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    BOOL warnBeforeQuitting = [defaults boolForKey:QSSPrefWarnBeforeQuittingKey];

    if (!quitItem)
        quitItem = [appMenu itemWithTitle:@"Hold \u2318Q to Quit"];

    if (!quitItem) {
        NSInteger idx = [appMenu indexOfItemWithTarget:nil andAction:@selector(terminate:)];
        if (idx != -1)
            quitItem = [appMenu itemAtIndex:idx];
    }

    if (quitItem) {
        warnBeforeQuittingItem = [appMenu itemWithTitle:@"Warn Before Quitting"];
        if (!warnBeforeQuittingItem) {
            warnBeforeQuittingItem = [[[NSMenuItem alloc] initWithTitle:@"Warn Before Quitting"
                                                                   action:@selector(toggleWarnBeforeQuitting:)
                                                            keyEquivalent:@""] autorelease];
            [appMenu insertItem:warnBeforeQuittingItem atIndex:[appMenu indexOfItem:quitItem]];
        }

        [warnBeforeQuittingItem setTarget:self];
        [warnBeforeQuittingItem setAction:@selector(toggleWarnBeforeQuitting:)];
        [warnBeforeQuittingItem setState:warnBeforeQuitting ? NSControlStateValueOn : NSControlStateValueOff];
        [warnBeforeQuittingItem setEnabled:YES];

        [quitItem setTitle:warnBeforeQuitting ? @"Hold \u2318Q to Quit" : @"Quit QSS-M"];
        [quitItem setTarget:self];
        [quitItem setAction:@selector(cancel:)];
        [quitItem setKeyEquivalent:warnBeforeQuitting ? @"" : @"q"];
        [quitItem setKeyEquivalentModifierMask:warnBeforeQuitting ? 0 : NSEventModifierFlagCommand];
        [quitItem setEnabled:YES];
    }
}

- (BOOL)warnBeforeQuittingEnabled
{
    return [[NSUserDefaults standardUserDefaults] boolForKey:QSSPrefWarnBeforeQuittingKey];
}

- (void)installQuitKeyMonitor
{
    __block AppController *blockSelf = self;

    if (quitKeyMonitor)
        return;

    quitKeyMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown | NSEventMaskKeyUp)
        handler:^NSEvent *(NSEvent *event) {
            NSEventModifierFlags flags;
            NSString *characters;
            BOOL commandQ;

            if (![blockSelf warnBeforeQuittingEnabled])
                return event;

            flags = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
            characters = [[event charactersIgnoringModifiers] lowercaseString];
            commandQ = ([characters isEqualToString:@"q"] &&
                        (flags & NSEventModifierFlagCommand) != 0 &&
                        (flags & (NSEventModifierFlagShift | NSEventModifierFlagOption |
                                  NSEventModifierFlagControl)) == 0);

            if ([event type] == NSEventTypeKeyDown && commandQ) {
                [blockSelf handleCommandQEventDown:YES];
                /* Do not let the key equivalent or the game see Command-Q. */
                return nil;
            }

            if ([event type] == NSEventTypeKeyUp &&
                blockSelf->quitKeyDown && [characters isEqualToString:@"q"]) {
                [blockSelf handleCommandQEventDown:NO];
                return nil;
            }

            return event;
        }] retain];
}

- (void)handleCommandQEventDown:(BOOL)down
{
    if (![self warnBeforeQuittingEnabled]) {
        if (down)
            [self quitNow];
        return;
    }

    if (down) {
        if (!quitKeyDown) {
            quitKeyDown = YES;
            [self beginQuitHold];
        }
    } else if (quitKeyDown) {
        quitKeyDown = NO;
        [self cancelQuitHoldAfterDelay];
    }
}

void PL_CommandQEvent(int down)
{
    id delegate = [NSApp delegate];

    if ([delegate respondsToSelector:@selector(handleCommandQEventDown:)]) {
        [(AppController *)delegate handleCommandQEventDown:(down != 0)];
        return;
    }

    if (down) {
        SDL_Event event = {0};
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
    }
}

- (void)beginQuitHold
{
    if (quitHoldTimer)
        return;

    if (quitHoldDismissTimer) {
        [quitHoldDismissTimer invalidate];
        [quitHoldDismissTimer release];
        quitHoldDismissTimer = nil;
    }
    [self showQuitHoldOverlay];
    quitHoldStartedAt = CFAbsoluteTimeGetCurrent();
    [quitHoldOverlayView setProgress:0.0f];
    quitHoldProgressTimer = [[NSTimer scheduledTimerWithTimeInterval:(1.0 / 120.0)
                                                               target:self
                                                             selector:@selector(updateQuitHoldProgress:)
                                                             userInfo:nil
                                                              repeats:YES] retain];
    [quitHoldProgressTimer setTolerance:0.0];
    quitHoldTimer = [[NSTimer scheduledTimerWithTimeInterval:QSSCommandQHoldDuration
                                                       target:self
                                                     selector:@selector(quitHoldTimerFired:)
                                                     userInfo:nil
                                                      repeats:NO] retain];
}

- (void)cancelQuitHold
{
    if (quitHoldTimer) {
        [quitHoldTimer invalidate];
        [quitHoldTimer release];
        quitHoldTimer = nil;
    }
    if (quitHoldProgressTimer) {
        [quitHoldProgressTimer invalidate];
        [quitHoldProgressTimer release];
        quitHoldProgressTimer = nil;
    }
    if (quitHoldDismissTimer) {
        [quitHoldDismissTimer invalidate];
        [quitHoldDismissTimer release];
        quitHoldDismissTimer = nil;
    }
    [quitHoldOverlayView setProgress:0.0f];
    [self hideQuitHoldOverlay];
}

- (void)cancelQuitHoldAfterDelay
{
    if (quitHoldTimer) {
        [quitHoldTimer invalidate];
        [quitHoldTimer release];
        quitHoldTimer = nil;
    }
    if (quitHoldProgressTimer) {
        [quitHoldProgressTimer invalidate];
        [quitHoldProgressTimer release];
        quitHoldProgressTimer = nil;
    }
    [quitHoldOverlayView setProgress:0.0f];
    [quitHoldOverlayView displayIfNeeded];
    if (quitHoldDismissTimer) {
        [quitHoldDismissTimer invalidate];
        [quitHoldDismissTimer release];
    }
    quitHoldDismissTimer = [[NSTimer scheduledTimerWithTimeInterval:QSSQuitHoldCancelledDisplayDuration
                                                              target:self
                                                            selector:@selector(quitHoldDismissTimerFired:)
                                                            userInfo:nil
                                                             repeats:NO] retain];
}

- (void)quitHoldDismissTimerFired:(NSTimer *)timer
{
    (void)timer;

    [quitHoldDismissTimer release];
    quitHoldDismissTimer = nil;
    [quitHoldOverlayView setProgress:0.0f];
    [self hideQuitHoldOverlay];
}

- (void)showQuitHoldOverlay
{
    NSView *overlayView;
    NSWindow *anchorWindow;
    NSScreen *screen;
    NSRect frame;

    if (!quitHoldOverlayWindow) {
        frame = NSMakeRect(0.0f, 0.0f, QSSQuitHoldOverlayWidth, QSSQuitHoldOverlayHeight);
        quitHoldOverlayWindow = [[NSPanel alloc] initWithContentRect:frame
                                                             styleMask:(NSWindowStyleMaskBorderless |
                                                                        NSWindowStyleMaskNonactivatingPanel)
                                                               backing:NSBackingStoreBuffered
                                                                 defer:NO];
        [quitHoldOverlayWindow setOpaque:NO];
        [quitHoldOverlayWindow setBackgroundColor:[NSColor clearColor]];
        [quitHoldOverlayWindow setHasShadow:NO];
        [quitHoldOverlayWindow setFloatingPanel:YES];
        [quitHoldOverlayWindow setHidesOnDeactivate:NO];
        [quitHoldOverlayWindow setIgnoresMouseEvents:YES];
        /* Exclusive fullscreen uses Core Graphics' shielding level. The HUD
           exists only while Command-Q is held, so placing it one level above
           the shield is safe and keeps it visible in every video mode. */
        [quitHoldOverlayWindow setLevel:(NSWindowLevel)(CGShieldingWindowLevel() + 1)];
        [quitHoldOverlayWindow setAnimationBehavior:NSWindowAnimationBehaviorNone];
        [quitHoldOverlayWindow setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces |
                                                       NSWindowCollectionBehaviorFullScreenAuxiliary |
                                                       NSWindowCollectionBehaviorStationary |
                                                       NSWindowCollectionBehaviorIgnoresCycle)];

        /* A fixed alpha view preserves the same translucent appearance over
           the launcher, console, renderer and every fullscreen style. */
        overlayView = [[[QSSQuitHoldOverlayView alloc] initWithFrame:frame] autorelease];
        quitHoldOverlayView = overlayView;
        [overlayView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [[quitHoldOverlayWindow contentView] addSubview:overlayView];
    }

    anchorWindow = [NSApp keyWindow];
    if (!anchorWindow)
        anchorWindow = [NSApp mainWindow];
    screen = anchorWindow ? [anchorWindow screen] : [NSScreen mainScreen];
    if (anchorWindow)
        frame = [anchorWindow frame];
    else
        frame = [screen visibleFrame];

    frame.origin.x = NSMidX(frame) - QSSQuitHoldOverlayWidth * 0.5f;
    frame.origin.y = NSMaxY(frame) - QSSQuitHoldOverlayHeight - 72.0f;
    frame.size.width = QSSQuitHoldOverlayWidth;
    frame.size.height = QSSQuitHoldOverlayHeight;
    [quitHoldOverlayWindow setFrame:frame display:NO];
    [quitHoldOverlayWindow orderFrontRegardless];
}

- (void)hideQuitHoldOverlay
{
    if (quitHoldOverlayWindow)
        [quitHoldOverlayWindow orderOut:nil];
}

- (void)quitHoldTimerFired:(NSTimer *)timer
{
    (void)timer;

    [quitHoldTimer release];
    quitHoldTimer = nil;
    [quitHoldProgressTimer invalidate];
    [quitHoldProgressTimer release];
    quitHoldProgressTimer = nil;
    [quitHoldOverlayView setProgress:1.0f];
    [quitHoldOverlayView displayIfNeeded];
    [quitHoldOverlayWindow display];
    [self performSelector:@selector(quitNow)
               withObject:nil
               afterDelay:QSSQuitHoldCompletionDisplayDuration];
}

- (void)updateQuitHoldProgress:(NSTimer *)timer
{
    CGFloat elapsed;

    (void)timer;
    elapsed = (CGFloat)(CFAbsoluteTimeGetCurrent() - quitHoldStartedAt);
    [quitHoldOverlayView setProgress:elapsed / (CGFloat)QSSCommandQHoldDuration];
}

- (void)toggleWarnBeforeQuitting:(id)sender
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    BOOL enabled = ![self warnBeforeQuittingEnabled];

    (void)sender;
    [defaults setBool:enabled forKey:QSSPrefWarnBeforeQuittingKey];
    [defaults synchronize];
    if (!enabled) {
        quitKeyDown = NO;
        [self cancelQuitHold];
    }
    [self configureQuitMenu];
}

- (void)configureFileMenu
{
    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenuItem *fileMenuItem;
    NSMenu *fileMenu;
    NSMenuItem *openFolderItem;

    if (!mainMenu)
        return;

    fileMenuItem = [mainMenu itemWithTitle:@"File"];
    if (!fileMenuItem) {
        fileMenuItem = [[[NSMenuItem alloc] initWithTitle:@"File" action:NULL keyEquivalent:@""] autorelease];
        [mainMenu insertItem:fileMenuItem atIndex:MIN(1, [mainMenu numberOfItems])];
    }

    fileMenu = [fileMenuItem submenu];
    if (!fileMenu) {
        fileMenu = [[[NSMenu alloc] initWithTitle:@"File"] autorelease];
        [fileMenuItem setSubmenu:fileMenu];
    }

    [fileMenu removeAllItems];
    openFolderItem = [[[NSMenuItem alloc] initWithTitle:@"Open Quake Folder"
                                                 action:@selector(openQuakeFolder:)
                                          keyEquivalent:@""] autorelease];
    [openFolderItem setTarget:self];
    [openFolderItem setEnabled:YES];
    [fileMenu addItem:openFolderItem];
}

- (void)removeUnusedMenus
{
    NSMenu *mainMenu = [NSApp mainMenu];
    NSArray *titles = @[ @"Format", @"View" ];

    for (NSString *title in titles) {
        NSMenuItem *item = [mainMenu itemWithTitle:title];
        if (item)
            [mainMenu removeItem:item];
    }
}

#pragma mark - Keyboard shortcuts window

- (NSTextField *)createShortcutLabelWithIdentifier:(NSString *)identifier
{
    NSTextField *field = [[[NSTextField alloc] initWithFrame:NSZeroRect] autorelease];

    [field setIdentifier:identifier];
    [field setBezeled:NO];
    [field setDrawsBackground:NO];
    [field setEditable:NO];
    [field setSelectable:NO];
    [field setLineBreakMode:NSLineBreakByTruncatingTail];
    [[field cell] setUsesSingleLineMode:YES];

    return field;
}

- (void)layoutKeyboardShortcutsTableColumns
{
    NSScrollView *scrollView = [keyboardShortcutsTableView enclosingScrollView];
    NSTableColumn *actionColumn = [keyboardShortcutsTableView tableColumnWithIdentifier:QSSShortcutActionColumnIdentifier];
    NSTableColumn *keysColumn = [keyboardShortcutsTableView tableColumnWithIdentifier:QSSShortcutKeysColumnIdentifier];
    NSRect tableFrame;
    CGFloat contentWidth;
    CGFloat columnWidth;
    CGFloat intercellWidth;
    CGFloat keysWidth;
    CGFloat actionWidth;

    if (!scrollView || !actionColumn || !keysColumn)
        return;

    contentWidth = floorf((float)NSWidth([[scrollView contentView] bounds]));
    if (contentWidth <= 0.0f)
        return;

    tableFrame = [keyboardShortcutsTableView frame];
    tableFrame.size.width = contentWidth;
    [keyboardShortcutsTableView setFrame:tableFrame];

    intercellWidth = [keyboardShortcutsTableView intercellSpacing].width;
    columnWidth = MAX(1.0f, contentWidth - QSSShortcutTableRightGutter);
    keysWidth = floorf((float)MIN(360.0f, MAX(220.0f, columnWidth * 0.42f)));
    if (keysWidth > columnWidth - 160.0f)
        keysWidth = MAX(120.0f, columnWidth - 160.0f);

    actionWidth = MAX(1.0f, columnWidth - keysWidth - intercellWidth);
    [actionColumn setWidth:actionWidth];
    [keysColumn setWidth:keysWidth];
}

- (void)createKeyboardShortcutsWindowIfNeeded
{
    NSRect frame = NSMakeRect(0.0f, 0.0f, 860.0f, 640.0f);
    NSView *contentView;
    NSTextField *titleField;
    NSScrollView *scrollView;
    NSTableColumn *actionColumn;
    NSTableColumn *keysColumn;

    if (keyboardShortcutsWindow)
        return;

    keyboardShortcutsWindow = [[NSWindow alloc] initWithContentRect:frame
                                                          styleMask:(NSWindowStyleMaskTitled |
                                                                     NSWindowStyleMaskClosable |
                                                                     NSWindowStyleMaskResizable)
                                                            backing:NSBackingStoreBuffered
                                                              defer:NO];
    [keyboardShortcutsWindow setTitle:@"Keyboard Shortcuts"];
    [keyboardShortcutsWindow setDelegate:self];
    [keyboardShortcutsWindow setReleasedWhenClosed:NO];
    [keyboardShortcutsWindow setMinSize:NSMakeSize(640.0f, 420.0f)];

    contentView = [keyboardShortcutsWindow contentView];

    titleField = [[[NSTextField alloc] initWithFrame:NSMakeRect(24.0f, 586.0f, 812.0f, 32.0f)] autorelease];
    [titleField setBezeled:NO];
    [titleField setDrawsBackground:NO];
    [titleField setEditable:NO];
    [titleField setSelectable:NO];
    [titleField setFont:[NSFont boldSystemFontOfSize:24.0f]];
    [titleField setStringValue:@"Keyboard Shortcuts"];
    [titleField setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [contentView addSubview:titleField];

    keyboardShortcutsSearchField = [[NSSearchField alloc] initWithFrame:NSMakeRect(24.0f, 538.0f, 812.0f, 32.0f)];
    [keyboardShortcutsSearchField setPlaceholderString:@"Search Shortcuts"];
    [keyboardShortcutsSearchField setDelegate:self];
    [keyboardShortcutsSearchField setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [contentView addSubview:keyboardShortcutsSearchField];

    scrollView = [[[NSScrollView alloc] initWithFrame:NSMakeRect(24.0f, 24.0f, 812.0f, 492.0f)] autorelease];
    [scrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:NO];
    [scrollView setHorizontalScrollElasticity:NSScrollElasticityNone];
    [scrollView setScrollerStyle:NSScrollerStyleOverlay];
    [scrollView setAutohidesScrollers:YES];
    [scrollView setBorderType:NSNoBorder];
    [scrollView setDrawsBackground:NO];

    keyboardShortcutsTableView = [[NSTableView alloc] initWithFrame:[[scrollView contentView] bounds]];
    [keyboardShortcutsTableView setHeaderView:nil];
    [keyboardShortcutsTableView setDelegate:self];
    [keyboardShortcutsTableView setDataSource:self];
    [keyboardShortcutsTableView setRowHeight:30.0f];
    [keyboardShortcutsTableView setSelectionHighlightStyle:NSTableViewSelectionHighlightStyleNone];
    [keyboardShortcutsTableView setColumnAutoresizingStyle:NSTableViewNoColumnAutoresizing];
    [keyboardShortcutsTableView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    actionColumn = [[[NSTableColumn alloc] initWithIdentifier:QSSShortcutActionColumnIdentifier] autorelease];
    [actionColumn setWidth:440.0f];
    [actionColumn setMinWidth:140.0f];
    [actionColumn setResizingMask:NSTableColumnNoResizing];
    [keyboardShortcutsTableView addTableColumn:actionColumn];

    keysColumn = [[[NSTableColumn alloc] initWithIdentifier:QSSShortcutKeysColumnIdentifier] autorelease];
    [keysColumn setWidth:350.0f];
    [keysColumn setMinWidth:120.0f];
    [keysColumn setResizingMask:NSTableColumnNoResizing];
    [keyboardShortcutsTableView addTableColumn:keysColumn];

    [scrollView setDocumentView:keyboardShortcutsTableView];
    [self layoutKeyboardShortcutsTableColumns];
    [contentView addSubview:scrollView];

    [self filterKeyboardShortcuts];
}

- (NSDictionary *)keyboardShortcutRowAtIndex:(NSInteger)row
{
    if (row < 0 || row >= (NSInteger)[filteredKeyboardShortcutRows count])
        return nil;

    return [filteredKeyboardShortcutRows objectAtIndex:(NSUInteger)row];
}

- (void)windowDidResize:(NSNotification *)notification
{
    if ([notification object] == keyboardShortcutsWindow)
        [self layoutKeyboardShortcutsTableColumns];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    if (tableView != keyboardShortcutsTableView)
        return 0;

    return (NSInteger)[filteredKeyboardShortcutRows count];
}

- (BOOL)tableView:(NSTableView *)tableView isGroupRow:(NSInteger)row
{
    (void)tableView;
    (void)row;
    return NO;
}

- (BOOL)tableView:(NSTableView *)tableView shouldSelectRow:(NSInteger)row
{
    NSDictionary *shortcutRow;

    if (tableView != keyboardShortcutsTableView)
        return YES;

    shortcutRow = [self keyboardShortcutRowAtIndex:row];
    return ![[shortcutRow objectForKey:QSSShortcutRowHeaderKey] boolValue];
}

- (CGFloat)tableView:(NSTableView *)tableView heightOfRow:(NSInteger)row
{
    NSDictionary *shortcutRow;

    if (tableView != keyboardShortcutsTableView)
        return 30.0f;

    shortcutRow = [self keyboardShortcutRowAtIndex:row];
    if ([[shortcutRow objectForKey:QSSShortcutRowHeaderKey] boolValue])
        return 44.0f;

    return 30.0f;
}

- (NSView *)tableView:(NSTableView *)tableView
   viewForTableColumn:(NSTableColumn *)tableColumn
                  row:(NSInteger)row
{
    NSDictionary *shortcutRow;
    BOOL header;
    NSString *columnIdentifier;
    NSString *identifier;
    NSTextField *field;
    QSSShortcutHeaderView *headerView;
    QSSShortcutKeysView *keysView;

    if (tableView != keyboardShortcutsTableView)
        return nil;

    shortcutRow = [self keyboardShortcutRowAtIndex:row];
    header = [[shortcutRow objectForKey:QSSShortcutRowHeaderKey] boolValue];
    columnIdentifier = [tableColumn identifier];
    identifier = header ?
        [NSString stringWithFormat:@"KeyboardShortcutHeaderCell.%@", columnIdentifier] :
        columnIdentifier;

    if (header && [columnIdentifier isEqualToString:QSSShortcutActionColumnIdentifier]) {
        headerView = [tableView makeViewWithIdentifier:identifier owner:self];
        if (!headerView) {
            headerView = [[[QSSShortcutHeaderView alloc] initWithFrame:NSZeroRect] autorelease];
            [headerView setIdentifier:identifier];
        }

        [headerView setTitle:QSSShortcutCapitalizedWords([shortcutRow objectForKey:QSSShortcutRowTitleKey])];
        return headerView;
    }

    if (!header && [columnIdentifier isEqualToString:QSSShortcutKeysColumnIdentifier]) {
        keysView = [tableView makeViewWithIdentifier:identifier owner:self];
        if (!keysView) {
            keysView = [[[QSSShortcutKeysView alloc] initWithFrame:NSZeroRect] autorelease];
            [keysView setIdentifier:identifier];
        }

        [keysView setShortcutKeysString:[shortcutRow objectForKey:QSSShortcutRowKeysKey]];
        return keysView;
    }

    field = [tableView makeViewWithIdentifier:identifier owner:self];
    if (!field)
        field = [self createShortcutLabelWithIdentifier:identifier];

    if (header) {
        [field setFont:[NSFont boldSystemFontOfSize:15.0f]];
        [field setTextColor:[NSColor labelColor]];
        [field setAlignment:NSTextAlignmentLeft];
        [field setStringValue:@""];
    } else {
        [field setFont:[NSFont systemFontOfSize:14.0f]];
        [field setTextColor:[NSColor labelColor]];
        [field setAlignment:NSTextAlignmentLeft];
        [field setStringValue:QSSShortcutCapitalizedWords([shortcutRow objectForKey:QSSShortcutRowActionKey])];
    }

    return field;
}

#pragma mark - Launcher window layout

- (NSView *)createCardWithFrame:(NSRect)frame
{
    NSView *card = [[[NSView alloc] initWithFrame:frame] autorelease];
    [card setWantsLayer:YES];
    [[card layer] setCornerRadius:16.0f];
    [[card layer] setBorderWidth:1.0f];
    [[card layer] setBackgroundColor:[QSSLauncherSurfaceColorForWindow(launcherWindow, 0.88f) CGColor]];
    [[card layer] setBorderColor:[QSSSeparatorColorForWindow(launcherWindow) CGColor]];
    return card;
}

- (NSTextField *)createStaticLabelWithText:(NSString *)text
                                      font:(NSFont *)font
                                     color:(NSColor *)color
                                     frame:(NSRect)frame
{
    NSTextField *field = [[[NSTextField alloc] initWithFrame:frame] autorelease];
    [field setBezeled:NO];
    [field setDrawsBackground:NO];
    [field setEditable:NO];
    [field setSelectable:NO];
    [field setFont:font];
    [field setTextColor:color];
    [field setStringValue:text ? text : @""];
    return field;
}

- (void)styleTextInput:(NSTextField *)textField
{
    CALayer *layer;

    if (!textField)
        return;

    [textField setCell:[[[QSSCenteredTextFieldCell alloc] initTextCell:@""] autorelease]];
    [textField setBezeled:NO];
    [textField setBordered:NO];
    [textField setDrawsBackground:NO];
    [textField setFocusRingType:NSFocusRingTypeNone];
    [textField setTextColor:[NSColor labelColor]];
    [textField setWantsLayer:YES];

    layer = [textField layer];
    [layer setCornerRadius:11.0f];
    [layer setBorderWidth:1.0f];
    [layer setBorderColor:[QSSSeparatorColorForWindow(launcherWindow) CGColor]];
    [layer setBackgroundColor:[QSSLauncherTextInputColorForWindow(launcherWindow, 0.96f) CGColor]];
}

- (void)styleFilledButton:(NSButton *)button
          backgroundColor:(NSColor *)backgroundColor
               titleColor:(NSColor *)titleColor
              borderColor:(NSColor *)borderColor
             cornerRadius:(CGFloat)cornerRadius
{
    NSDictionary *attributes;
    NSAttributedString *title;
    CALayer *layer;

    if (!button)
        return;

    attributes = @{
        NSForegroundColorAttributeName: titleColor ? titleColor : [NSColor labelColor],
        NSFontAttributeName: [NSFont systemFontOfSize:15.0f weight:NSFontWeightSemibold]
    };
    title = [[[NSAttributedString alloc] initWithString:[button title] attributes:attributes] autorelease];

    [button setBordered:NO];
    [button setButtonType:NSButtonTypeMomentaryChange];
    [button setAttributedTitle:title];
    [button setAttributedAlternateTitle:title];
    [button setFocusRingType:NSFocusRingTypeNone];
    [button setWantsLayer:YES];

    layer = [button layer];
    [layer setCornerRadius:cornerRadius];
    [layer setBackgroundColor:[backgroundColor CGColor]];
    [layer setBorderWidth:borderColor ? 1.0f : 0.0f];
    if (borderColor)
        [layer setBorderColor:[borderColor CGColor]];
}

- (NSControl *)createRawMouseSwitch
{
    if (@available(macOS 10.15, *)) {
        NSSwitch *sw = [[[NSSwitch alloc] initWithFrame:NSMakeRect(0.0f, 0.0f, 38.0f, 22.0f)] autorelease];
        [sw setTarget:self];
        [sw setAction:@selector(rawMouseSwitchToggled:)];
        return sw;
    }
    NSButton *cb = [[[NSButton alloc] initWithFrame:NSMakeRect(0.0f, 0.0f, 18.0f, 18.0f)] autorelease];
    [cb setButtonType:NSButtonTypeSwitch];
    [cb setTitle:@""];
    [cb setTarget:self];
    [cb setAction:@selector(rawMouseSwitchToggled:)];
    return cb;
}

- (void)setRawMouseSwitchOn:(BOOL)on
{
    NSInteger state = on ? NSControlStateValueOn : NSControlStateValueOff;
    if (@available(macOS 10.15, *)) {
        if ([rawMouseSwitch isKindOfClass:[NSSwitch class]]) {
            [(NSSwitch *)rawMouseSwitch setState:state];
            return;
        }
    }
    if ([rawMouseSwitch isKindOfClass:[NSButton class]])
        [(NSButton *)rawMouseSwitch setState:state];
}

- (BOOL)rawMouseSwitchIsOn
{
    if (@available(macOS 10.15, *)) {
        if ([rawMouseSwitch isKindOfClass:[NSSwitch class]])
            return [(NSSwitch *)rawMouseSwitch state] == NSControlStateValueOn;
    }
    if ([rawMouseSwitch isKindOfClass:[NSButton class]])
        return [(NSButton *)rawMouseSwitch state] == NSControlStateValueOn;
    return NO;
}

- (void)refreshRawMouseSwitchState
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    BOOL granted = QSSInputMonitoringIsGranted();
    BOOL remembered = [defaults boolForKey:QSSPrefRawMouseInputEnabledKey];

    if (granted) {
        rawMousePermissionPending = NO;
        remembered = YES;
        [defaults setBool:YES forKey:QSSPrefRawMouseInputEnabledKey];
        [defaults synchronize];
    }

    [self setRawMouseSwitchOn:(granted || rawMousePermissionPending || remembered)];

    if (granted)
        [self stopRawMousePermissionAssistant];
}

- (void)refreshRawMouseSwitchStateFromTimer:(NSTimer *)timer
{
    (void)timer;
    if (rawMouseStartupRefreshTimer)
    {
        [rawMouseStartupRefreshTimer release];
        rawMouseStartupRefreshTimer = nil;
    }
    [self refreshRawMouseSwitchState];
}

- (void)scheduleRawMouseStartupRefresh
{
    if (rawMouseStartupRefreshTimer)
    {
        [rawMouseStartupRefreshTimer invalidate];
        [rawMouseStartupRefreshTimer release];
        rawMouseStartupRefreshTimer = nil;
    }

    rawMouseStartupRefreshTimer = [[NSTimer scheduledTimerWithTimeInterval:0.35f
                                                                    target:self
                                                                  selector:@selector(refreshRawMouseSwitchStateFromTimer:)
                                                                  userInfo:nil
                                                                   repeats:NO] retain];
}

- (void)buildLaunchOptionsCard
{
    CGFloat cardW = NSWidth([launchOptionsCard bounds]);
    CGFloat cardH = NSHeight([launchOptionsCard bounds]);
    CGFloat pad = 16.0f;
    NSRect textFrame;
    NSRect addFrame;

    launchOptionsHeaderLabel = [self createStaticLabelWithText:@"Launch options"
                                                          font:[NSFont systemFontOfSize:13.0f weight:NSFontWeightSemibold]
                                                         color:[NSColor labelColor]
                                                         frame:NSMakeRect(pad, cardH - pad - 18.0f, cardW - pad * 2.0f, 18.0f)];
    [launchOptionsCard addSubview:launchOptionsHeaderLabel];

    CGFloat rowY = cardH - pad - 18.0f - 14.0f - 28.0f;
    CGFloat addWidth = 32.0f;
    CGFloat spacing = 8.0f;

    addFrame = NSMakeRect(cardW - pad - addWidth, rowY, addWidth, 28.0f);
    textFrame = NSMakeRect(pad, rowY, NSMinX(addFrame) - spacing - pad, 28.0f);

    argumentTextField = [[NSTextField alloc] initWithFrame:textFrame];
    [self styleTextInput:argumentTextField];
    [argumentTextField setFont:[NSFont systemFontOfSize:13.0f]];
    [argumentTextField setPlaceholderString:@"Example: -heapsize 256000 -game ctf"];
    [argumentTextField setEditable:YES];
    [argumentTextField setSelectable:YES];
    [argumentTextField setTarget:self];
    [argumentTextField setAction:@selector(argumentTextFieldEntered:)];
    [argumentTextField setDelegate:self];
    [launchOptionsCard addSubview:argumentTextField];

    argumentCompletionGhostLabel = [[QSSPassthroughTextField alloc] initWithFrame:textFrame];
    [argumentCompletionGhostLabel setCell:[[[QSSCenteredTextFieldCell alloc] initTextCell:@""] autorelease]];
    [argumentCompletionGhostLabel setBezeled:NO];
    [argumentCompletionGhostLabel setDrawsBackground:NO];
    [argumentCompletionGhostLabel setEditable:NO];
    [argumentCompletionGhostLabel setSelectable:NO];
    [argumentCompletionGhostLabel setFont:[argumentTextField font]];
    [argumentCompletionGhostLabel setTextColor:[[NSColor labelColor] colorWithAlphaComponent:0.32f]];
    [argumentCompletionGhostLabel setStringValue:@""];
    [argumentCompletionGhostLabel setHidden:YES];
    [launchOptionsCard addSubview:argumentCompletionGhostLabel
                       positioned:NSWindowAbove
                       relativeTo:argumentTextField];

    addArgumentButton = [[NSButton alloc] initWithFrame:addFrame];
    [addArgumentButton setTitle:@"+"];
    [addArgumentButton setFont:[NSFont systemFontOfSize:16.0f weight:NSFontWeightSemibold]];
    [addArgumentButton setTarget:self];
    [addArgumentButton setAction:@selector(addArgumentButtonPressed:)];
    [self styleFilledButton:addArgumentButton
            backgroundColor:QSSLauncherSurfaceColorForWindow(launcherWindow, 1.0f)
                 titleColor:QSSAboutLinkColor()
                borderColor:QSSSeparatorColorForWindow(launcherWindow)
               cornerRadius:11.0f];
    [launchOptionsCard addSubview:addArgumentButton];

    CGFloat helperY = 10.0f;
    helperLabel = [[self createStaticLabelWithText:QSSLaunchOptionsDefaultHelperText()
                                              font:[NSFont systemFontOfSize:11.0f]
                                             color:[NSColor secondaryLabelColor]
                                             frame:NSMakeRect(pad, helperY, cardW - pad * 2.0f, 16.0f)] retain];
    [launchOptionsCard addSubview:helperLabel];
    [helperLabel release];

    CGFloat chipsY = helperY + 16.0f + 6.0f;
    CGFloat chipsH = NSMinY(textFrame) - chipsY - 6.0f;
    if (chipsH < QSSChipHeight)
        chipsH = QSSChipHeight;

    chipsContainer = [[NSView alloc] initWithFrame:NSMakeRect(pad, chipsY, cardW - pad * 2.0f, chipsH)];
    [chipsContainer setWantsLayer:YES];
    [[chipsContainer layer] setBackgroundColor:[[NSColor clearColor] CGColor]];
    [launchOptionsCard addSubview:chipsContainer];
    [chipsContainer release];
}

- (void)buildSettingsCard
{
    CGFloat cardW = NSWidth([settingsCard bounds]);
    CGFloat cardH = NSHeight([settingsCard bounds]);
    CGFloat pad = 16.0f;
    NSTextField *header;
    NSTextField *titleLabel;
    NSTextField *subtitleLabel;
    NSRect switchFrame;

    header = [self createStaticLabelWithText:@"Settings"
                                        font:[NSFont systemFontOfSize:13.0f weight:NSFontWeightSemibold]
                                       color:[NSColor labelColor]
                                       frame:NSMakeRect(pad, cardH - pad - 18.0f, cardW - pad * 2.0f, 18.0f)];
    [settingsCard addSubview:header];

    rawMouseSwitch = [[self createRawMouseSwitch] retain];
    switchFrame = [rawMouseSwitch frame];
    switchFrame.origin.x = cardW - pad - QSSRawMouseSwitchRightInsetExtra - NSWidth(switchFrame);
    switchFrame.origin.y = (cardH - pad - 22.0f - NSHeight(switchFrame)) / 2.0f + 4.0f;
    [rawMouseSwitch setFrame:switchFrame];
    [settingsCard addSubview:rawMouseSwitch];

    CGFloat textMaxX = NSMinX(switchFrame) - 10.0f;
    CGFloat textWidth = textMaxX - pad;
    CGFloat titleY = NSMidY(switchFrame) + 2.0f;
    CGFloat subtitleY = NSMidY(switchFrame) - 16.0f;

    titleLabel = [self createStaticLabelWithText:@"RAW Mouse Input"
                                            font:[NSFont systemFontOfSize:13.0f weight:NSFontWeightMedium]
                                           color:[NSColor labelColor]
                                           frame:NSMakeRect(pad, titleY, textWidth, 18.0f)];
    [settingsCard addSubview:titleLabel];

    subtitleLabel = [self createStaticLabelWithText:@"Improves input responsiveness in-game."
                                               font:[NSFont systemFontOfSize:11.0f]
                                              color:[NSColor secondaryLabelColor]
                                              frame:NSMakeRect(pad, subtitleY, textWidth, 16.0f)];
    [settingsCard addSubview:subtitleLabel];

    [self refreshRawMouseSwitchState];
}

- (void)buildLauncherUI
{
    NSView *contentView;
    CGFloat inset = QSSLauncherCardInset;
    CGFloat cardsWidth;
    CGFloat launchCardH = QSSLaunchOptionsBaseHeight;
    CGFloat settingsCardH = QSSSettingsCardHeight;
    CGFloat titleY;
    CGFloat subtitleY;
    CGFloat launchCardY;
    CGFloat settingsCardY;
    CGFloat buttonY = 22.0f;
    CGFloat buttonH = 40.0f;

    if (!launcherWindow)
        return;

    contentView = [launcherWindow contentView];
    cardsWidth = QSSLauncherWindowWidth - inset * 2.0f;

    titleY = QSSLauncherWindowHeight - inset - 34.0f;
    subtitleY = titleY - 22.0f;
    launchCardY = subtitleY - 16.0f - launchCardH;
    settingsCardY = launchCardY - 12.0f - settingsCardH;

    launcherTitleLabel = [self createStaticLabelWithText:@"Launch Quake"
                                                    font:[NSFont systemFontOfSize:26.0f weight:NSFontWeightBold]
                                                   color:[NSColor labelColor]
                                                   frame:NSMakeRect(inset, titleY, cardsWidth, 34.0f)];
    [contentView addSubview:launcherTitleLabel];

    launcherSubtitleLabel = [self createStaticLabelWithText:@"Optional startup settings before launching."
                                                       font:[NSFont systemFontOfSize:12.5f]
                                                      color:[NSColor secondaryLabelColor]
                                                      frame:NSMakeRect(inset, subtitleY, cardsWidth, 18.0f)];
    [contentView addSubview:launcherSubtitleLabel];

    launchOptionsCard = [[self createCardWithFrame:NSMakeRect(inset, launchCardY, cardsWidth, launchCardH)] retain];
    [contentView addSubview:launchOptionsCard];
    [launchOptionsCard release];
    [self buildLaunchOptionsCard];

    settingsCard = [[self createCardWithFrame:NSMakeRect(inset, settingsCardY, cardsWidth, settingsCardH)] retain];
    [contentView addSubview:settingsCard];
    [settingsCard release];
    [self buildSettingsCard];

    launchButton = [[NSButton alloc] initWithFrame:NSMakeRect(0.0f, buttonY, 144.0f, buttonH)];
    [launchButton setTitle:@"Launch"];
    [launchButton setKeyEquivalent:@"\r"];
    [launchButton setTarget:self];
    [launchButton setAction:@selector(launchQuake:)];
    [self styleFilledButton:launchButton
            backgroundColor:QSSAboutLinkColor()
                 titleColor:[NSColor whiteColor]
                borderColor:nil
               cornerRadius:13.0f];
    {
        NSRect f = [launchButton frame];
        f.origin.x = QSSLauncherWindowWidth - inset - NSWidth(f);
        [launchButton setFrame:f];
    }
    [contentView addSubview:launchButton];

    cancelButton = [[NSButton alloc] initWithFrame:NSMakeRect(0.0f, buttonY, 126.0f, buttonH)];
    [cancelButton setTitle:@"Quit"];
    [cancelButton setKeyEquivalent:@"\033"];
    [cancelButton setTarget:self];
    [cancelButton setAction:@selector(cancel:)];
    [self styleFilledButton:cancelButton
            backgroundColor:QSSLauncherSurfaceColorForWindow(launcherWindow, 1.0f)
                 titleColor:[NSColor labelColor]
                borderColor:QSSSeparatorColorForWindow(launcherWindow)
               cornerRadius:13.0f];
    {
        NSRect f = [cancelButton frame];
        f.origin.x = NSMinX([launchButton frame]) - 16.0f - NSWidth(f);
        [cancelButton setFrame:f];
    }
    [contentView addSubview:cancelButton];

    [self reflowChips];
    [self refreshLauncherAppearance];
}

- (void)configureLauncherWindow
{
    NSView *contentView;
    NSWindowStyleMask style;

    if (!launcherWindow)
        return;

    [launcherWindow setTitle:@"QSS-M"];
    [launcherWindow setContentSize:NSMakeSize(QSSLauncherWindowWidth, QSSLauncherWindowHeight)];

    style = [launcherWindow styleMask];
    style |= NSWindowStyleMaskTitled;
    style |= NSWindowStyleMaskFullSizeContentView;
    style &= ~NSWindowStyleMaskClosable;
    style &= ~NSWindowStyleMaskMiniaturizable;
    style &= ~NSWindowStyleMaskResizable;
    [launcherWindow setStyleMask:style];

    [launcherWindow setTitleVisibility:NSWindowTitleHidden];
    [launcherWindow setTitlebarAppearsTransparent:YES];
    [launcherWindow setMovableByWindowBackground:YES];
    [launcherWindow setBackgroundColor:[NSColor clearColor]];
    [launcherWindow setOpaque:NO];

    contentView = [launcherWindow contentView];
    if (![contentView isKindOfClass:[QSSAppearanceView class]]) {
        QSSAppearanceView *appearanceView = [[[QSSAppearanceView alloc] initWithFrame:[contentView frame]] autorelease];
        [appearanceView setAutoresizingMask:[contentView autoresizingMask]];
        [launcherWindow setContentView:appearanceView];
        contentView = appearanceView;
    }
    [(QSSAppearanceView *)contentView setAppearanceOwner:self action:@selector(launcherAppearanceDidChange:)];

    [contentView setWantsLayer:YES];
    [[contentView layer] setCornerRadius:18.0f];
    [[contentView layer] setMasksToBounds:YES];
    [[contentView layer] setBorderWidth:1.0f];
    [[contentView layer] setBackgroundColor:[QSSLauncherWindowBackgroundColorForWindow(launcherWindow) CGColor]];
    [[contentView layer] setBorderColor:[QSSSeparatorColorForWindow(launcherWindow) CGColor]];
}

- (void)launcherAppearanceDidChange:(id)sender
{
    (void)sender;
    [self refreshLauncherAppearance];
}

- (void)refreshLauncherAppearance
{
    NSWindow *window = launcherWindow;
    NSView *contentView = [launcherWindow contentView];

    if (contentView && [contentView layer]) {
        [[contentView layer] setBackgroundColor:[QSSLauncherWindowBackgroundColorForWindow(window) CGColor]];
        [[contentView layer] setBorderColor:[QSSSeparatorColorForWindow(window) CGColor]];
    }

    if (launchOptionsCard && [launchOptionsCard layer]) {
        [[launchOptionsCard layer] setBackgroundColor:[QSSLauncherSurfaceColorForWindow(window, 0.88f) CGColor]];
        [[launchOptionsCard layer] setBorderColor:[QSSSeparatorColorForWindow(window) CGColor]];
    }

    if (settingsCard && [settingsCard layer]) {
        [[settingsCard layer] setBackgroundColor:[QSSLauncherSurfaceColorForWindow(window, 0.88f) CGColor]];
        [[settingsCard layer] setBorderColor:[QSSSeparatorColorForWindow(window) CGColor]];
    }

    if (argumentTextField && [argumentTextField layer]) {
        [argumentTextField setTextColor:[NSColor labelColor]];
        [[argumentTextField layer] setBackgroundColor:[QSSLauncherTextInputColorForWindow(window, 0.96f) CGColor]];
        [[argumentTextField layer] setBorderColor:[QSSSeparatorColorForWindow(window) CGColor]];
    }

    if (argumentCompletionGhostLabel)
        [argumentCompletionGhostLabel setTextColor:[[NSColor labelColor] colorWithAlphaComponent:0.32f]];

    if (launcherTitleLabel)
        [launcherTitleLabel setTextColor:[NSColor labelColor]];
    if (launcherSubtitleLabel)
        [launcherSubtitleLabel setTextColor:[NSColor secondaryLabelColor]];
    if (launchOptionsHeaderLabel)
        [launchOptionsHeaderLabel setTextColor:[NSColor labelColor]];
    if (helperLabel)
        [helperLabel setTextColor:[NSColor secondaryLabelColor]];

    if (addArgumentButton) {
        [self styleFilledButton:addArgumentButton
                backgroundColor:QSSLauncherSurfaceColorForWindow(window, 1.0f)
                     titleColor:QSSAboutLinkColor()
                    borderColor:QSSSeparatorColorForWindow(window)
                   cornerRadius:11.0f];
    }

    if (launchButton) {
        [self styleFilledButton:launchButton
                backgroundColor:QSSAboutLinkColor()
                     titleColor:[NSColor whiteColor]
                    borderColor:nil
                   cornerRadius:13.0f];
    }

    if (cancelButton) {
        [self styleFilledButton:cancelButton
                backgroundColor:QSSLauncherSurfaceColorForWindow(window, 1.0f)
                     titleColor:[NSColor labelColor]
                    borderColor:QSSSeparatorColorForWindow(window)
                   cornerRadius:13.0f];
    }

    for (QSSArgumentChipView *chip in argumentChips)
        [chip refreshAppearance];
}

- (void)layoutLauncherWindowForChipRows:(NSUInteger)rows chipContainerHeight:(CGFloat)containerH
{
    CGFloat inset = QSSLauncherCardInset;
    CGFloat titleHeight = 34.0f;
    CGFloat subtitleHeight = 18.0f;
    CGFloat subtitleGap = 4.0f;
    CGFloat buttonHeight = 40.0f;
    CGFloat buttonGap = 12.0f;
    CGFloat bottomInset = 18.0f;
    CGFloat cardsWidth = QSSLauncherWindowWidth - inset * 2.0f;
    CGFloat launchCardHeight = QSSLaunchOptionsBaseHeight + (rows > 0 ? containerH + 10.0f : 0.0f);
    CGFloat contentHeight = inset + titleHeight + subtitleGap + subtitleHeight + 16.0f +
        launchCardHeight + 12.0f + QSSSettingsCardHeight + buttonGap + buttonHeight + bottomInset;
    CGFloat titleY;
    CGFloat subtitleY;
    CGFloat launchCardY;
    CGFloat settingsCardY;
    CGFloat buttonY;
    NSRect windowFrame;
    NSRect contentFrame;
    NSRect newWindowFrame;
    CGFloat pad = 16.0f;
    CGFloat addWidth = 32.0f;
    CGFloat spacing = 8.0f;
    CGFloat rowY;
    NSRect headerFrame;
    NSRect addFrame;
    NSRect textFieldFrame;
    NSRect helperFrame;
    NSRect chipsFrame;
    NSRect settingsFrame;
    NSRect launchButtonFrame;
    NSRect cancelButtonFrame;

    if (!launcherWindow || !launchOptionsCard || !settingsCard || !launchButton || !cancelButton ||
        !launcherTitleLabel || !launcherSubtitleLabel || !launchOptionsHeaderLabel ||
        !argumentTextField || !addArgumentButton || !helperLabel || !chipsContainer)
        return;

    windowFrame = [launcherWindow frame];
    contentFrame = [launcherWindow contentRectForFrameRect:windowFrame];
    if (fabs(NSHeight(contentFrame) - contentHeight) > 0.5f) {
        newWindowFrame = [launcherWindow frameRectForContentRect:
                          NSMakeRect(0.0f, 0.0f, QSSLauncherWindowWidth, contentHeight)];
        newWindowFrame.origin.x = windowFrame.origin.x;
        newWindowFrame.origin.y = NSMaxY(windowFrame) - NSHeight(newWindowFrame);
        [launcherWindow setFrame:newWindowFrame display:YES];
    }

    titleY = contentHeight - inset - titleHeight;
    subtitleY = titleY - subtitleGap - subtitleHeight;
    launchCardY = subtitleY - 16.0f - launchCardHeight;
    settingsCardY = launchCardY - 12.0f - QSSSettingsCardHeight;
    buttonY = settingsCardY - buttonGap - buttonHeight;

    [launcherTitleLabel setFrame:NSMakeRect(inset, titleY, cardsWidth, titleHeight)];
    [launcherSubtitleLabel setFrame:NSMakeRect(inset, subtitleY, cardsWidth, subtitleHeight)];
    [launchOptionsCard setFrame:NSMakeRect(inset, launchCardY, cardsWidth, launchCardHeight)];

    headerFrame = NSMakeRect(pad, launchCardHeight - pad - 18.0f, cardsWidth - pad * 2.0f, 18.0f);
    [launchOptionsHeaderLabel setFrame:headerFrame];

    rowY = launchCardHeight - pad - 18.0f - 14.0f - 28.0f;
    addFrame = NSMakeRect(cardsWidth - pad - addWidth, rowY, addWidth, 28.0f);
    textFieldFrame = NSMakeRect(pad, rowY, NSMinX(addFrame) - spacing - pad, 28.0f);
    [argumentTextField setFrame:textFieldFrame];
    [addArgumentButton setFrame:addFrame];

    helperFrame = NSMakeRect(pad, 10.0f, cardsWidth - pad * 2.0f, 16.0f);
    [helperLabel setFrame:helperFrame];

    chipsFrame = NSMakeRect(pad, NSMaxY(helperFrame) + 10.0f, cardsWidth - pad * 2.0f, containerH);
    [chipsContainer setFrame:chipsFrame];
    [chipsContainer setHidden:(rows == 0)];

    settingsFrame = [settingsCard frame];
    settingsFrame.origin.x = inset;
    settingsFrame.origin.y = settingsCardY;
    settingsFrame.size.width = cardsWidth;
    settingsFrame.size.height = QSSSettingsCardHeight;
    [settingsCard setFrame:settingsFrame];

    launchButtonFrame = [launchButton frame];
    launchButtonFrame.origin.y = buttonY;
    launchButtonFrame.origin.x = QSSLauncherWindowWidth - inset - NSWidth(launchButtonFrame);
    [launchButton setFrame:launchButtonFrame];

    cancelButtonFrame = [cancelButton frame];
    cancelButtonFrame.origin.y = buttonY;
    cancelButtonFrame.origin.x = NSMinX(launchButtonFrame) - 16.0f - NSWidth(cancelButtonFrame);
    [cancelButton setFrame:cancelButtonFrame];
}

#pragma mark - Chip management

- (void)reflowChips
{
    CGFloat spacingX = 6.0f;
    CGFloat spacingY = 6.0f;
    CGFloat maxW;
    CGFloat cursorX = 0.0f;
    CGFloat lineY;
    CGFloat containerH = 0.0f;
    NSUInteger rows = 0;
    CGFloat rowTopY;
    NSRect chipsFrame;

    if (!chipsContainer || !helperLabel || !argumentTextField || !launchOptionsCard)
        return;

    maxW = NSWidth([chipsContainer bounds]);

    for (QSSArgumentChipView *chip in argumentChips) {
        NSRect f = [chip frame];
        if (rows == 0) {
            rows = 1;
        } else if (cursorX > 0.0f && cursorX + NSWidth(f) > maxW) {
            cursorX = 0.0f;
            rows++;
        }
        cursorX += NSWidth(f) + spacingX;
    }

    if (rows > 0)
        containerH = (rows * QSSChipHeight) + ((rows - 1) * spacingY);

    [self layoutLauncherWindowForChipRows:rows chipContainerHeight:containerH];

    chipsFrame = [chipsContainer frame];
    maxW = NSWidth(chipsFrame);

    cursorX = 0.0f;
    rowTopY = containerH - QSSChipHeight;
    lineY = rowTopY;

    for (QSSArgumentChipView *chip in argumentChips) {
        NSRect f = [chip frame];
        if (cursorX > 0.0f && cursorX + NSWidth(f) > maxW) {
            cursorX = 0.0f;
            lineY -= (QSSChipHeight + spacingY);
            rows++;
        }
        f.origin.x = cursorX;
        f.origin.y = lineY;
        [chip setFrame:f];
        cursorX += NSWidth(f) + spacingX;
    }
}

- (void)addChipWithString:(NSString *)str
{
    QSSArgumentChipView *chip;
    NSString *trimmed;

    if (!str)
        return;
    trimmed = [str stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([trimmed length] == 0)
        return;

    chip = [[[QSSArgumentChipView alloc] initWithArgument:trimmed
                                                    owner:self
                                             removeAction:@selector(chipRemoveRequested:)] autorelease];
    [argumentChips addObject:chip];
    [chipsContainer addSubview:chip];
    [self reflowChips];
    [self syncArgumentTextFieldFromChips];
}

- (void)removeAllChips
{
    for (QSSArgumentChipView *chip in argumentChips)
        [chip removeFromSuperview];
    [argumentChips removeAllObjects];
}

- (void)syncArgumentTextFieldFromChips
{
    NSString *joined;
    NSText *editor;

    if (!argumentTextField || suppressArgumentTextSync)
        return;

    joined = [self joinedArgumentString];
    [argumentTextField setStringValue:joined];

    editor = [self activeArgumentFieldEditor];
    if (editor) {
        [editor setString:joined];
        [editor setSelectedRange:NSMakeRange([joined length], 0)];
    }

    [self updateArgumentCompletionHint];
}

- (void)rebuildChipsFromString:(NSString *)line syncTextField:(BOOL)syncTextField
{
    NSArray *strings;
    BOOL oldSuppressArgumentTextSync = suppressArgumentTextSync;

    suppressArgumentTextSync = !syncTextField;
    [self removeAllChips];
    strings = [self chipStringsFromCommandLine:line];
    for (NSString *s in strings)
        [self addChipWithString:s];
    suppressArgumentTextSync = oldSuppressArgumentTextSync;

    [self reflowChips];
    if (syncTextField)
        [self syncArgumentTextFieldFromChips];
}

- (void)rebuildChipsFromString:(NSString *)line
{
    [self rebuildChipsFromString:line syncTextField:YES];
}

- (void)chipRemoveRequested:(QSSArgumentChipView *)chip
{
    if (!chip)
        return;
    [chip removeFromSuperview];
    [argumentChips removeObject:chip];
    [self reflowChips];
    [self syncArgumentTextFieldFromChips];
}

#pragma mark - Text field / add actions

- (void)argumentTextFieldEntered:(id)sender
{
    NSString *value;

    (void)sender;
    if (!argumentTextField)
        return;
    value = [[self currentArgumentText] stringByTrimmingCharactersInSet:
             [NSCharacterSet whitespaceAndNewlineCharacterSet]];

    if ([value length] == 0) {
        [self removeAllChips];
        [self reflowChips];
        [self syncArgumentTextFieldFromChips];
        [self updateArgumentCompletionHint];
        return;
    }

    [self rebuildChipsFromString:value];
    [self updateArgumentCompletionHint];
}

- (NSText *)activeArgumentFieldEditor
{
    NSWindow *window;
    NSText *editor;

    if (!argumentTextField)
        return nil;

    window = [argumentTextField window];
    if (!window)
        return nil;

    editor = [window fieldEditor:NO forObject:argumentTextField];
    if (editor && [window firstResponder] == editor)
        return editor;

    return nil;
}

- (NSString *)currentArgumentText
{
    NSText *editor = [self activeArgumentFieldEditor];
    NSString *text;

    if (editor) {
        text = [editor string];
        return text ? text : @"";
    }

    text = [argumentTextField stringValue];
    return text ? text : @"";
}

- (NSRange)argumentCompletionTokenRangeForText:(NSString *)text cursor:(NSUInteger)cursor
{
    NSCharacterSet *ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    NSUInteger length = [text length];
    NSUInteger start;
    NSUInteger end;

    if (cursor > length)
        cursor = length;

    start = cursor;
    while (start > 0 && ![ws characterIsMember:[text characterAtIndex:start - 1]])
        start--;

    end = cursor;
    while (end < length && ![ws characterIsMember:[text characterAtIndex:end]])
        end++;

    return NSMakeRange(start, end - start);
}

- (NSString *)argumentCompletionForText:(NSString *)text tokenRange:(NSRange *)tokenRangeOut
{
    NSText *editor;
    NSRange selectedRange;
    NSRange tokenRange;
    NSString *partial;

    if (!text)
        text = [self currentArgumentText];

    selectedRange = NSMakeRange([text length], 0);
    editor = [self activeArgumentFieldEditor];
    if (editor)
        selectedRange = [editor selectedRange];

    tokenRange = [self argumentCompletionTokenRangeForText:text cursor:selectedRange.location];
    if (tokenRangeOut)
        *tokenRangeOut = tokenRange;
    if (tokenRange.length < 2)
        return nil;

    partial = [text substringWithRange:tokenRange];
    if (!([partial hasPrefix:@"-"] || [partial hasPrefix:@"+"]))
        return nil;

    for (size_t i = 0; i < sizeof(QSSPresetArgumentEntries) / sizeof(QSSPresetArgumentEntries[0]); i++) {
        NSString *insert = QSSPresetArgumentEntries[i].insert;
        NSString *candidate;
        if (!insert)
            continue;

        candidate = [insert stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([candidate length] == 0)
            continue;

        if ([candidate caseInsensitiveCompare:partial] == NSOrderedSame)
            return [insert hasSuffix:@" "] ? insert : nil;
    }

    for (size_t i = 0; i < sizeof(QSSPresetArgumentEntries) / sizeof(QSSPresetArgumentEntries[0]); i++) {
        NSString *insert = QSSPresetArgumentEntries[i].insert;
        NSString *candidate;
        if (!insert)
            continue;

        candidate = [insert stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([candidate length] == 0)
            continue;

        if ([candidate rangeOfString:partial options:(NSAnchoredSearch | NSCaseInsensitiveSearch)].location == NSNotFound)
            continue;

        if ([candidate length] > [partial length])
            return insert;
    }

    return nil;
}

- (void)hideArgumentCompletionGhost
{
    if (!argumentCompletionGhostLabel)
        return;

    [argumentCompletionGhostLabel setStringValue:@""];
    [argumentCompletionGhostLabel setHidden:YES];
}

- (void)updateArgumentCompletionGhostWithText:(NSString *)text
                                   completion:(NSString *)completion
                                   tokenRange:(NSRange)tokenRange
{
    static const CGFloat textInset = 8.0f;
    NSText *editor;
    NSRange selectedRange;
    NSString *partial;
    NSString *suffix;
    NSString *trailingText;
    NSCharacterSet *ws;
    NSFont *font;
    NSDictionary *attributes;
    CGFloat prefixWidth;
    NSRect fieldFrame;
    CGFloat hintX;
    CGFloat maxX;

    if (!argumentCompletionGhostLabel || !argumentTextField || !text || !completion) {
        [self hideArgumentCompletionGhost];
        return;
    }

    editor = [self activeArgumentFieldEditor];
    if (!editor) {
        [self hideArgumentCompletionGhost];
        return;
    }

    selectedRange = [editor selectedRange];
    if (selectedRange.length != 0 ||
        tokenRange.location == NSNotFound ||
        NSMaxRange(tokenRange) > [text length] ||
        selectedRange.location != NSMaxRange(tokenRange))
    {
        [self hideArgumentCompletionGhost];
        return;
    }

    ws = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    trailingText = [text substringFromIndex:selectedRange.location];
    if ([[trailingText stringByTrimmingCharactersInSet:ws] length] != 0) {
        [self hideArgumentCompletionGhost];
        return;
    }

    partial = [text substringWithRange:tokenRange];
    if ([completion length] <= [partial length]) {
        [self hideArgumentCompletionGhost];
        return;
    }

    suffix = [completion substringFromIndex:[partial length]];
    if ([[suffix stringByTrimmingCharactersInSet:ws] length] == 0) {
        [self hideArgumentCompletionGhost];
        return;
    }

    font = [argumentTextField font];
    if (!font)
        font = [NSFont systemFontOfSize:13.0f];

    attributes = @{ NSFontAttributeName: font };
    prefixWidth = ceilf([[text substringToIndex:NSMaxRange(tokenRange)] sizeWithAttributes:attributes].width);
    fieldFrame = [argumentTextField frame];
    hintX = NSMinX(fieldFrame) + textInset + prefixWidth;
    maxX = NSMaxX(fieldFrame) - textInset;

    if (hintX >= maxX) {
        [self hideArgumentCompletionGhost];
        return;
    }

    [argumentCompletionGhostLabel setFont:font];
    [argumentCompletionGhostLabel setStringValue:suffix];
    [argumentCompletionGhostLabel setFrame:NSMakeRect(hintX - textInset,
                                                      NSMinY(fieldFrame),
                                                      maxX - hintX + textInset,
                                                      NSHeight(fieldFrame))];
    [[argumentCompletionGhostLabel superview] addSubview:argumentCompletionGhostLabel
                                              positioned:NSWindowAbove
                                              relativeTo:nil];
    [argumentCompletionGhostLabel setHidden:NO];
}

- (void)updateArgumentCompletionHint
{
    NSString *completion;
    NSString *display;
    NSString *hint;
    NSString *text;
    NSRange tokenRange = NSMakeRange(NSNotFound, 0);

    if (!helperLabel || !argumentTextField)
        return;

    text = [self currentArgumentText];
    completion = [self argumentCompletionForText:text tokenRange:&tokenRange];
    if (!completion) {
        [self hideArgumentCompletionGhost];
        [helperLabel setStringValue:QSSLaunchOptionsDefaultHelperText()];
        return;
    }

    [self updateArgumentCompletionGhostWithText:text completion:completion tokenRange:tokenRange];

    display = [completion stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    hint = [completion hasSuffix:@" "]
        ? [NSString stringWithFormat:@"Tab completes %@; type a value after it.", display]
        : [NSString stringWithFormat:@"Tab completes %@.", display];
    [helperLabel setStringValue:hint];
}

- (BOOL)acceptArgumentCompletion
{
    NSString *text;
    NSString *completion;
    NSMutableString *newText;
    NSRange tokenRange;
    NSUInteger cursor;
    NSText *editor;

    if (!argumentTextField)
        return NO;

    text = [self currentArgumentText];
    completion = [self argumentCompletionForText:text tokenRange:&tokenRange];
    if (!completion)
        return NO;

    newText = [[text mutableCopy] autorelease];
    [newText replaceCharactersInRange:tokenRange withString:completion];
    cursor = tokenRange.location + [completion length];

    [argumentTextField setStringValue:newText];
    [[argumentTextField window] makeFirstResponder:argumentTextField];
    editor = [[argumentTextField window] fieldEditor:YES forObject:argumentTextField];
    [editor setString:newText];
    [editor setSelectedRange:NSMakeRange(cursor, 0)];

    [self rebuildChipsFromString:newText syncTextField:NO];
    [self updateArgumentCompletionHint];
    return YES;
}

- (BOOL)control:(NSControl *)control
       textView:(NSTextView *)textView
doCommandBySelector:(SEL)commandSelector
{
    if (control == argumentTextField && commandSelector == @selector(insertTab:))
        return [self acceptArgumentCompletion];

    (void)textView;

    if (control == argumentTextField &&
        (commandSelector == @selector(insertNewline:) ||
         commandSelector == @selector(insertNewlineIgnoringFieldEditor:)))
    {
        NSString *value = [[self currentArgumentText] stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([value length] == 0)
            return NO;

        [self argumentTextFieldEntered:argumentTextField];
        return YES;
    }

    return NO;
}

- (void)controlTextDidEndEditing:(NSNotification *)notification
{
    NSString *value;
    NSString *current;

    if ([notification object] != argumentTextField)
        return;

    value = [[self currentArgumentText] stringByTrimmingCharactersInSet:
             [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    current = [self joinedArgumentString];

    if (![value isEqualToString:current])
        [self argumentTextFieldEntered:argumentTextField];
    else
        [self syncArgumentTextFieldFromChips];
    [self updateArgumentCompletionHint];
}

- (void)controlTextDidChange:(NSNotification *)notification
{
    NSString *value;

    if ([notification object] == keyboardShortcutsSearchField) {
        [self filterKeyboardShortcuts];
        return;
    }

    if ([notification object] != argumentTextField)
        return;

    value = [self currentArgumentText];
    [self rebuildChipsFromString:value syncTextField:NO];
    [self updateArgumentCompletionHint];
}

- (NSMenu *)buildPresetArgumentsMenu
{
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    [menu setAutoenablesItems:NO];

    for (size_t i = 0; i < sizeof(QSSPresetArgumentEntries) / sizeof(QSSPresetArgumentEntries[0]); i++) {
        NSMenuItem *item;
        if (!QSSPresetArgumentEntries[i].title) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        item = [[[NSMenuItem alloc] initWithTitle:QSSPresetArgumentEntries[i].title
                                           action:@selector(presetMenuSelected:)
                                    keyEquivalent:@""] autorelease];
        [item setTarget:self];
        [item setRepresentedObject:QSSPresetArgumentEntries[i].insert];
        [item setEnabled:YES];
        [menu addItem:item];
    }

    return menu;
}

- (void)presetMenuSelected:(id)sender
{
    NSMenuItem *item;
    NSString *insert;
    NSString *trimmed;

    if (![sender isKindOfClass:[NSMenuItem class]])
        return;

    item = (NSMenuItem *)sender;
    insert = [item representedObject];
    if (!insert)
        return;

    trimmed = [insert stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([insert hasSuffix:@" "] && ![trimmed isEqualToString:insert]) {
        NSString *current = [[self currentArgumentText] stringByTrimmingCharactersInSet:
                             [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        NSString *newText = ([current length] > 0)
            ? [NSString stringWithFormat:@"%@ %@", current, insert]
            : insert;

        // Preset expects a value; drop it into the text field so user can type one.
        [argumentTextField setStringValue:newText];
        [self rebuildChipsFromString:newText syncTextField:NO];
        [self updateArgumentCompletionHint];
        [[argumentTextField window] makeFirstResponder:argumentTextField];
        NSText *editor = [[argumentTextField window] fieldEditor:YES forObject:argumentTextField];
        [editor setString:newText];
        [editor setSelectedRange:NSMakeRange([newText length], 0)];
        return;
    }

    [self addChipWithString:trimmed];
}

- (void)addArgumentButtonPressed:(id)sender
{
    NSString *typed;
    NSString *current;
    (void)sender;

    typed = [[self currentArgumentText] stringByTrimmingCharactersInSet:
             [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    current = [self joinedArgumentString];

    if ([typed length] > 0 && ![typed isEqualToString:current]) {
        [self argumentTextFieldEntered:argumentTextField];
        return;
    }

    {
        NSRect f = [addArgumentButton frame];
        NSPoint loc = NSMakePoint(0.0f, NSMinY(f) - 2.0f);
        if (!presetArgumentsMenu)
            presetArgumentsMenu = [[self buildPresetArgumentsMenu] retain];
        [presetArgumentsMenu popUpMenuPositioningItem:nil atLocation:loc inView:addArgumentButton];
    }
}

#pragma mark - Help assistant / overlay

- (BOOL)openMacOSInstructionsPage
{
    NSURL *instructionsURL = [[NSBundle mainBundle] URLForResource:@"macos_instructions"
                                                     withExtension:@"html"];
    if (!instructionsURL)
        return NO;

    return [[NSWorkspace sharedWorkspace] openURL:instructionsURL];
}

- (BOOL)rawMouseOverlayShouldShowDragSource
{
    if (!QSSSupportsInputMonitoring() || QSSInputMonitoringIsGranted())
        return NO;
    return !rawMouseDragCompleted;
}

- (void)refreshRawMouseOverlayContent
{
    BOOL showDragSource;

    if (!rawMouseOverlayTitleField)
        return;

    showDragSource = [self rawMouseOverlayShouldShowDragSource];

    if (showDragSource)
    {
        [rawMouseOverlayTitleField setFrame:NSMakeRect(66.0f, 68.0f, 438.0f, 22.0f)];
        [rawMouseOverlayTitleField setFont:[NSFont systemFontOfSize:14.0f weight:NSFontWeightMedium]];
        [rawMouseOverlayTitleField setStringValue:[NSString stringWithFormat:
            @"Drag %@ to the list above to allow RAW Mouse Input", QSSHostAppDisplayName()]];
        [rawMouseOverlayArrowField setFrame:NSMakeRect(28.0f, 62.0f, 36.0f, 36.0f)];
        [rawMouseOverlayDragSourceView setHidden:NO];
    }
    else
    {
        [rawMouseOverlayTitleField setFrame:NSMakeRect(66.0f, 44.0f, 438.0f, 34.0f)];
        [rawMouseOverlayTitleField setFont:[NSFont systemFontOfSize:14.0f weight:NSFontWeightMedium]];
        [rawMouseOverlayTitleField setStringValue:@"Enable QSS-M in the list above, then choose Quit & Reopen if macOS asks."];
        [rawMouseOverlayArrowField setFrame:NSMakeRect(28.0f, 42.0f, 36.0f, 36.0f)];
        [rawMouseOverlayDragSourceView setHidden:YES];
    }
}

- (void)rawMousePermissionDragDidComplete
{
    rawMouseDragCompleted = YES;
    rawMousePermissionPending = YES;
    [[NSUserDefaults standardUserDefaults] setBool:YES forKey:QSSPrefRawMouseInputEnabledKey];
    [[NSUserDefaults standardUserDefaults] synchronize];
    [self refreshRawMouseSwitchState];
    if (rawMouseOverlayWindow)
        [rawMouseOverlayWindow orderOut:nil];
    rawMouseOverlayPresented = NO;
}

- (void)createRawMouseOverlayWindowIfNeeded
{
    NSPanel *panel;
    NSVisualEffectView *materialView;
    NSRect bounds;
    QSSAppDragSourceView *dragSourceView;
    __block AppController *blockSelf;

    if (rawMouseOverlayWindow)
        return;

    panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0.0f, 0.0f,
            QSSRawMouseOverlayWidth, QSSRawMouseOverlayHeight)
                                       styleMask:(NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel)
                                         backing:NSBackingStoreBuffered
                                           defer:NO];
    [panel setOpaque:NO];
    [panel setBackgroundColor:[NSColor clearColor]];
    [panel setHasShadow:YES];
    [panel setHidesOnDeactivate:NO];
    [panel setFloatingPanel:YES];
    [panel setLevel:NSStatusWindowLevel];
    [panel setCollectionBehavior:(NSWindowCollectionBehaviorCanJoinAllSpaces |
        NSWindowCollectionBehaviorStationary |
        NSWindowCollectionBehaviorIgnoresCycle)];

    bounds = NSMakeRect(0.0f, 0.0f, QSSRawMouseOverlayWidth, QSSRawMouseOverlayHeight);
    materialView = [[[NSVisualEffectView alloc] initWithFrame:bounds] autorelease];
    [materialView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [materialView setMaterial:NSVisualEffectMaterialPopover];
    [materialView setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
    [materialView setState:NSVisualEffectStateActive];
    [materialView setWantsLayer:YES];
    [[materialView layer] setCornerRadius:18.0f];
    [[materialView layer] setMasksToBounds:YES];
    [[materialView layer] setBorderWidth:0.5f];
    [[materialView layer] setBorderColor:[QSSSeparatorColorForWindow(panel) CGColor]];

    rawMouseOverlayArrowField = [[NSTextField alloc] initWithFrame:NSMakeRect(28.0f, 62.0f, 36.0f, 36.0f)];
    [rawMouseOverlayArrowField setBezeled:NO];
    [rawMouseOverlayArrowField setDrawsBackground:NO];
    [rawMouseOverlayArrowField setEditable:NO];
    [rawMouseOverlayArrowField setSelectable:NO];
    [rawMouseOverlayArrowField setAlignment:NSTextAlignmentCenter];
    [rawMouseOverlayArrowField setFont:[NSFont boldSystemFontOfSize:28.0f]];
    [rawMouseOverlayArrowField setTextColor:[NSColor colorWithSRGBRed:0.15f green:0.54f blue:0.98f alpha:1.0f]];
    [rawMouseOverlayArrowField setStringValue:@"↑"];
    [materialView addSubview:rawMouseOverlayArrowField];

    rawMouseOverlayTitleField = [[NSTextField alloc] initWithFrame:NSMakeRect(66.0f, 68.0f, 438.0f, 22.0f)];
    [rawMouseOverlayTitleField setBezeled:NO];
    [rawMouseOverlayTitleField setDrawsBackground:NO];
    [rawMouseOverlayTitleField setEditable:NO];
    [rawMouseOverlayTitleField setSelectable:NO];
    [rawMouseOverlayTitleField setTextColor:[NSColor labelColor]];
    [rawMouseOverlayTitleField setAlignment:NSTextAlignmentLeft];
    [[rawMouseOverlayTitleField cell] setWraps:YES];
    [[rawMouseOverlayTitleField cell] setScrollable:NO];
    [[rawMouseOverlayTitleField cell] setLineBreakMode:NSLineBreakByWordWrapping];
    [materialView addSubview:rawMouseOverlayTitleField];

    blockSelf = self;
    dragSourceView = [[[QSSAppDragSourceView alloc] initWithFrame:NSMakeRect(24.0f, 18.0f, 482.0f, 43.0f)
                                                        bundleURL:QSSHostAppBundleURL()
                                                      displayName:QSSHostAppDisplayName()
                                                             icon:QSSHostAppIcon()
                                                 onSuccessfulDrop:^{
        [blockSelf rawMousePermissionDragDidComplete];
    }] autorelease];
    rawMouseOverlayDragSourceView = [dragSourceView retain];
    [materialView addSubview:dragSourceView];

    [panel setContentView:materialView];
    rawMouseOverlayWindow = panel;
    [self refreshRawMouseOverlayContent];
}

- (NSRect)rawMouseSwitchFrameInScreen
{
    NSRect buttonFrameInWindow;

    if (!rawMouseSwitch || !launcherWindow || ![rawMouseSwitch superview])
        return NSZeroRect;

    buttonFrameInWindow = [[rawMouseSwitch superview] convertRect:[rawMouseSwitch frame]
                                                           toView:nil];
    return [launcherWindow convertRectToScreen:buttonFrameInWindow];
}

- (void)showRawMouseOverlayWithSnapshot:(QSSSystemSettingsWindowSnapshot)snapshot
{
    NSPoint targetOrigin;
    NSRect targetFrame;

    [self createRawMouseOverlayWindowIfNeeded];

    targetOrigin = QSSRawMouseOverlayOrigin(snapshot);
    targetFrame = NSMakeRect(targetOrigin.x, targetOrigin.y,
        QSSRawMouseOverlayWidth, QSSRawMouseOverlayHeight);

    if (!rawMouseOverlayPresented || ![rawMouseOverlayWindow isVisible])
    {
        NSRect startFrame = [self rawMouseSwitchFrameInScreen];

        if (NSIsEmptyRect(startFrame))
            startFrame = targetFrame;

        [rawMouseOverlayWindow setAlphaValue:NSIsEmptyRect(startFrame) ? 1.0f : 0.0f];
        [rawMouseOverlayWindow setFrame:startFrame display:NO];
        [rawMouseOverlayWindow orderFrontRegardless];

        [NSAnimationContext beginGrouping];
        [[NSAnimationContext currentContext] setDuration:0.22f];
        [[rawMouseOverlayWindow animator] setAlphaValue:1.0f];
        [[rawMouseOverlayWindow animator] setFrame:targetFrame display:YES];
        [NSAnimationContext endGrouping];

        rawMouseOverlayPresented = YES;
        return;
    }

    [rawMouseOverlayWindow setAlphaValue:1.0f];
    [rawMouseOverlayWindow setFrame:targetFrame display:YES];
    [rawMouseOverlayWindow orderFrontRegardless];
}

- (void)updateRawMousePermissionOverlay:(NSTimer *)timer
{
    QSSSystemSettingsWindowSnapshot snapshot;

    (void)timer;

    if (QSSInputMonitoringIsGranted())
    {
        [self refreshRawMouseSwitchState];
        return;
    }

    if (rawMouseDragCompleted)
    {
        if (rawMouseOverlayWindow)
            [rawMouseOverlayWindow orderOut:nil];
        rawMouseOverlayPresented = NO;
        return;
    }

    if (!QSSCopySystemSettingsWindowSnapshot(&snapshot))
    {
        if (rawMouseOverlayWindow)
            [rawMouseOverlayWindow orderOut:nil];
        rawMouseOverlayPresented = NO;
        return;
    }

    [self refreshRawMouseOverlayContent];
    [self showRawMouseOverlayWithSnapshot:snapshot];
}

- (void)startRawMousePermissionAssistant
{
    NSNotificationCenter *workspaceNotificationCenter;

    if (rawMouseOverlayTimer)
        [rawMouseOverlayTimer invalidate];
    [rawMouseOverlayTimer release];
    rawMouseOverlayTimer = [[NSTimer scheduledTimerWithTimeInterval:0.20f
                                                             target:self
                                                           selector:@selector(updateRawMousePermissionOverlay:)
                                                           userInfo:nil
                                                            repeats:YES] retain];

    workspaceNotificationCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    if (!rawMouseActivationObserver)
    {
        rawMouseActivationObserver = [workspaceNotificationCenter addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                                                              object:nil
                                                                               queue:[NSOperationQueue mainQueue]
                                                                          usingBlock:^(NSNotification *note) {
            (void)note;
            [self updateRawMousePermissionOverlay:nil];
            [self refreshRawMouseSwitchState];
        }];
    }

    [self updateRawMousePermissionOverlay:nil];
}

- (void)stopRawMousePermissionAssistant
{
    NSNotificationCenter *workspaceNotificationCenter;

    if (rawMouseOverlayTimer)
    {
        [rawMouseOverlayTimer invalidate];
        [rawMouseOverlayTimer release];
        rawMouseOverlayTimer = nil;
    }

    if (rawMouseActivationObserver)
    {
        workspaceNotificationCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
        [workspaceNotificationCenter removeObserver:rawMouseActivationObserver];
        rawMouseActivationObserver = nil;
    }

    if (rawMouseOverlayWindow)
        [rawMouseOverlayWindow orderOut:nil];
    rawMouseOverlayPresented = NO;
}

- (void)startPermissionFlow
{
    BOOL openedSettings = NO;

    if (!QSSSupportsInputMonitoring())
    {
        [self openMacOSInstructionsPage];
        return;
    }

    if (QSSInputMonitoringIsGranted())
    {
        [self refreshRawMouseSwitchState];
        return;
    }

    rawMousePermissionPending = YES;
    [[NSUserDefaults standardUserDefaults] setBool:YES forKey:QSSPrefRawMouseInputEnabledKey];
    [[NSUserDefaults standardUserDefaults] synchronize];
    [self setRawMouseSwitchOn:YES];
    rawMouseDragCompleted = NO;

    if (@available(macOS 10.15, *))
        (void)IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);

    openedSettings = QSSOpenInputMonitoringSettings();

    if (openedSettings)
        [self startRawMousePermissionAssistant];
    else
    {
        rawMousePermissionPending = NO;
        [[NSUserDefaults standardUserDefaults] setBool:NO forKey:QSSPrefRawMouseInputEnabledKey];
        [[NSUserDefaults standardUserDefaults] synchronize];
        [self setRawMouseSwitchOn:NO];
        [self openMacOSInstructionsPage];
    }
}

- (void)rawMouseSwitchToggled:(id)sender
{
    BOOL userWantsOn = [self rawMouseSwitchIsOn];
    BOOL granted = QSSInputMonitoringIsGranted();

    (void)sender;

    if (userWantsOn && !granted) {
        [self startPermissionFlow];
    } else if (userWantsOn) {
        [[NSUserDefaults standardUserDefaults] setBool:YES forKey:QSSPrefRawMouseInputEnabledKey];
        [[NSUserDefaults standardUserDefaults] synchronize];
        [self refreshRawMouseSwitchState];
    } else if (!userWantsOn) {
        rawMousePermissionPending = NO;
        [[NSUserDefaults standardUserDefaults] setBool:NO forKey:QSSPrefRawMouseInputEnabledKey];
        [[NSUserDefaults standardUserDefaults] synchronize];
        [self stopRawMousePermissionAssistant];
        [self setRawMouseSwitchOn:NO];

        if (QSSSupportsInputMonitoring() && !QSSOpenInputMonitoringSettings())
            [self openMacOSInstructionsPage];
    }
}

#pragma mark - App lifecycle

- (void)awakeFromNib {
}

- (void)populateInitialChips
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];

    [self removeAllChips];
    if (argumentTextField)
        [argumentTextField setStringValue:@""];

    // Launch options are session-only UI state. Clear any older persisted values.
    [defaults removeObjectForKey:FQPrefCommandLineKey];
    [defaults synchronize];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    NSUInteger flags;
    BOOL optionKeyPressed;
    BOOL launcherRequested;

    (void)aNotification;

    [self configureAboutMenu];
    [self configureSettingsMenu];
    [self configureHelpMenu];
    [self configureQuitMenu];
    [self installQuitKeyMonitor];
    [self configureFileMenu];
    [self removeUnusedMenus];

    if (launcherWindow) {
        [self configureLauncherWindow];
        [self buildLauncherUI];
        [self populateInitialChips];
    }

    [self refreshRawMouseSwitchState];

    flags = [NSEvent modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
    optionKeyPressed = (flags & NSEventModifierFlagOption) != 0;
    launcherRequested = ([arguments argument:@"-launcher"] != nil);

    if (launcherRequested)
        [arguments removeArgument:@"-launcher"];

    if ([arguments argument:@"-nolauncher"] != nil) {
        [arguments removeArgument:@"-nolauncher"];
        [self launchQuake:self];
    } else if (!optionKeyPressed && !launcherRequested) {
        [self launchQuake:self];
    } else {
        [launcherWindow center];
        [launcherWindow makeKeyAndOrderFront:self];
        [self scheduleRawMouseStartupRefresh];
    }
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    (void)notification;
    [self refreshLauncherAppearance];
    [self refreshRawMouseSwitchState];
}

- (NSMenu *)applicationDockMenu:(NSApplication *)sender
{
    NSMenu *menu;
    NSMenuItem *newInstanceItem;
    NSMenuItem *launcherItem;

    (void)sender;

    /* Explicit targets: automatic validation resolves nil-target actions
       through the responder chain, which no longer reaches us once the SDL
       window takes over, leaving the items greyed out while the game runs. */
    menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    [menu setAutoenablesItems:NO];
    newInstanceItem = [[[NSMenuItem alloc] initWithTitle:@"New Instance"
                                                  action:@selector(startNewInstance:)
                                           keyEquivalent:@""] autorelease];
    [newInstanceItem setTarget:self];
    [menu addItem:newInstanceItem];

    launcherItem = [[[NSMenuItem alloc] initWithTitle:@"Open with Launcher"
                                               action:@selector(openWithLauncher:)
                                        keyEquivalent:@""] autorelease];
    [launcherItem setTarget:self];
    /* The launcher only chooses arguments for a not-yet-started game, so it
       has nothing to offer once SDL_main has taken over this process. */
    [launcherItem setEnabled:(SDL_WasInit(0) == 0)];
    [menu addItem:launcherItem];

    {
        NSURL *gameFolderURL = QSSGameFolderURL();
        SEL connect = @selector(connectToRecentServer:);

        QSSAddServersSection(menu, @"Recent",
                             QSSRecentServersForGameFolder(gameFolderURL), self, connect);
        QSSAddServersSection(menu, @"Bookmarks",
                             QSSBookmarksForGameFolder(gameFolderURL), self, connect);
        QSSAddModsSection(menu, QSSModsForGameFolder(gameFolderURL), self,
                          @selector(switchToMod:));
    }

    [menu addItem:[NSMenuItem separatorItem]];
    {
        NSMenuItem *folderItem = [[[NSMenuItem alloc] initWithTitle:@"Open Game Folder"
                                                             action:@selector(openQuakeFolder:)
                                                      keyEquivalent:@""] autorelease];
        [folderItem setTarget:self];
        [menu addItem:folderItem];
    }
    return menu;
}

- (IBAction)startNewInstance:(id)sender
{
    (void)sender;
    QSSLaunchNewInstance();
}

- (IBAction)openWithLauncher:(id)sender
{
    (void)sender;

    /* Disabled in the Dock menu once the game is running, so this only ever
       has to bring this instance's launcher window back to the front. */
    if (SDL_WasInit(0) != 0 || !launcherWindow)
        return;

    [launcherWindow center];
    [launcherWindow makeKeyAndOrderFront:self];
    [NSApp activateIgnoringOtherApps:YES];
}

- (IBAction)connectToRecentServer:(id)sender
{
    id represented = [sender respondsToSelector:@selector(representedObject)] ?
        [sender representedObject] : nil;
    NSString *server = [represented isKindOfClass:[NSString class]] ? represented : nil;

    if (!QSSValidServerAddress(server))
        return;

    if (SDL_WasInit(0) == 0) {
        /* Replace, so a saved "+connect other" cannot make the game connect
           somewhere else first and then here. */
        [arguments setArgument:@"+connect" withValue:server];
        [self launchQuake:self];
    } else {
        /* The game runs in this process, and this action is dispatched from
           the engine's event pump on the main thread, so hand the command
           straight to the running game instead of spawning a new instance. */
        Cbuf_AddText([[NSString stringWithFormat:@"connect \"%@\"\n", server] UTF8String]);
        [NSApp activateIgnoringOtherApps:YES];
    }
}

- (IBAction)switchToMod:(id)sender
{
    id represented = [sender respondsToSelector:@selector(representedObject)] ?
        [sender representedObject] : nil;
    NSString *mod = [represented isKindOfClass:[NSString class]] ? represented : nil;

    if (!QSSValidModName(mod))
        return;

    if (SDL_WasInit(0) == 0) {
        /* Replace: COM_InitFilesystem stacks a gamedir for every -game it
           finds, so appending to a saved "-game other" would launch both. */
        [arguments setArgument:@"-game" withValue:mod];
        [self launchQuake:self];
    } else {
        /* COM_Game_f disconnects and reloads the game directory itself. */
        Cbuf_AddText([[NSString stringWithFormat:@"game \"%@\"\n", mod] UTF8String]);
        [NSApp activateIgnoringOtherApps:YES];
    }
}

- (void)launchDedicatedServerWithParsedArguments
{
    NSString *executablePath = [[NSBundle mainBundle] executablePath];
    if (!executablePath)
        return;

    NSString *executableDir = [executablePath stringByDeletingLastPathComponent];
    NSString *argsString = [self sanitizeCommandLine:[self joinedArgumentString]];
    BOOL hasMemOrHeap = NO;

    if ([argsString rangeOfString:@"-mem "].location != NSNotFound ||
        [argsString rangeOfString:@"-heapsize "].location != NSNotFound)
        hasMemOrHeap = YES;

    NSMutableString *command = [NSMutableString stringWithFormat:@"cd '%@' && ./QSS-M -nolauncher", executableDir];

    if ([argsString length] > 0) {
        [command appendString:@" "];
        [command appendString:argsString];
    }

    if (!hasMemOrHeap) {
        [command appendString:@" -mem 128"];
    }

    NSString *terminalPath = [[NSWorkspace sharedWorkspace] fullPathForApplication:@"Terminal"];
    if (!terminalPath)
        terminalPath = @"/Applications/Utilities/Terminal.app";

    NSTask *task = [[NSTask alloc] init];
    [task setLaunchPath:@"/usr/bin/osascript"];

    NSString *appleScript = [NSString stringWithFormat:
                             @"tell application \"Terminal\" to do script \"%@\"",
                             [command stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""]];

    [task setArguments:@[ @"-e", appleScript ]];
    [task launch];
    [task release];

    [self stopRawMousePermissionAssistant];
    exit(0);
}

- (IBAction)launchQuake:(id)sender {
    BOOL hadInitialArgs = ([arguments count] > 0);
    NSString *launchOptions;
    NSString *effectiveCommandLine;
    NSString *path;
    QuakeArguments *launchArguments;
    int argc;
    int i;

    (void)sender;

    // If the user has text pending in the argument text field, add it before launch.
    if (argumentTextField) {
        NSString *pending = [[self currentArgumentText] stringByTrimmingCharactersInSet:
                             [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if ([pending length] > 0)
            [self argumentTextFieldEntered:argumentTextField];
    }

    launchOptions = [self joinedArgumentString];
    effectiveCommandLine = [self effectiveCommandLineWithLaunchOptions:launchOptions];
    launchArguments = [[QuakeArguments alloc] init];
    [launchArguments parseArguments:effectiveCommandLine];

    if (!hadInitialArgs && [launchArguments argument:@"-dedicated"] != nil) {
        [launchArguments release];
        [self launchDedicatedServerWithParsedArguments];
        return;
    }

    path = [NSString stringWithCString:gArgv[0] encoding:NSASCIIStringEncoding];

    for (i = 0; i < 4; i++)
        path = [path stringByDeletingLastPathComponent];

    [[NSFileManager defaultManager] changeCurrentDirectoryPath:path];

    argc = [launchArguments count] + 1;
    {
        char *argv[argc];
        argv[0] = gArgv[0];
        [launchArguments setArguments:argv + 1];

        [self stopRawMousePermissionAssistant];
        if (launcherWindow)
            [launcherWindow close];

        int status = SDL_main(argc, argv);
        [launchArguments release];
        exit(status);
    }
}

- (IBAction)cancel:(id)sender {
    (void)sender;

    [self quitNow];
}

- (void)quitNow
{
    quitKeyDown = NO;
    [self cancelQuitHold];

    /* Once SDL is running, let the engine perform its normal quit sequence. */
    if (SDL_WasInit(0) != 0) {
        SDL_Event event = {0};
        event.type = SDL_QUIT;
        if (SDL_PushEvent(&event) == 1)
            return;
    }

    [self stopRawMousePermissionAssistant];
    exit(0);
}

- (IBAction)showSettings:(id)sender
{
    (void)sender;

    if (SDL_WasInit(0) != 0) {
        const SDL_Scancode scancodes[] = {
            SDL_SCANCODE_LGUI, SDL_SCANCODE_COMMA,
            SDL_SCANCODE_COMMA, SDL_SCANCODE_LGUI
        };
        const SDL_Keycode keycodes[] = {
            SDLK_LGUI, SDLK_COMMA, SDLK_COMMA, SDLK_LGUI
        };
        const Uint8 states[] = {
            SDL_PRESSED, SDL_PRESSED, SDL_RELEASED, SDL_RELEASED
        };
        size_t i;

        for (i = 0; i < sizeof(scancodes) / sizeof(scancodes[0]); ++i) {
            SDL_Event event = {0};
            event.type = (states[i] == SDL_PRESSED) ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.state = states[i];
            event.key.keysym.scancode = scancodes[i];
            event.key.keysym.sym = keycodes[i];
            event.key.keysym.mod = (i == 0 || i == 3) ? KMOD_NONE : KMOD_GUI;
            SDL_PushEvent(&event);
        }
        return;
    }

    [launcherWindow makeKeyAndOrderFront:self];
    [NSApp activateIgnoringOtherApps:YES];
}

- (IBAction)showAboutPanel:(id)sender {
    NSString *githubDisplay = @"github.com/timbergeron/QSS-M";
    NSString *versionString = [[[NSBundle mainBundle] infoDictionary] objectForKey:@"CFBundleShortVersionString"];
    if (!versionString)
        versionString = @"";

    NSRect frame = NSMakeRect(0, 0, 480, 260);
    NSWindow *aboutWindow = [[NSWindow alloc] initWithContentRect:frame
                                                        styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];

    [aboutWindow setReleasedWhenClosed:NO];

    NSView *contentView = [aboutWindow contentView];
    NSRect bounds = [contentView bounds];

    NSImage *appIcon = [NSApp applicationIconImage];
    CGFloat iconSize = 128.0f;
    CGFloat iconX = (NSWidth(bounds) - iconSize) / 2.0f;
    CGFloat iconY = NSMaxY(bounds) - iconSize - 20.0f;
    NSImageView *imageView = [[[NSImageView alloc] initWithFrame:NSMakeRect(iconX, iconY, iconSize, iconSize)] autorelease];
    [imageView setImage:appIcon];
    [imageView setImageScaling:NSImageScaleProportionallyUpOrDown];
    [contentView addSubview:imageView];

    CGFloat textWidth = 260.0f;
    CGFloat textX = (NSWidth(bounds) - textWidth) / 2.0f;
    CGFloat titleY = iconY - 28.0f;
    CGFloat versionY = titleY - 22.0f;
    CGFloat linkY = versionY - 16.0f;

    NSTextField *titleField = [[[NSTextField alloc] initWithFrame:NSMakeRect(textX, titleY, textWidth, 24)] autorelease];
    [titleField setBezeled:NO];
    [titleField setDrawsBackground:NO];
    [titleField setEditable:NO];
    [titleField setSelectable:NO];
    [titleField setAlignment:NSTextAlignmentCenter];
    [titleField setFont:[NSFont boldSystemFontOfSize:18.0f]];
    [titleField setStringValue:@"QSS-M"];
    [contentView addSubview:titleField];

    NSTextField *versionField = [[[NSTextField alloc] initWithFrame:NSMakeRect(textX, versionY, textWidth, 20)] autorelease];
    [versionField setBezeled:NO];
    [versionField setDrawsBackground:NO];
    [versionField setEditable:NO];
    [versionField setSelectable:NO];
    [versionField setAlignment:NSTextAlignmentCenter];
    [versionField setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
    [versionField setStringValue:[NSString stringWithFormat:@"Version %@", versionString]];
    [contentView addSubview:versionField];

    NSButton *linkButton = [[[NSButton alloc] initWithFrame:NSMakeRect(textX, linkY, textWidth, 20)] autorelease];
    [linkButton setBordered:NO];
    [linkButton setButtonType:NSButtonTypeMomentaryChange];
    [linkButton setTarget:self];
    [linkButton setAction:@selector(openGithub:)];
    [linkButton setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];

    NSDictionary *linkAttributes = @{
        NSForegroundColorAttributeName: QSSAboutLinkColor(),
        NSUnderlineStyleAttributeName: [NSNumber numberWithInt:NSUnderlineStyleSingle]
    };
    NSMutableAttributedString *linkTitle = [[[NSMutableAttributedString alloc] initWithString:githubDisplay attributes:linkAttributes] autorelease];
    [linkButton setAttributedTitle:linkTitle];
    [contentView addSubview:linkButton];

    NSButton *minimizeButton = [aboutWindow standardWindowButton:NSWindowMiniaturizeButton];
    if (minimizeButton)
        [minimizeButton setHidden:YES];
    NSButton *zoomButton = [aboutWindow standardWindowButton:NSWindowZoomButton];
    if (zoomButton)
        [zoomButton setHidden:YES];

    [aboutWindow center];
    [aboutWindow makeKeyAndOrderFront:self];
    [NSApp activateIgnoringOtherApps:YES];
}

- (IBAction)openQuakeFolder:(id)sender {
    NSURL *folderURL;

    (void)sender;

    folderURL = QSSGameFolderURL();
    if (!folderURL)
        folderURL = [NSURL fileURLWithPath:[[NSFileManager defaultManager] currentDirectoryPath]
                               isDirectory:YES];

    if (folderURL)
        [[NSWorkspace sharedWorkspace] openURL:folderURL];
}

- (IBAction)openGithub:(id)sender {
    (void)sender;
    NSURL *url = [NSURL URLWithString:@"https://github.com/timbergeron/QSS-M"];
    if (url)
        [[NSWorkspace sharedWorkspace] openURL:url];
}

- (IBAction)showKeyboardShortcutsPanel:(id)sender {
    (void)sender;

    [self createKeyboardShortcutsWindowIfNeeded];
    [self layoutKeyboardShortcutsTableColumns];
    [keyboardShortcutsWindow center];
    [keyboardShortcutsWindow makeKeyAndOrderFront:self];
    [keyboardShortcutsWindow makeFirstResponder:keyboardShortcutsSearchField];
    [NSApp activateIgnoringOtherApps:YES];
}

- (IBAction)openWebsite:(id)sender {
    (void)sender;
    NSURL *url = [NSURL URLWithString:@"https://qssm.quakeone.com/"];
    if (url)
        [[NSWorkspace sharedWorkspace] openURL:url];
}

- (void) dealloc {
    [self stopRawMousePermissionAssistant];
    if (quitKeyMonitor)
        [NSEvent removeMonitor:quitKeyMonitor];
    [quitKeyMonitor release];
    [quitHoldTimer invalidate];
    [quitHoldTimer release];
    [quitHoldProgressTimer invalidate];
    [quitHoldProgressTimer release];
    [quitHoldDismissTimer invalidate];
    [quitHoldDismissTimer release];
    [quitHoldOverlayWindow orderOut:nil];
    [quitHoldOverlayWindow release];
    [rawMouseOverlayWindow release];
    [rawMouseOverlayArrowField release];
    [rawMouseOverlayTitleField release];
    [rawMouseOverlayDragSourceView release];
    if (rawMouseStartupRefreshTimer)
        [rawMouseStartupRefreshTimer invalidate];
    [rawMouseStartupRefreshTimer release];
    [rawMouseSwitch release];
    [launchOptionsCard release];
    [settingsCard release];
    [argumentTextField release];
    [argumentCompletionGhostLabel release];
    [addArgumentButton release];
    [presetArgumentsMenu release];
    [argumentChips release];
    [keyboardShortcutsWindow setDelegate:nil];
    [keyboardShortcutsWindow release];
    [keyboardShortcutsSearchField release];
    [keyboardShortcutsTableView release];
    [keyboardShortcutRows release];
    [filteredKeyboardShortcutRows release];
    [launchButton release];
    [cancelButton release];
    [super dealloc];
}

@end
