#include "AppPlatform_macos.h"
#include "client/gui/screens/DialogDefinitions.h"

#import <Cocoa/Cocoa.h>

/** Builds the accessory view for the "create new world" prompt: name, seed and
    a creative-mode toggle, matching the three strings SelectWorldScreen::tick()
    parses back out (name, seed, "creative"/"survival"). */
static NSView* buildCreateWorldAccessory(NSTextField** outName,
										 NSTextField** outSeed,
										 NSButton** outCreative)
{
	NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 90)];

	NSTextField* nameLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 62, 50, 20)];
	[nameLabel setStringValue:@"Name"];
	[nameLabel setBezeled:NO];
	[nameLabel setDrawsBackground:NO];
	[nameLabel setEditable:NO];
	[nameLabel setSelectable:NO];
	[view addSubview:nameLabel];

	NSTextField* nameField = [[NSTextField alloc] initWithFrame:NSMakeRect(55, 60, 245, 24)];
	[nameField setStringValue:@"World"];
	[view addSubview:nameField];

	NSTextField* seedLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 32, 50, 20)];
	[seedLabel setStringValue:@"Seed"];
	[seedLabel setBezeled:NO];
	[seedLabel setDrawsBackground:NO];
	[seedLabel setEditable:NO];
	[seedLabel setSelectable:NO];
	[view addSubview:seedLabel];

	NSTextField* seedField = [[NSTextField alloc] initWithFrame:NSMakeRect(55, 30, 245, 24)];
	[[seedField cell] setPlaceholderString:@"leave blank for random"];
	[view addSubview:seedField];

	NSButton* creativeBox = [[NSButton alloc] initWithFrame:NSMakeRect(55, 0, 245, 22)];
	[creativeBox setButtonType:NSButtonTypeSwitch];
	[creativeBox setTitle:@"Creative mode"];
	[creativeBox setState:NSControlStateValueOn];
	[view addSubview:creativeBox];

	*outName = nameField;
	*outSeed = seedField;
	*outCreative = creativeBox;
	return view;
}

void AppPlatform_macos::showDialog(int dialogId)
{
	_userInput.clear();
	_userInputStatus = USERINPUT_NOTINITED;

	@autoreleasepool {
		NSAlert* alert = [[NSAlert alloc] init];
		[alert addButtonWithTitle:@"OK"];
		[alert addButtonWithTitle:@"Cancel"];

		NSTextField* nameField = nil;
		NSTextField* seedField = nil;
		NSButton* creativeBox = nil;
		NSTextField* singleField = nil;

		switch (dialogId) {
		case DialogDefinitions::DIALOG_CREATE_NEW_WORLD: {
			[alert setMessageText:@"Create new world"];
			NSView* accessory = buildCreateWorldAccessory(&nameField, &seedField, &creativeBox);
			[alert setAccessoryView:accessory];
			break;
		}
		case DialogDefinitions::DIALOG_RENAME_MP_WORLD:
		case DialogDefinitions::DIALOG_NEW_CHAT_MESSAGE: {
			[alert setMessageText:(dialogId == DialogDefinitions::DIALOG_NEW_CHAT_MESSAGE)
								  ? @"Chat message" : @"Rename world"];
			singleField = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)];
			[alert setAccessoryView:singleField];
			break;
		}
		default:
			// Nothing to collect for informational dialogs.
			_userInputStatus = USERINPUT_CANCEL;
			return;
		}

		NSView* firstResponder = nameField ? (NSView*)nameField : (NSView*)singleField;
		[[alert window] setInitialFirstResponder:firstResponder];

		const NSModalResponse response = [alert runModal];

		if (response != NSAlertFirstButtonReturn) {
			_userInputStatus = USERINPUT_CANCEL;
			return;
		}

		if (nameField) {
			// validateEditing commits text the user typed but never committed
			// by tabbing or pressing return.
			[nameField validateEditing];
			[seedField validateEditing];
			_userInput.push_back([[nameField stringValue] UTF8String]);
			_userInput.push_back([[seedField stringValue] UTF8String]);
			_userInput.push_back([creativeBox state] == NSControlStateValueOn ? "creative" : "survival");
		} else {
			[singleField validateEditing];
			_userInput.push_back([[singleField stringValue] UTF8String]);
		}

		_userInputStatus = USERINPUT_OK;
	}
}

int AppPlatform_macos::getUserInputStatus()
{
	// Consume-once: the screens poll this every tick and only reset their own
	// state when it reads back as something other than "not inited".
	const int status = _userInputStatus;
	_userInputStatus = USERINPUT_NOTINITED;
	return status;
}

StringVector AppPlatform_macos::getUserInput()
{
	return _userInput;
}
