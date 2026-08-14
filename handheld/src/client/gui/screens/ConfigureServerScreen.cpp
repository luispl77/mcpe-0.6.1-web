#include "ConfigureServerScreen.h"
#include "ScreenChooser.h"
#include "../Font.h"
#include "../../Minecraft.h"
#include "../../../AppPlatform.h"

static const int ID_SAVE   = 1;
static const int ID_CANCEL = 2;
static const int ID_CLEAR  = 3;

ConfigureServerScreen::ConfigureServerScreen(unsigned int route, const std::string& name, bool locked)
:	_route(route),
	_locked(locked),
	bSave(NULL),
	bCancel(NULL),
	bClear(NULL),
	tName(10, name),
	tPassword(11, ""),
	_state(STATE_EDITING)
{
	tName.maxLength = 20;
	tName.placeholder = "Server name";

	/* Blank means "leave it as it is", which is the only reading that lets this
	 * screen be opened to rename a world without also being asked to retype a
	 * password nobody wrote down. Clearing one is its own button. */
	tPassword.maxLength = 24;
	tPassword.password = true;
	tPassword.placeholder = locked ? "Leave blank to keep" : "Blank for none";
}

ConfigureServerScreen::~ConfigureServerScreen()
{
	delete bSave;
	delete bCancel;
	delete bClear;
}

void ConfigureServerScreen::init()
{
	bSave   = new Touch::TButton(ID_SAVE,   "Save");
	bCancel = new Touch::TButton(ID_CANCEL, "Cancel");

	buttons.push_back(bSave);
	buttons.push_back(bCancel);

	// Only where there is one to remove.
	if (_locked) {
		bClear = new Touch::TButton(ID_CLEAR, "Remove password");
		buttons.push_back(bClear);
	}

	textBoxes.push_back(&tName);
	textBoxes.push_back(&tPassword);
}

void ConfigureServerScreen::setupPositions()
{
	const int fieldW = 170;
	const int fieldH = 18;
	const int labelSpace = 68;   // "New password" is the longest caption here
	const int groupX = (width - (labelSpace + fieldW)) / 2;
	const int fx = groupX + labelSpace;

	const int by = height - 28;
	const int top = 26;

	int gap = (by - 8 - top - (_locked ? 24 : 0) - fieldH * 2) / 3;
	if (gap < 4)  gap = 4;
	if (gap > 16) gap = 16;

	tName.x = tPassword.x = fx;
	tName.w = tPassword.w = fieldW;
	tName.h = tPassword.h = fieldH;

	tName.y     = top;
	tPassword.y = top + fieldH + gap;

	if (bClear) {
		bClear->x = fx;
		bClear->y = tPassword.y + fieldH + gap;
		bClear->width = fieldW;
		bClear->height = 24;
	}

	bSave->width = bCancel->width = 96;
	bSave->height = bCancel->height = 24;
	bCancel->x = width / 2 - 4 - bCancel->width;
	bSave->x = width / 2 + 4;
	bSave->y = bCancel->y = by;
}

void ConfigureServerScreen::buttonClicked(Button* button)
{
	if (_state != STATE_EDITING)
		return;

	if (button->id == ID_CANCEL) {
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	if (button->id == ID_SAVE) {
		if (tName.text.empty()) {
			_error = "Give the server a name";
			return;
		}
		_error.clear();
		lostFocus();
		// An empty field here means "leave the password as it is", which is why
		// the seam carries that as a flag rather than as an empty string.
		minecraft->platform()->configureServer(_route, tName.text, tPassword.text,
		                                       !tPassword.text.empty());
		_state = STATE_SENDING;
		return;
	}

	if (bClear && button->id == ID_CLEAR) {
		_error.clear();
		lostFocus();
		// The one call that does send a blank password, and means it.
		minecraft->platform()->configureServer(_route, tName.text, std::string(), true);
		_locked = false;
		_state = STATE_SENDING;
	}
}

bool ConfigureServerScreen::handleBackEvent(bool isDown)
{
	if (!isDown && _state == STATE_EDITING)
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
	return true;
}

void ConfigureServerScreen::tick()
{
	if (_state != STATE_SENDING)
		return;

	const int status = minecraft->platform()->createServerStatus();
	if (status < 0)
		return;

	if (status == 1) {
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	_error = "Could not save the changes";
	_state = STATE_EDITING;
}

void ConfigureServerScreen::drawLabel(const std::string& caption, const TextBox& field)
{
	const int w = minecraft->font->width(caption);
	drawString(minecraft->font, caption, field.x - 6 - w, field.y + (field.h - 8) / 2, 0xffa0a0a0);
}

void ConfigureServerScreen::render(int xm, int ym, float a)
{
	renderBackground();

	drawCenteredString(minecraft->font, "Server Settings", width / 2, 8, 0xffffffff);

	drawLabel("Name", tName);
	drawLabel(_locked ? "New password" : "Password", tPassword);

	super::render(xm, ym, a);

	if (_state == STATE_SENDING)
		drawCenteredString(minecraft->font, "Saving...", width / 2, height - 48, 0xffffffff);
	else if (!_error.empty())
		drawCenteredString(minecraft->font, _error, width / 2, height - 48, 0xffff5555);
}

bool ConfigureServerScreen::isInGameScreen() { return false; }


//
// Delete
//

DeleteServerScreen::DeleteServerScreen(unsigned int route, const std::string& name)
:	ConfirmScreen(NULL, "Are you sure you want to delete this server?",
	                    "'" + name + "' will stop and everyone in it will be dropped.",
	                    "Delete", "Cancel", 0),
	_route(route),
	_deleting(false)
{
	// Cancel, not Delete, under the finger to begin with.
	tabButtonIndex = 1;
}

void DeleteServerScreen::postResult(bool isOk)
{
	if (!isOk) {
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	minecraft->platform()->deleteServer(_route);
	_deleting = true;
}

void DeleteServerScreen::tick()
{
	if (!_deleting)
		return;

	const int status = minecraft->platform()->createServerStatus();
	if (status < 0)
		return;

	if (status == 1) {
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	// Stays on this screen to say so. Dropping back to the list on a failure
	// would look exactly like a delete that worked, right up until the row
	// reappeared on the next poll.
	_error = "Could not delete the server";
	_deleting = false;
}

void DeleteServerScreen::render(int xm, int ym, float a)
{
	super::render(xm, ym, a);

	if (_deleting)
		drawCenteredString(minecraft->font, "Deleting...", width / 2, height - 48, 0xffffffff);
	else if (!_error.empty())
		drawCenteredString(minecraft->font, _error, width / 2, height - 48, 0xffff5555);
}
