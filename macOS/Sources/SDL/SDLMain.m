/*   SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>

    Feel free to customize this file to suit your needs
*/

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
#import <sys/param.h> /* for MAXPATHLEN */
#import <unistd.h>
#import "SDLApplication.h"

int    gArgc;
char  **gArgv;
BOOL   gFinderLaunch;
BOOL   gCalledAppMainline = FALSE;

/* woods: show download progress on the dock icon (see Sys_SetDockProgress in sys.h)

   The stock -[NSDockTile setBadgeLabel:] always draws the system red badge,
   which can't be recoloured. So instead we install a custom content view that
   redraws the app icon plus a Chrome-style circular progress indicator at the
   bottom-right, with the filled arc drawn in our own colour. */

#define DOCKBADGE_R (0x8f / 255.0)
#define DOCKBADGE_G (0x58 / 255.0)
#define DOCKBADGE_B (0x48 / 255.0)

@interface QSSDockProgressView : NSView
{
    float progress;     /* 0..1 download fraction, or <0 to hide the badge */
}
- (void) setProgress:(float)p;
@end

@implementation QSSDockProgressView

- (void) setProgress:(float)p
{
    progress = p;
    [self setNeedsDisplay:YES];
}

- (void) drawRect:(NSRect)rect
{
    NSRect b = [self bounds];

    /* draw the normal application icon underneath */
    [[NSApp applicationIconImage] drawInRect:b fromRect:NSZeroRect
        operation:NSCompositingOperationCopy fraction:1.0];

    if (progress < 0.0f)
        return;

    /* white disc anchored at the bottom-right (non-flipped coords: y grows up) */
    CGFloat radius = b.size.width * 0.17;
    CGFloat margin = b.size.width * 0.045;
    NSPoint c = NSMakePoint(b.size.width - radius - margin, radius + margin);
    NSRect discRect = NSMakeRect(c.x - radius, c.y - radius, radius * 2.0, radius * 2.0);

    NSBezierPath *disc = [NSBezierPath bezierPathWithOvalInRect:discRect];
    [[NSColor whiteColor] setFill];
    [disc fill];

    /* progress ring sits flush with the disc edge (no white rim outside it),
       like Chrome: a thin track + coloured arc for the completed fraction */
    CGFloat ringWidth = radius * 0.32;
    CGFloat ringRadius = radius - ringWidth * 0.5;	/* outer edge = disc edge */
    NSColor *fg = [NSColor colorWithSRGBRed:DOCKBADGE_R green:DOCKBADGE_G blue:DOCKBADGE_B alpha:1.0];

    NSBezierPath *track = [NSBezierPath bezierPath];
    [track appendBezierPathWithArcWithCenter:c radius:ringRadius startAngle:0.0 endAngle:360.0];
    [track setLineWidth:ringWidth];
    [[NSColor colorWithWhite:0.85 alpha:1.0] setStroke];
    [track stroke];

    if (progress > 0.0f)
    {
        /* Draw the completed arc with a subtle gradient (like Chrome) by clipping
           to the stroked arc and filling it with an NSGradient. Built with Core
           Graphics so it works below 10.14 (no -[NSBezierPath CGPath]). */
        CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
        CGFloat sweep = MIN(progress, 1.0f) * (CGFloat)(2.0 * M_PI);

        CGContextSaveGState(ctx);
        if (sweep >= (CGFloat)(2.0 * M_PI) - 0.001f)
            CGContextAddArc(ctx, c.x, c.y, ringRadius, 0.0f, (CGFloat)(2.0 * M_PI), 0);
        else /* clockwise from 12 o'clock */
            CGContextAddArc(ctx, c.x, c.y, ringRadius, (CGFloat)M_PI_2, (CGFloat)M_PI_2 - sweep, 1);
        CGContextSetLineWidth(ctx, ringWidth);
        CGContextSetLineCap(ctx, kCGLineCapRound);
        CGContextReplacePathWithStrokedPath(ctx);
        CGContextClip(ctx);

        NSGradient *grad = [[[NSGradient alloc]
            initWithStartingColor:[fg shadowWithLevel:0.18]
                      endingColor:[fg highlightWithLevel:0.22]] autorelease];
        [grad drawInRect:discRect angle:65.0];	/* lighter toward the upper-right */
        CGContextRestoreGState(ctx);
    }

    /* download count in the centre (Chrome shows the number of active downloads;
       the engine fetches one file at a time, so this is always "1") */
    NSMutableParagraphStyle *ps = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [ps setAlignment:NSTextAlignmentCenter];
    NSDictionary *attrs = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSFont boldSystemFontOfSize:radius * 1.05], NSFontAttributeName,
        fg, NSForegroundColorAttributeName,
        ps, NSParagraphStyleAttributeName,
        nil];
    NSString *glyph = @"1";
    NSSize ts = [glyph sizeWithAttributes:attrs];
    /* nudge left so the vertical stem of the "1" (not its top flag) reads centred */
    CGFloat nudge = radius * 0.07;
    [glyph drawInRect:NSMakeRect(c.x - radius - nudge, c.y - ts.height * 0.5, radius * 2.0, ts.height)
        withAttributes:attrs];
}

@end

static QSSDockProgressView *dockProgressView = nil;

void Sys_SetDockProgress (float fraction)
{
    /* fraction is captured by value; all AppKit access (NSApp/NSDockTile/the
       content view) happens inside the block so the function is safe to call
       from any thread. */
    dispatch_block_t apply = ^{
        @autoreleasepool {
            NSDockTile *tile = [NSApp dockTile];
            if (!tile)
                return;
            if (fraction >= 0.0f)
            {
                if (!dockProgressView)
                    dockProgressView = [[QSSDockProgressView alloc] initWithFrame:NSMakeRect(0, 0, 128, 128)];
                [dockProgressView setProgress:fraction];
                if ([tile contentView] != dockProgressView)
                    [tile setContentView:dockProgressView];
                [tile display];
            }
            else if ([tile contentView] != nil)
            {
                /* restore the default Dock icon when there's nothing to show */
                [tile setContentView:nil];
                [tile display];
            }
        }
    };
    if ([NSThread isMainThread])
        apply();
    else
        dispatch_async (dispatch_get_main_queue(), apply);
}

/* The main class of the application, the application's delegate */
@implementation SDLMain

/* Set the working directory to the .app's parent directory */
- (void) setupWorkingDirectory:(BOOL)shouldChdir
{
    if (shouldChdir)
    {
        char parentdir[MAXPATHLEN];
		CFURLRef url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
		CFURLRef url2 = CFURLCreateCopyDeletingLastPathComponent(0, url);
		if (CFURLGetFileSystemRepresentation(url2, true, (UInt8 *)parentdir, MAXPATHLEN)) {
	        assert ( chdir (parentdir) == 0 );   /* chdir to the binary app's parent */
		}
		CFRelease(url);
		CFRelease(url2);
	}

}

/* Called when the internal event loop has just started running */
- (void) applicationDidFinishLaunching: (NSNotification *) note
{
    int status;

    /* Set the working directory to the .app's parent directory */
    [self setupWorkingDirectory:gFinderLaunch];

    /* Hand off to main application code */
    gCalledAppMainline = TRUE;
    status = SDL_main (gArgc, gArgv);

    /* We're done, thank you for playing */
    exit(status);
}
@end


#ifdef main
#  undef main
#endif


static int IsRootCwd()
{
    char buf[MAXPATHLEN];
    char *cwd = getcwd(buf, sizeof (buf));
    return (cwd && (strcmp(cwd, "/") == 0));
}

static int IsFinderLaunch(const int argc, char **argv)
{
    /* -psn_XXX is passed if we are launched from Finder, SOMETIMES */
    if ( (argc >= 2) && (strncmp(argv[1], "-psn", 4) == 0) ) {
        return 1;
    } else if ((argc == 1) && IsRootCwd()) {
        /* we might still be launched from the Finder; on 10.9+, you might not
        get the -psn command line anymore. If there's no
        command line, and if our current working directory is "/", it
        might as well be a Finder launch. */
        return 1;
    }
    return 0;  /* not a Finder launch. */
}

/* Main entry point to executable - should *not* be SDL_main! */
int main (int argc, char **argv)
{
    /* Copy the arguments into a global variable */
    if (IsFinderLaunch(argc, argv)) {
        gArgv = (char **) SDL_malloc(sizeof (char *) * 2);
        gArgv[0] = argv[0];
        gArgv[1] = NULL;
        gArgc = 1;
        gFinderLaunch = YES;
    } else {
        int i;
        gArgc = argc;
        gArgv = (char **) SDL_malloc(sizeof (char *) * (argc+1));
        for (i = 0; i <= argc; i++)
            gArgv[i] = argv[i];
        gFinderLaunch = NO;
    }

    NSApplicationMain (argc, (const char**) argv);
    return 0;
}
