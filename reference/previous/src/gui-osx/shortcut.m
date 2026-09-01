/*
 Previous - shortcut.m
 
 This file is distributed under the GNU General Public License, version 2
 or at your option any later version. Read the file gpl.txt for details.
 
 This file listens for the application to launch and for the window to open
 and then modifies the menu items and toolbar buttons to avoid conflicts
 with the guest operating system and to remove fullscreen which does not
 work correctly when activated by macOS. Rather fullscreen should be activated
 via the shortcut (Control + Option + F).
 
 Contributed by Jeffrey Bergier on 2024/11/01
 */

#import <AppKit/AppKit.h>

#define PRINT_DEBUG_LOG 0

@interface PreviousGUI: NSObject
@end

@interface NSMenuItem (Previous)
/* Returns YES if the modification was made */
-(BOOL)PREV_setKeyEquivalentModifierMask:(NSEventModifierFlags)desiredMask
						  ifExpectedMask:(NSEventModifierFlags)expectedMask
						  andExpectedKey:(NSString*)expectedKey;
-(BOOL)PREV_removeFromMenu;
@end

@interface NSMenu (Previous)
-(NSArray*)PREV_itemArrayWithSubitems;
@end

@implementation PreviousGUI

/* MARK: Subscribe to notifications */

+(void)load
{
	[[NSNotificationCenter defaultCenter] addObserver:self
											 selector:@selector(applicationDidFinishLaunching:)
												 name:NSApplicationDidFinishLaunchingNotification
											   object:nil];
	[[NSNotificationCenter defaultCenter] addObserver:self
											 selector:@selector(windowDidBecomeKey:)
												 name:NSWindowDidBecomeKeyNotification
											   object:nil];
}

/* MARK: Menu Shortcut Customizing */
+(void)applicationDidFinishLaunching:(NSNotification*)aNotification
{
	NSInteger processd = 0;
	NSInteger modified = 0;
	NSArray *menuItems = [[(NSApplication*)[aNotification object] mainMenu] PREV_itemArrayWithSubitems];
	
	for (NSMenuItem *item in menuItems) {
		SEL action = [item action];
		if (action == @selector(terminate:)) { /* Quit Application */
			modified += [item PREV_setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption
												 ifExpectedMask:NSEventModifierFlagCommand
												 andExpectedKey:@"q"];
		} else if (action == @selector(hide:)) { /* Hide Application */
			modified += [item PREV_setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption
												 ifExpectedMask:NSEventModifierFlagCommand
												 andExpectedKey:@"h"];
		} else if (action == @selector(hideOtherApplications:)) { /* Hide Other Applications */
			modified += [item PREV_setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption | NSEventModifierFlagShift
												 ifExpectedMask:NSEventModifierFlagCommand | NSEventModifierFlagOption
												 andExpectedKey:@"h"];
		} else if (action == @selector(performMiniaturize:)) { /* Minimize Window */
			modified += [item PREV_setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption
												 ifExpectedMask:NSEventModifierFlagCommand
												 andExpectedKey:@"m"];
		} else if (action == @selector(performClose:)) { /* Close Window */
			modified += [item PREV_setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption
												 ifExpectedMask:NSEventModifierFlagCommand
												 andExpectedKey:@"w"];
		} else if ([[item keyEquivalent] isEqualToString:@","]) { /* Open Application Settings */
			modified += [item PREV_setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption
												 ifExpectedMask:NSEventModifierFlagCommand
												 andExpectedKey:@","];
		} else if (action == @selector(closeAll:)) { /* Close All Windows */
			modified += [item PREV_removeFromMenu];
		} else if (action == @selector(toggleFullScreen:)) { /* Enter Full Screen Mode */
			modified += [item PREV_removeFromMenu];
		} else {
			continue;
		}
		processd += 1;
	}
	
	[[NSNotificationCenter defaultCenter] removeObserver:self
													name:NSApplicationDidFinishLaunchingNotification
												  object:nil];
#if PRINT_DEBUG_LOG
	NSLog(@"%@: Processed Main Menu: %ld total -> %ld processed -> %ld modified",
		  self, [menuItems count], processd, modified);
#else
	(void)processd;
	(void)modified;
#endif
}

/* MARK: Window Zoom Button Disabling */
+(void)windowDidBecomeKey:(NSNotification*)aNotification
{
	NSWindow *window = [aNotification object];
	if ([window collectionBehavior] & NSWindowCollectionBehaviorFullScreenNone) { return; }
	
	Class sdl3WindowClass = NSClassFromString(@"SDL3Window");
	Class sdl2WindowClass = NSClassFromString(@"SDLWindow");
	if (![window isKindOfClass:sdl3WindowClass] && ![window isKindOfClass:sdl2WindowClass]) { return; }
	
	[window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenNone];
	[[NSNotificationCenter defaultCenter] removeObserver:self
													name:NSWindowDidBecomeKeyNotification
												  object:nil];
#if PRINT_DEBUG_LOG
	NSLog(@"%@: %@ Disabled Full Screen Button",self, window);
#endif
}

@end

@implementation NSMenuItem (Previous)

-(BOOL)PREV_setKeyEquivalentModifierMask:(NSEventModifierFlags)desiredMask
						  ifExpectedMask:(NSEventModifierFlags)expectedMask
						  andExpectedKey:(NSString*)expectedKey
{
	if ([[self keyEquivalent] isEqualToString:expectedKey]
		&& [self keyEquivalentModifierMask] == expectedMask)
	{
		[self setKeyEquivalentModifierMask:desiredMask];
		return YES;
	}
	return NO;
}

-(BOOL)PREV_removeFromMenu
{
	[[self menu] removeItem:self];
	return YES;
}

@end

@implementation NSMenu (Previous)

-(NSArray*)PREV_itemArrayWithSubitems
{
	NSMutableArray *output = [[NSMutableArray new] autorelease];
	for (NSMenuItem *item in [self itemArray]) {
		[output addObject:item];
		if ([item hasSubmenu]) {
			[output addObjectsFromArray:[[item submenu] PREV_itemArrayWithSubitems]];
		}
	}
	return [[output copy] autorelease];
}

@end
