#import <Cocoa/Cocoa.h>
#include <csignal>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

extern "C" char** g_argv;
extern "C" int g_argc;
extern "C" int WinMain(void*, void*, void*, int);

void OnSignalHandler(int signum)
{
   printf("Exiting from signal: %d\n", signum);
   exit(-9999);
}

@interface VPXAppDelegate : NSObject <NSApplicationDelegate>
- (void)makeWindowExit;

@end

@implementation VPXAppDelegate

// Route quit requests (menu items, Cmd+Q, dock quit) through SDL so the app shuts
// down cleanly via its normal close path; before SDL is up, plain exit is fine.
- (void)requestQuit:(id)sender
{
  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&ev);
  } else {
    exit(0);
  }
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    [self requestQuit:sender];
    return NSTerminateCancel;
  }
  return NSTerminateNow;
}

- (void)buildMenuBar
{
  NSMenu* menubar = [[NSMenu alloc] init];
  NSString* appName = [[NSProcessInfo processInfo] processName];

  NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
  [menubar addItem:appMenuItem];
  NSMenu* appMenu = [[NSMenu alloc] init];
  [appMenu addItemWithTitle:[@"About " stringByAppendingString:appName]
                     action:@selector(orderFrontStandardAboutPanel:)
              keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:appName]
                     action:@selector(hide:)
              keyEquivalent:@"h"];
  NSMenuItem* hideOthers = [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                                      action:@selector(hideOtherApplications:)
                                               keyEquivalent:@"h"];
  hideOthers.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  [appMenu addItem:hideOthers];
  [appMenu addItem:[NSMenuItem separatorItem]];
  NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:appName]
                                                    action:@selector(requestQuit:)
                                             keyEquivalent:@"q"];
  quitItem.target = self;
  [appMenu addItem:quitItem];
  appMenuItem.submenu = appMenu;

  NSMenuItem* fileMenuItem = [[NSMenuItem alloc] init];
  [menubar addItem:fileMenuItem];
  NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
  NSMenuItem* exitItem = [[NSMenuItem alloc] initWithTitle:@"Exit"
                                                    action:@selector(requestQuit:)
                                             keyEquivalent:@""];
  exitItem.target = self;
  [fileMenu addItem:exitItem];
  fileMenuItem.submenu = fileMenu;

  NSMenuItem* windowMenuItem = [[NSMenuItem alloc] init];
  [menubar addItem:windowMenuItem];
  NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
  [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
  [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
  windowMenuItem.submenu = windowMenu;
  [NSApp setWindowsMenu:windowMenu];

  [NSApp setMainMenu:menubar];
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
  [self buildMenuBar];

  if (g_argc == 1) {

    NSOpenPanel* panel = [NSOpenPanel openPanel];

    panel.message = @"Select a Visual Pinball Table (.vpx) File:";
    panel.allowsMultipleSelection = NO;
    panel.canChooseDirectories = NO;

    [panel beginWithCompletionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {

          NSURL* fileURL = panel.URLs[0]; 
          NSString* vpxTableArchiveFile =
              [NSString stringWithUTF8String:[fileURL fileSystemRepresentation]];

          if (vpxTableArchiveFile && vpxTableArchiveFile.length > 0 &&
              [[vpxTableArchiveFile.lowercaseString pathExtension]
                  isEqualToString:@"vpx"]) {
            char** new_argv = (char**)malloc(3 * sizeof(char*));

            new_argv[0] = g_argv[0];
            new_argv[1] = strdup("-play");
            new_argv[2] = strdup([vpxTableArchiveFile UTF8String]);

            g_argc = 3;
            g_argv = new_argv;
          }
        } else {
          NSAlert* alert = [[NSAlert alloc] init];
          [alert setMessageText:
                     @"VPinballX_GL\n\nYou must choose a VPX table to start, run using the command line "
                     @" to set arguments, or\ndouble click a \".vpx\" file."];
          [alert addButtonWithTitle:@"OK"];
          [alert setAlertStyle:NSAlertStyleWarning];
          [panel close];
          [alert runModal];
        }
        [self makeWindowExit];
    }];

  } else if (g_argc > 1) {
    [self makeWindowExit];
  }
}
- (void)makeWindowExit
{
  int status = WinMain(NULL, NULL, NULL, 0);
  exit(status);
}

- (BOOL)application:(NSApplication*)sender openFile:(NSString*)filename
{
  if (g_argc == 1
      && (filename && filename.length > 0 &&
          [[filename.lowercaseString pathExtension] isEqualToString:@"vpx"])) {
    char** new_argv = (char**)malloc(3 * sizeof(char*));

    new_argv[0] = g_argv[0];
    new_argv[1] = strdup("-play");
    new_argv[2] = strdup([filename UTF8String]);

    g_argc = 3;
    g_argv = new_argv;
  }
  [self makeWindowExit];
  return YES;
}

@end

int main(int argc, const char* argv[])
{
  @autoreleasepool {
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = OnSignalHandler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, nullptr);

    g_argc = argc;
    g_argv = (char**)argv;

    NSApplication* vpxApp = [NSApplication sharedApplication];
    VPXAppDelegate* delegate = [[VPXAppDelegate alloc] init];
    [vpxApp setDelegate:delegate];
    [vpxApp run];
  }
  return 0;
}
