/*   SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>

    Feel free to customize this file to suit your needs
*/

#import <Cocoa/Cocoa.h>

extern int    gArgc;
extern char  **gArgv;
extern BOOL   gFinderLaunch;
extern BOOL   gCalledAppMainline;

/* engine entry point, defined in main_sdl.c */
int QSSM_Main (int argc, char **argv);

@interface SDLMain : NSObject
@end
