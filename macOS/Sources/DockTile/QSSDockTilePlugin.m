#import <Cocoa/Cocoa.h>
#import "QSSDockMenuContent.h"

/* This runs in the Dock's plug-in host process while QSS-M itself is not
   running, so menu actions must be handled here: they cannot be forwarded
   to the app, and nil-target items fail validation and render disabled. */

@interface QSSDockTilePlugin : NSObject <NSDockTilePlugIn>
{
    /* The returned menu must stay strongly held until the user is done with
       it; an autoreleased menu renders but its selections are ignored (see
       the NSDockTilePlugIn protocol note in NSDockTile.h). */
    NSMenu *dockMenu;
}
@end

@implementation QSSDockTilePlugin

static NSURL *QSSDockApplicationURL(void)
{
    static NSURL *applicationURL;
    NSString *pluginPath;
    NSString *applicationPath;
    NSURL *candidateURL;

    if (applicationURL)
        return applicationURL;

    /* plugin bundle -> PlugIns -> Contents -> QSS-M.app */
    pluginPath = [[NSBundle bundleForClass:[QSSDockTilePlugin class]] bundlePath];
    applicationPath = [[[pluginPath stringByDeletingLastPathComponent]
                        stringByDeletingLastPathComponent]
                       stringByDeletingLastPathComponent];
    candidateURL = [NSURL fileURLWithPath:applicationPath isDirectory:YES];

    applicationURL = [QSSOriginalApplicationURL(candidateURL, @"QSSDockTilePlugin") copy];
    return applicationURL;
}

static NSURL *QSSDockGameFolderURL(void)
{
    return [QSSDockApplicationURL() URLByDeletingLastPathComponent];
}

static void QSSDockLaunchApp(NSArray *launchArguments)
{
    NSURL *applicationURL = QSSDockApplicationURL();
    NSError *error = nil;

    if (!applicationURL)
        return;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    /* NSWorkspaceOpenConfiguration needs macOS 10.15; the plugin targets 10.13. */
    if (![[NSWorkspace sharedWorkspace] launchApplicationAtURL:applicationURL
                                                       options:NSWorkspaceLaunchDefault
                                                 configuration:@{ NSWorkspaceLaunchConfigurationArguments : launchArguments }
                                                         error:&error])
        NSLog(@"QSSDockTilePlugin: unable to launch QSS-M: %@", error);
#pragma clang diagnostic pop
}

- (void)setDockTile:(NSDockTile *)dockTile
{
    (void)dockTile;
}

- (void)dealloc
{
    [dockMenu release];
    [super dealloc];
}

- (void)openWithLauncher:(id)sender
{
    (void)sender;
    QSSDockLaunchApp(@[ @"-launcher" ]);
}

- (void)connectToRecentServer:(id)sender
{
    id represented = [sender respondsToSelector:@selector(representedObject)] ?
        [sender representedObject] : nil;
    NSString *server = [represented isKindOfClass:[NSString class]] ? represented : nil;

    if (!QSSValidServerAddress(server))
        return;
    QSSDockLaunchApp(@[ @"-nolauncher", @"+connect", server ]);
}

- (void)switchToMod:(id)sender
{
    id represented = [sender respondsToSelector:@selector(representedObject)] ?
        [sender representedObject] : nil;
    NSString *mod = [represented isKindOfClass:[NSString class]] ? represented : nil;

    if (!QSSValidModName(mod))
        return;
    QSSDockLaunchApp(@[ @"-nolauncher", @"-game", mod ]);
}

- (void)openQuakeFolder:(id)sender
{
    NSURL *folderURL = QSSDockGameFolderURL();

    (void)sender;
    if (folderURL)
        [[NSWorkspace sharedWorkspace] openURL:folderURL];
}

- (NSMenu *)dockMenu
{
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    NSMenuItem *launcherItem;

    [menu setAutoenablesItems:NO];
    launcherItem = [[[NSMenuItem alloc] initWithTitle:@"Open with Launcher"
                                               action:@selector(openWithLauncher:)
                                        keyEquivalent:@""] autorelease];
    [launcherItem setTarget:self];
    [menu addItem:launcherItem];

    {
        NSURL *gameFolderURL = QSSDockGameFolderURL();
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

    [dockMenu release];
    dockMenu = [menu retain];
    return dockMenu;
}

@end
