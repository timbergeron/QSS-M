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
        
        int screenModeIndex = [defaults integerForKey:FQPrefScreenModeKey];
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
    
    int index = [screenModePopUp indexOfSelectedItem];
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

    [launcherWindow close];

    // update the defaults
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:[paramTextField stringValue] forKey:FQPrefCommandLineKey];
    [defaults setObject:[NSNumber numberWithBool:[fullscreenCheckBox state] == NSControlStateValueOn] forKey:FQPrefFullscreenKey];
    [defaults setObject:[NSNumber numberWithInt:index] forKey:FQPrefScreenModeKey];
    [defaults synchronize];

    int status = SDL_main (argc, argv);
    exit(status);
}

- (IBAction)cancel:(id)sender {
    exit(0);
}

- (IBAction)showAboutPanel:(id)sender {
    NSString *githubDisplay = @"github.com/timbergeron/QSS-M";
    NSString *versionString = @"1.6.5";

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
        NSForegroundColorAttributeName: [NSColor linkColor],
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
    [launcherTitleLabel release];
    [commandOptionPopUp release];
    [screenModes release];
    [super dealloc];
}


@end
