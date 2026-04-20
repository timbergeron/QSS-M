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
#import "ScreenInfo.h"
#import <ApplicationServices/ApplicationServices.h>
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
NSString *FQPrefFullscreenKey = @"Fullscreen";
NSString *FQPrefScreenModeKey = @"ScreenMode";

typedef struct {
    CGRect frame;
    CGRect visibleFrame;
    BOOL valid;
} QSSSystemSettingsWindowSnapshot;

static const CGFloat QSSRawMouseOverlayWidth = 530.0f;
static const CGFloat QSSRawMouseOverlayHeight = 109.0f;

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

static BOOL QSSInputMonitoringIsGranted(void)
{
    return !QSSSupportsInputMonitoring() ||
        QSSInputMonitoringAccessType() == kIOHIDAccessTypeGranted;
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
    [[rowView layer] setBorderColor:[[NSColor disabledControlTextColor] colorWithAlphaComponent:0.22f].CGColor];
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

@implementation AppController

+(void) initialize {
    NSMutableDictionary *defaults = [NSMutableDictionary dictionary];
    
    [defaults setObject:@"" forKey:FQPrefCommandLineKey];
    [defaults setObject:[NSNumber numberWithBool:YES] forKey:FQPrefFullscreenKey];
    [defaults setObject:[NSNumber numberWithInt:0] forKey:FQPrefScreenModeKey];
    
    [[NSUserDefaults standardUserDefaults] registerDefaults:defaults];
}

- (id)init {
    int i;
#ifndef USE_SDL2
    int j;
    int flags;
    int bpps[3] = {32, 24, 16};
    SDL_PixelFormat format;
    SDL_Rect **modes;
#endif
    ScreenInfo *info;

    self = [super init];
    if (!self)
        return nil;

    screenModes = [[NSMutableArray alloc] init];
    [screenModes addObject:@"Default or command line arguments"];

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) == -1)
        return self;
    
#if defined(USE_SDL2)
    {
        const int sdlmodes = SDL_GetNumDisplayModes(0);
        for (i = 0; i < sdlmodes; i++)
        {
            SDL_DisplayMode mode;
            if (SDL_GetDisplayMode(0, i, &mode) == 0)
            {
                info = [[ScreenInfo alloc] initWithWidth:mode.w height:mode.h bpp:SDL_BITSPERPIXEL(mode.format)];
                [screenModes addObject:info];
                [info release];
            }
        }
    }
#else
    flags = SDL_OPENGL | SDL_FULLSCREEN;
    format.palette = NULL;
    
    for (i = 0; i < 3; i++) {
        format.BitsPerPixel = bpps[i];
        modes = SDL_ListModes(&format, flags);

        if (modes == (SDL_Rect **)0 || modes == (SDL_Rect **)-1)
            continue;

        for (j = 0; modes[j]; j++) {
            info = [[ScreenInfo alloc] initWithWidth:modes[j]->w height:modes[j]->h bpp:bpps[i]];
            [screenModes addObject:info];
            [info release];
        }
    }
#endif

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    
    arguments = [[QuakeArguments alloc] initWithArguments:gArgv + 1 count:gArgc - 1];
    return self;
}

- (NSArray *)screenModes {
    return screenModes;
}

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

- (void)addOptionWithTitle:(NSString *)title
                    insert:(NSString *)insertString
                   enabled:(BOOL)enabled
{
    if (!commandOptionPopUp)
        return;

    NSMenu *menu = [commandOptionPopUp menu];
    NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:title action:NULL keyEquivalent:@""] autorelease];
    [item setEnabled:enabled];
    if (insertString)
        [item setRepresentedObject:insertString];
    [menu addItem:item];
}

- (void)populateCommandLineOptionsMenu {
    if (!commandOptionPopUp)
        return;

    NSMenu *menu = [commandOptionPopUp menu];
    [menu removeAllItems];

    // General / debug
    [self addOptionWithTitle:@"-condebug (log console to file)" insert:@"-condebug" enabled:YES];
    [self addOptionWithTitle:@"-nohome (ignore ~/.quakespasm)" insert:@"-nohome" enabled:YES];
    [self addOptionWithTitle:@"-fitz (FitzQuake emulation)" insert:@"-fitz" enabled:YES];
    [self addOptionWithTitle:@"-qmenu (classic menu)" insert:@"-qmenu" enabled:YES];

    [menu addItem:[NSMenuItem separatorItem]];

    // Memory
    [self addOptionWithTitle:@"-heapsize <kb> (engine heap)" insert:@"-heapsize " enabled:YES];
    [self addOptionWithTitle:@"-mem <mb> (engine heap)" insert:@"-mem " enabled:YES];

    [menu addItem:[NSMenuItem separatorItem]];

    // Video
    [self addOptionWithTitle:@"-width <pixels>" insert:@"-width " enabled:YES];
    [self addOptionWithTitle:@"-height <pixels>" insert:@"-height " enabled:YES];
    [self addOptionWithTitle:@"-bpp <bits>" insert:@"-bpp " enabled:YES];
    [self addOptionWithTitle:@"-refreshrate <hz>" insert:@"-refreshrate " enabled:YES];
    [self addOptionWithTitle:@"-window (windowed)" insert:@"-window" enabled:YES];
    [self addOptionWithTitle:@"-fullscreen (fullscreen)" insert:@"-fullscreen" enabled:YES];
    [self addOptionWithTitle:@"-novbo (disable VBOs)" insert:@"-novbo" enabled:YES];
    [self addOptionWithTitle:@"-nomtex (disable multitexture)" insert:@"-nomtex" enabled:YES];
    [self addOptionWithTitle:@"-nocombine (disable combine)" insert:@"-nocombine" enabled:YES];
    [self addOptionWithTitle:@"-noadd (disable additive blends)" insert:@"-noadd" enabled:YES];
    [self addOptionWithTitle:@"-notexturenpot (no NPOT textures)" insert:@"-notexturenpot" enabled:YES];
    [self addOptionWithTitle:@"-noglsl (disable GLSL)" insert:@"-noglsl" enabled:YES];
    [self addOptionWithTitle:@"-noglslgamma (disable GLSL gamma)" insert:@"-noglslgamma" enabled:YES];
    [self addOptionWithTitle:@"-noglslalias (disable GLSL alias)" insert:@"-noglslalias" enabled:YES];
    [self addOptionWithTitle:@"-nowarpmipmaps" insert:@"-nowarpmipmaps" enabled:YES];
    [self addOptionWithTitle:@"-current (use current display mode)" insert:@"-current" enabled:YES];

    [menu addItem:[NSMenuItem separatorItem]];

    // Audio
    [self addOptionWithTitle:@"-nosound (disable sound)" insert:@"-nosound" enabled:YES];
    [self addOptionWithTitle:@"-noextmusic (disable external music)" insert:@"-noextmusic" enabled:YES];
    [self addOptionWithTitle:@"-sndspeed <hz>" insert:@"-sndspeed " enabled:YES];
    [self addOptionWithTitle:@"-mixspeed <hz>" insert:@"-mixspeed " enabled:YES];
    [self addOptionWithTitle:@"-nocdaudio (disable CD audio)" insert:@"-nocdaudio" enabled:YES];

    [menu addItem:[NSMenuItem separatorItem]];

    // Input
    [self addOptionWithTitle:@"-nojoy (disable joystick)" insert:@"-nojoy" enabled:YES];
    [self addOptionWithTitle:@"-nomouse (disable mouse)" insert:@"-nomouse" enabled:YES];

    [menu addItem:[NSMenuItem separatorItem]];

    // Network / server
    [self addOptionWithTitle:@"-dedicated (dedicated server)" insert:@"-dedicated" enabled:YES];
    [self addOptionWithTitle:@"-listen (listen server)" insert:@"-listen" enabled:YES];
    [self addOptionWithTitle:@"-nolan (disable LAN broadcast)" insert:@"-nolan" enabled:YES];

    [menu addItem:[NSMenuItem separatorItem]];

    // Console
    [self addOptionWithTitle:@"-consize <kb> (console buffer)" insert:@"-consize " enabled:YES];

    [menu addItem:[NSMenuItem separatorItem]];

    // CD / misc
    [self addOptionWithTitle:@"-cddev <device>" insert:@"-cddev " enabled:YES];

    [menu addItem:[NSMenuItem separatorItem]];

    // Startup commands (+console)
    [self addOptionWithTitle:@"+map <mapname>" insert:@"+map " enabled:YES];
    [self addOptionWithTitle:@"+skill <0-3>" insert:@"+skill " enabled:YES];
    [self addOptionWithTitle:@"+name <playername>" insert:@"+name " enabled:YES];
    [self addOptionWithTitle:@"+connect <address>" insert:@"+connect " enabled:YES];
    [self addOptionWithTitle:@"+exec <cfg>" insert:@"+exec " enabled:YES];
    [self addOptionWithTitle:@"+developer 1" insert:@"+developer 1" enabled:YES];
}

- (void)configureAboutMenu {
    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenuItem *appMenuItem = ([mainMenu numberOfItems] > 0) ? [mainMenu itemAtIndex:0] : nil;
    NSMenu *appMenu = [appMenuItem submenu];
    NSMenuItem *aboutItem = [appMenu itemWithTitle:@"About QuakeSpasm"];
    if (!aboutItem && [appMenu numberOfItems] > 0)
        aboutItem = [appMenu itemAtIndex:0];

    if (aboutItem) {
        [aboutItem setTitle:@"About QSS-M"];
        [aboutItem setTarget:self];
        [aboutItem setAction:@selector(showAboutPanel:)];
        [aboutItem setEnabled:YES];
    }
}

- (void)configureQuitMenu {
    NSMenu *mainMenu = [NSApp mainMenu];
    NSMenuItem *appMenuItem = ([mainMenu numberOfItems] > 0) ? [mainMenu itemAtIndex:0] : nil;
    NSMenu *appMenu = [appMenuItem submenu];
    NSMenuItem *quitItem = [appMenu itemWithTitle:@"Quit QuakeSpasm"];
    if (!quitItem)
        quitItem = [appMenu itemWithTitle:@"Quit QSS-M"];

    if (!quitItem) {
        NSInteger idx = [appMenu indexOfItemWithTarget:nil andAction:@selector(terminate:)];
        if (idx != -1)
            quitItem = [appMenu itemAtIndex:idx];
    }

    if (quitItem) {
        [quitItem setTitle:@"Quit QSS-M"];
        [quitItem setTarget:self];
        [quitItem setAction:@selector(cancel:)];
        [quitItem setKeyEquivalent:@"q"];
        [quitItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        [quitItem setEnabled:YES];
    }
}

- (void)configureQuitButtonInView:(NSView *)view {
    NSEnumerator *enumerator = [[view subviews] objectEnumerator];
    NSView *subview;

    while ((subview = [enumerator nextObject])) {
        if ([subview isKindOfClass:[NSButton class]]) {
            NSButton *button = (NSButton *)subview;
            if ([[button title] isEqualToString:@"Cancel"]) {
                [button setTitle:@"Quit"];
                [button setKeyEquivalent:@"q"];
                [button setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
                return;
            }
        }
        [self configureQuitButtonInView:subview];
    }
}

- (void)hideSettingsLabelInView:(NSView *)view {
    NSArray *subviews = [view subviews];
    for (NSView *subview in subviews) {
        if ([subview isKindOfClass:[NSTextField class]]) {
            NSTextField *textField = (NSTextField *)subview;
            if ([[textField stringValue] isEqualToString:@"Settings"]) {
                [textField setHidden:YES];
            }
        }
        [self hideSettingsLabelInView:subview];
    }
}

- (void)configureQuitButton {
    if (!launcherWindow)
        return;

    NSView *contentView = [launcherWindow contentView];
    if (!contentView)
        return;

    [self configureQuitButtonInView:contentView];
}

- (void)setupCommandLineOptionsUI {
    if (!launcherWindow || !paramTextField || commandOptionPopUp)
        return;

    NSView *parent = [paramTextField superview];
    if (!parent)
        return;

    NSRect paramFrame = [paramTextField frame];

    // Layout: [TextField] [Label "Add"] [Popup]
    CGFloat popupWidth = 24.0f;
    CGFloat labelWidth = 30.0f;
    CGFloat spacing = 8.0f;

    // Shrink the text field to make room for the label + popup.
    NSRect newParamFrame = paramFrame;
    CGFloat minWidth = 120.0f;
    CGFloat shrinkAmount = labelWidth + popupWidth + spacing * 2.0f;
    if (newParamFrame.size.width > minWidth + shrinkAmount)
        newParamFrame.size.width -= shrinkAmount;
    else
        newParamFrame.size.width = minWidth;

    [paramTextField setFrame:newParamFrame];

    // "Add" Label
    NSRect labelFrame;
    labelFrame.size.width = labelWidth;
    labelFrame.size.height = 17.0f; // Standard label height
    labelFrame.origin.x = NSMaxX(newParamFrame) + spacing;
    // Center vertically relative to text field
    labelFrame.origin.y = newParamFrame.origin.y + (newParamFrame.size.height - labelFrame.size.height) / 2.0f - 1.0f;

    NSTextField *addLabel = [[NSTextField alloc] initWithFrame:labelFrame];
    [addLabel setStringValue:@"Add"];
    [addLabel setBezeled:NO];
    [addLabel setDrawsBackground:NO];
    [addLabel setEditable:NO];
    [addLabel setSelectable:NO];
    [addLabel setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
    [addLabel setTextColor:[NSColor labelColor]];
    [addLabel setAutoresizingMask:(NSViewMinXMargin | NSViewMinYMargin)];
    [parent addSubview:addLabel];
    [addLabel release];

    // Popup
    NSRect popupFrame;
    popupFrame.size.width = popupWidth;
    popupFrame.size.height = newParamFrame.size.height;
    popupFrame.origin.x = NSMaxX(labelFrame) + spacing;
    popupFrame.origin.y = newParamFrame.origin.y;

    commandOptionPopUp = [[NSPopUpButton alloc] initWithFrame:popupFrame pullsDown:NO];
    [commandOptionPopUp setAutoresizingMask:(NSViewMinXMargin | NSViewMinYMargin)];
    [commandOptionPopUp setTarget:self];
    [commandOptionPopUp setAction:@selector(addCommandLineOption:)];
    [parent addSubview:commandOptionPopUp];

    [self populateCommandLineOptionsMenu];

    // Don't show any initial label in the popup,
    // just the arrows.
    [commandOptionPopUp selectItem:nil];
    [commandOptionPopUp setTitle:@""];
}

- (void)layoutStartAndQuitButtons {
    if (!launcherWindow)
        return;

    NSView *contentView = [launcherWindow contentView];
    if (!contentView)
        return;

    NSButton *startButton = nil;
    NSButton *quitButton = nil;

    for (NSView *subview in [contentView subviews]) {
        if (![subview isKindOfClass:[NSButton class]])
            continue;

        NSButton *button = (NSButton *)subview;
        NSString *title = [button title];
        if ([title isEqualToString:@"Start"])
            startButton = button;
        else if ([title isEqualToString:@"Quit"] || [title isEqualToString:@"Cancel"])
            quitButton = button;
    }

    if (!startButton || !quitButton)
        return;

    NSRect contentBounds = [contentView bounds];
    NSRect quitFrame = [quitButton frame];
    NSRect startFrame = [startButton frame];

    CGFloat spacing = 30.0f;
    CGFloat totalWidth = quitFrame.size.width + spacing + startFrame.size.width;
    CGFloat originX = (NSWidth(contentBounds) - totalWidth) / 2.0f;

    quitFrame.origin.x = originX;
    startFrame.origin.x = originX + quitFrame.size.width + spacing;

    [quitButton setFrame:quitFrame];
    [startButton setFrame:startFrame];
}

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
    [[materialView layer] setBorderColor:[[NSColor disabledControlTextColor] colorWithAlphaComponent:0.18f].CGColor];

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

- (NSRect)rawMouseInputButtonFrameInScreen
{
    NSRect buttonFrameInWindow;

    if (!rawMouseInputButton || !launcherWindow || ![rawMouseInputButton superview])
        return NSZeroRect;

    buttonFrameInWindow = [[rawMouseInputButton superview] convertRect:[rawMouseInputButton frame]
                                                                toView:nil];
    return [launcherWindow convertRectToScreen:buttonFrameInWindow];
}

- (void)adjustRawMousePermissionLayoutVisible:(BOOL)visible
{
    NSRect labelFrame;
    NSRect popupFrame;
    NSRect fullscreenFrame;
    NSRect buttonFrame;
    CGFloat permissionLayoutOffset = 23.0f;

    if (!rawMousePermissionLayoutInitialized || !rawMouseInputButton)
        return;

    labelFrame = screenModeLabelBaseFrame;
    popupFrame = screenModePopUpBaseFrame;
    fullscreenFrame = fullscreenCheckBoxBaseFrame;
    buttonFrame = NSMakeRect(NSMinX(screenModePopUpBaseFrame), 17.0f, 184.0f, 20.0f);

    if (visible)
    {
        labelFrame.origin.y += permissionLayoutOffset;
        popupFrame.origin.y += permissionLayoutOffset;
        fullscreenFrame.origin.y += permissionLayoutOffset;
        [rawMouseInputButton setFrame:buttonFrame];
    }

    if (screenModeLabel)
        [screenModeLabel setFrame:labelFrame];
    [screenModePopUp setFrame:popupFrame];
    [fullscreenCheckBox setFrame:fullscreenFrame];
    [rawMouseInputButton setHidden:!visible];
}

- (void)refreshRawMousePermissionUI
{
    BOOL showButton;

    if (!rawMousePermissionLayoutInitialized)
        return;

    showButton = QSSSupportsInputMonitoring() && !QSSInputMonitoringIsGranted();
    [self adjustRawMousePermissionLayoutVisible:showButton];

    if (!showButton)
        [self stopRawMousePermissionAssistant];
}

- (void)setupRawMousePermissionUI
{
    NSView *parent;
    NSView *subview;

    if (!launcherWindow || !screenModePopUp || rawMouseInputButton)
        return;

    parent = [screenModePopUp superview];
    if (!parent)
        return;

    for (subview in [parent subviews])
    {
        if (![subview isKindOfClass:[NSTextField class]])
            continue;
        if ([[(NSTextField *)subview stringValue] isEqualToString:@"Resolution and color depth"])
        {
            screenModeLabel = (NSTextField *)subview;
            break;
        }
    }

    if (!screenModeLabel)
        return;

    screenModeLabelBaseFrame = [screenModeLabel frame];
    screenModePopUpBaseFrame = [screenModePopUp frame];
    fullscreenCheckBoxBaseFrame = [fullscreenCheckBox frame];

    rawMouseInputButton = [[NSButton alloc] initWithFrame:NSZeroRect];
    [rawMouseInputButton setTitle:@"Allow RAW Mouse Input"];
    [rawMouseInputButton setBezelStyle:NSBezelStyleRounded];
    [rawMouseInputButton setControlSize:NSControlSizeSmall];
    [rawMouseInputButton setFont:[NSFont systemFontOfSize:[NSFont smallSystemFontSize]]];
    [rawMouseInputButton setTarget:self];
    [rawMouseInputButton setAction:@selector(allowRawMouseInput:)];
    [rawMouseInputButton setAutoresizingMask:(NSViewMaxXMargin | NSViewMinYMargin)];
    [rawMouseInputButton setHidden:YES];
    [parent addSubview:rawMouseInputButton];

    rawMousePermissionLayoutInitialized = YES;
    [self refreshRawMousePermissionUI];
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
        NSRect startFrame = [self rawMouseInputButtonFrameInScreen];

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
        [self refreshRawMousePermissionUI];
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
            [self refreshRawMousePermissionUI];
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

- (IBAction)allowRawMouseInput:(id)sender
{
    NSURL *settingsURL;
    BOOL openedSettings = NO;

    (void)sender;

    if (!QSSSupportsInputMonitoring())
    {
        [self openMacOSInstructionsPage];
        return;
    }

    if (QSSInputMonitoringIsGranted())
    {
        [self refreshRawMousePermissionUI];
        return;
    }

    rawMouseDragCompleted = NO;

    if (@available(macOS 10.15, *))
        (void)IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);

    settingsURL = QSSInputMonitoringSettingsURL();
    if (settingsURL)
        openedSettings = [[NSWorkspace sharedWorkspace] openURL:settingsURL];
    if (!openedSettings)
    {
        settingsURL = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent"];
        if (settingsURL)
            openedSettings = [[NSWorkspace sharedWorkspace] openURL:settingsURL];
    }

    if (openedSettings)
        [self startRawMousePermissionAssistant];
    else
        [self openMacOSInstructionsPage];
}

#ifndef MAC_OS_X_VERSION_10_13
#define NSControlStateValueOff NSOffState
#define NSControlStateValueOn NSOnState
#endif
- (void)awakeFromNib {
    if ([arguments count] > 0) {
        NSString *sanitized = [self sanitizeCommandLine:[arguments description]];
        [paramTextField setStringValue:sanitized];
        if ([arguments argument:@"-window"] != nil)
            [fullscreenCheckBox setState:NSControlStateValueOff];
    } else {
		NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        NSString *raw = [defaults stringForKey:FQPrefCommandLineKey];
        NSString *sanitized = [self sanitizeCommandLine:raw];
        [paramTextField setStringValue:sanitized ? sanitized : @""];
        
        BOOL fullscreen = [defaults boolForKey:FQPrefFullscreenKey];
        [fullscreenCheckBox setState:fullscreen ? NSControlStateValueOn : NSControlStateValueOff];
        
        NSInteger screenModeIndex = [defaults integerForKey:FQPrefScreenModeKey];
        [screenModePopUp selectItemAtIndex:screenModeIndex];

        // If we stripped anything (like stray -nolauncher tokens),
        // persist the cleaned value so it doesn't come back.
        if (raw && ![sanitized isEqualToString:raw]) {
            [defaults setObject:sanitized forKey:FQPrefCommandLineKey];
            [defaults synchronize];
        }
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    [self configureAboutMenu];
    [self configureQuitMenu];
    [self configureQuitButton];
    [self layoutStartAndQuitButtons];
    [self setupCommandLineOptionsUI];
    [self setupRawMousePermissionUI];

    if (launcherWindow) {
        [launcherWindow setTitle:@"QSS-M"];
        
        // Remove title bar and other controls
        NSWindowStyleMask style = [launcherWindow styleMask];
        // Ensure Titled is present so it can be key, but hide it visually
        style |= NSWindowStyleMaskTitled;
        style |= NSWindowStyleMaskFullSizeContentView;
        style &= ~NSWindowStyleMaskClosable;
        style &= ~NSWindowStyleMaskMiniaturizable;
        style &= ~NSWindowStyleMaskResizable;
        [launcherWindow setStyleMask:style];
        
        [launcherWindow setTitleVisibility:NSWindowTitleHidden];
        [launcherWindow setTitlebarAppearsTransparent:YES];
        
        // Allow moving the window by dragging the background since there is no title bar
        [launcherWindow setMovableByWindowBackground:YES];
        
        // Rounded corners
        [launcherWindow setBackgroundColor:[NSColor clearColor]];
        [launcherWindow setOpaque:NO];
        
        NSView *contentView = [launcherWindow contentView];
        [contentView setWantsLayer:YES];
        [contentView.layer setCornerRadius:15.0f];
        [contentView.layer setMasksToBounds:YES];
        [contentView.layer setBackgroundColor:[[NSColor windowBackgroundColor] CGColor]];
        
        [self hideSettingsLabelInView:contentView];
    }

    [self refreshRawMousePermissionUI];

    // Get current keyboard state - woods #option
    NSUInteger flags = [NSEvent modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
    BOOL optionKeyPressed = (flags & NSEventModifierFlagOption) != 0;

	if ([arguments argument:@"-nolauncher"] != nil) {
		[arguments removeArgument:@"-nolauncher"];
		[self launchQuake:self];
    } else if (!optionKeyPressed) {
        // If Option key is NOT pressed, directly execute the launch code
        // First load preferences as awakeFromNib would have done
        [self awakeFromNib];
        // Then launch the game directly
        [self launchQuake:self];
	} else {
        // Show launcher window if Option key is pressed
        [launcherWindow center];
        [launcherWindow makeKeyAndOrderFront:self];
	}
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    (void)notification;
    [self refreshRawMousePermissionUI];
}

- (IBAction)changeScreenMode:(id)sender {
    // Always allow changing fullscreen, even when using
    // "Default or command line arguments" for resolution.
    [fullscreenCheckBox setEnabled:YES];
}

- (void)launchDedicatedServerWithParsedArguments
{
    NSString *executablePath = [[NSBundle mainBundle] executablePath];
    if (!executablePath)
        return;

    NSString *executableDir = [executablePath stringByDeletingLastPathComponent];

    // Build the exact shell command we want Terminal to run,
    // matching the working manual invocation:
    //   cd /path/to/QSS-M.app/Contents/MacOS && ./QSS-M -dedicated -mem 128 ...
    // Start from the text field so we only use what the user typed.
    NSString *argsString = [self sanitizeCommandLine:[paramTextField stringValue]];

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

    // Launch Terminal.app and run the command there so the
    // dedicated server has a visible console window.
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

    // Terminate the launcher app; the dedicated server will
    // continue running in the Terminal session.
    [self stopRawMousePermissionAssistant];
    exit(0);
}

- (IBAction)launchQuake:(id)sender {
    BOOL hadInitialArgs = ([arguments count] > 0);
    [arguments parseArguments:[paramTextField stringValue]];

    // If launched from the GUI (no initial command-line args)
    // and the user requested -dedicated, spawn a separate
    // dedicated server process that matches the working
    // terminal command instead of running inside the GUI app.
    if (!hadInitialArgs && [arguments argument:@"-dedicated"] != nil) {
        [self launchDedicatedServerWithParsedArguments];
        return;
    }
    
    NSInteger index = [screenModePopUp indexOfSelectedItem];
    if (index > 0) {
        ScreenInfo *info = [screenModes objectAtIndex:index];
        
        int width = [info width];
        int height = [info height];
        int bpp = [info bpp];

        [arguments addArgument:@"-width" withValue:[NSString stringWithFormat:@"%d", width]];
        [arguments addArgument:@"-height" withValue:[NSString stringWithFormat:@"%d", height]];
        [arguments addArgument:@"-bpp" withValue:[NSString stringWithFormat:@"%d", bpp]];
    }
    
    [arguments removeArgument:@"-fullscreen"];
    [arguments removeArgument:@"-window"];
    BOOL fullscreen = [fullscreenCheckBox state] == NSControlStateValueOn;
    if (fullscreen)
        [arguments addArgument:@"-fullscreen"];
    else
        [arguments addArgument:@"-window"];

    NSString *path = [NSString stringWithCString:gArgv[0] encoding:NSASCIIStringEncoding];
    
    int i;
    for (i = 0; i < 4; i++)
        path = [path stringByDeletingLastPathComponent];

    NSFileManager *fileManager = [NSFileManager defaultManager];
    [fileManager changeCurrentDirectoryPath:path];
    
    int argc = [arguments count] + 1;
    char *argv[argc];

    argv[0] = gArgv[0];
    [arguments setArguments:argv + 1];

    [self stopRawMousePermissionAssistant];
    [launcherWindow close];

    // update the defaults
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:[paramTextField stringValue] forKey:FQPrefCommandLineKey];
    [defaults setObject:[NSNumber numberWithBool:[fullscreenCheckBox state] == NSControlStateValueOn] forKey:FQPrefFullscreenKey];
    [defaults setObject:[NSNumber numberWithInteger:index] forKey:FQPrefScreenModeKey];
    [defaults synchronize];

    int status = SDL_main (argc, argv);
    exit(status);
}

- (IBAction)cancel:(id)sender {
    (void)sender;
    [self stopRawMousePermissionAssistant];
    exit(0);
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

    // Large icon centered at top
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

    // Title (below icon)
    NSTextField *titleField = [[[NSTextField alloc] initWithFrame:NSMakeRect(textX, titleY, textWidth, 24)] autorelease];
    [titleField setBezeled:NO];
    [titleField setDrawsBackground:NO];
    [titleField setEditable:NO];
    [titleField setSelectable:NO];
    [titleField setAlignment:NSTextAlignmentCenter];
    [titleField setFont:[NSFont boldSystemFontOfSize:18.0f]];
    [titleField setStringValue:@"QSS-M"];
    [contentView addSubview:titleField];

    // Version (below title)
    NSTextField *versionField = [[[NSTextField alloc] initWithFrame:NSMakeRect(textX, versionY, textWidth, 20)] autorelease];
    [versionField setBezeled:NO];
    [versionField setDrawsBackground:NO];
    [versionField setEditable:NO];
    [versionField setSelectable:NO];
    [versionField setAlignment:NSTextAlignmentCenter];
    [versionField setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
    [versionField setStringValue:[NSString stringWithFormat:@"Version %@", versionString]];
    [contentView addSubview:versionField];

    // GitHub link as a button (below version)
    NSButton *linkButton = [[[NSButton alloc] initWithFrame:NSMakeRect(textX, linkY, textWidth, 20)] autorelease];
    [linkButton setBordered:NO];
    [linkButton setButtonType:NSButtonTypeMomentaryChange];
    [linkButton setTarget:self];
    [linkButton setAction:@selector(openGithub:)];
    [linkButton setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];

    NSDictionary *linkAttributes = @{
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:139.0f/255.0f green:95.0f/255.0f blue:71.0f/255.0f alpha:1.0f],
        NSUnderlineStyleAttributeName: [NSNumber numberWithInt:NSUnderlineStyleSingle]
    };
    NSMutableAttributedString *linkTitle = [[[NSMutableAttributedString alloc] initWithString:githubDisplay attributes:linkAttributes] autorelease];
    [linkButton setAttributedTitle:linkTitle];
    [contentView addSubview:linkButton];

    // Only show close button; hide minimize+zoom if present
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

- (IBAction)addCommandLineOption:(id)sender {
    if (!commandOptionPopUp || !paramTextField)
        return;

    NSMenuItem *item = [commandOptionPopUp selectedItem];
    if (!item || ![item isEnabled] || [item isSeparatorItem])
        return;

    NSString *insert = [item representedObject];
    if (!insert || [insert length] == 0)
        insert = [item title];
    if (!insert || [insert length] == 0)
        return;

    NSString *current = [paramTextField stringValue];
    if (!current)
        current = @"";

    // Trim trailing whitespace and append a space if needed.
    current = [current stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([current length] > 0)
        current = [current stringByAppendingString:@" "];

    NSString *newText = [current stringByAppendingString:insert];
    [paramTextField setStringValue:newText];

    // Focus the text field so the user can edit values.
    [[paramTextField window] makeFirstResponder:paramTextField];

    // Reset the popup selection so it looks like a button again
    [commandOptionPopUp selectItem:nil];
    [commandOptionPopUp setTitle:@""];
}

- (IBAction)openGithub:(id)sender {
    NSURL *url = [NSURL URLWithString:@"https://github.com/timbergeron/QSS-M"];
    if (url)
        [[NSWorkspace sharedWorkspace] openURL:url];
}

- (void) dealloc {
    [self stopRawMousePermissionAssistant];
    [rawMouseOverlayWindow release];
    [rawMouseOverlayArrowField release];
    [rawMouseOverlayTitleField release];
    [rawMouseOverlayDragSourceView release];
    [rawMouseInputButton release];
    [launcherTitleLabel release];
    [commandOptionPopUp release];
    [screenModes release];
    [super dealloc];
}


@end
