#include "CreateServerScreen.h"
#include "ScreenChooser.h"
#include "../Font.h"
#include "../../Minecraft.h"
#include "../../../AppPlatform.h"

static const int ID_CREATE = 1;
static const int ID_CANCEL = 2;
static const int ID_MODE   = 3;

CreateServerScreen::CreateServerScreen()
:	bCreate(NULL),
	bCancel(NULL),
	bMode(NULL),
	tName(10, ""),
	tSeed(11, ""),
	tPassword(12, ""),
	creative(false),
	_state(STATE_EDITING)
{
	tName.placeholder = "Server name";
	tName.maxLength = 20;

	tSeed.placeholder = "Blank for random";
	tSeed.maxLength = 20;

	tPassword.placeholder = "Blank for none";
	tPassword.maxLength = 24;
	tPassword.password = true;
}

CreateServerScreen::~CreateServerScreen()
{
	delete bCreate;
	delete bCancel;
	delete bMode;
}

void CreateServerScreen::init()
{
	bCreate = new Touch::TButton(ID_CREATE, "Create");
	bCancel = new Touch::TButton(ID_CANCEL, "Cancel");
	bMode   = new Touch::TButton(ID_MODE,   "");
	updateModeLabel();

	buttons.push_back(bMode);
	buttons.push_back(bCreate);
	buttons.push_back(bCancel);

	textBoxes.push_back(&tName);
	textBoxes.push_back(&tSeed);
	textBoxes.push_back(&tPassword);

	// Straight into the first field. Somebody who opened this screen came here
	// to type a name, and on a phone the keyboard is already up from the tap
	// that got them here.
	tName.setFocus(minecraft);
}

void CreateServerScreen::setupPositions()
{
	const int fieldW = 170;
	const int fieldH = 18;
	const int modeH  = 24;

	/* Captions sit to the left of their boxes rather than above them. Above was
	 * the obvious way round and it does not survive the touch GUI: that one is
	 * only about 175 units tall, and three captions plus three boxes plus the
	 * mode button plus a row of buttons leaves each caption a pixel or two under
	 * the box above it -- which at 2.75x reads as a rendering fault. Beside them
	 * the rows cost nothing but width, of which there is plenty. */
	const int labelSpace = 56;
	const int groupX = (width - (labelSpace + fieldW)) / 2;
	const int fx = groupX + labelSpace;

	const int by = height - 28;          // the Create / Cancel row
	const int top = 22;                  // clear of the title

	// Whatever is left over goes between the rows rather than under them.
	int gap = (by - 8 - top - modeH - fieldH * 3) / 3;
	if (gap < 4)  gap = 4;
	if (gap > 16) gap = 16;
	const int pitch = fieldH + gap;

	tName.x = tSeed.x = tPassword.x = fx;
	tName.w = tSeed.w = tPassword.w = fieldW;
	tName.h = tSeed.h = tPassword.h = fieldH;

	tName.y     = top;
	tSeed.y     = top + pitch;
	tPassword.y = top + pitch * 2;

	bMode->x = fx;
	bMode->y = top + pitch * 3;
	bMode->width = fieldW;
	bMode->height = modeH;

	bCreate->width = bCancel->width = 96;
	bCreate->height = bCancel->height = 24;
	bCancel->x = width / 2 - 4 - bCancel->width;
	bCreate->x = width / 2 + 4;
	bCreate->y = bCancel->y = by;
}

void CreateServerScreen::updateModeLabel()
{
	if (bMode)
		bMode->msg = creative ? "Mode: Creative" : "Mode: Survival";
}

void CreateServerScreen::buttonClicked(Button* button)
{
	if (_state != STATE_EDITING)
		return;

	if (button->id == ID_MODE) {
		creative = !creative;
		updateModeLabel();
		return;
	}

	if (button->id == ID_CANCEL) {
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	if (button->id == ID_CREATE) {
		if (tName.text.empty()) {
			// Said rather than silently refused: a greyed-out Create leaves
			// somebody looking for what is wrong with the seed.
			_error = "Give the server a name";
			return;
		}

		_error.clear();
		lostFocus();   // the keyboard has no more to say to this screen
		minecraft->platform()->createServer(tName.text,
		                                    creative ? "creative" : "survival",
		                                    tSeed.text,
		                                    tPassword.text);
		_state = STATE_SENDING;
	}
}

bool CreateServerScreen::handleBackEvent(bool isDown)
{
	if (!isDown && _state == STATE_EDITING)
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
	return true;
}

void CreateServerScreen::tick()
{
	if (_state != STATE_SENDING)
		return;

	/* The manager answers over the network whenever it answers; until then this
	 * reads back NOTINITED, which is the ordinary state and not a stall. */
	const int status = minecraft->platform()->createServerStatus();
	if (status < 0)
		return;

	if (status == 1) {
		// Nothing more to do here: the world announces itself to the board and
		// turns up in the list the way everybody else's does.
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	_error = "Could not create the server";
	_state = STATE_EDITING;
}

void CreateServerScreen::drawLabel(const std::string& caption, const TextBox& field)
{
	const int w = minecraft->font->width(caption);
	drawString(minecraft->font, caption, field.x - 6 - w, field.y + (field.h - 8) / 2, 0xffa0a0a0);
}

void CreateServerScreen::render(int xm, int ym, float a)
{
	renderBackground();

	drawCenteredString(minecraft->font, "New Server", width / 2, 6, 0xffffffff);

	// Right-aligned against the boxes, so the three captions make a column edge
	// instead of three ragged starts.
	drawLabel("Name",     tName);
	drawLabel("Seed",     tSeed);
	drawLabel("Password", tPassword);

	super::render(xm, ym, a);

	if (_state == STATE_SENDING)
		drawCenteredString(minecraft->font, "Creating server...", width / 2, height - 48, 0xffffffff);
	else if (!_error.empty())
		drawCenteredString(minecraft->font, _error, width / 2, height - 48, 0xffff5555);
}

bool CreateServerScreen::isInGameScreen() { return false; }
