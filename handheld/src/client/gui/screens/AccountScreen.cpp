#include "AccountScreen.h"
#include "ScreenChooser.h"
#include "../Font.h"
#include "../../Minecraft.h"
#include "../../../AppPlatform.h"

static const int ID_LOGIN  = 1;
static const int ID_CREATE = 2;
static const int ID_LOGOUT = 3;
static const int ID_BACK   = 4;

AccountScreen::AccountScreen()
:	bLogIn(NULL),
	bCreate(NULL),
	bLogOut(NULL),
	bBack(NULL),
	tUser(10, ""),
	tPassword(11, ""),
	_state(STATE_EDITING),
	_creating(false)
{
	// Matches what the manager will accept, so the rules are visible before
	// they are enforced rather than only in a refusal.
	tUser.placeholder = "3-16 letters or numbers";
	tUser.maxLength = 16;

	tPassword.placeholder = "At least 6 characters";
	tPassword.maxLength = 64;
	tPassword.password = true;
}

AccountScreen::~AccountScreen()
{
	delete bLogIn;
	delete bCreate;
	delete bLogOut;
	delete bBack;
}

void AccountScreen::init()
{
	_name = minecraft->platform()->accountName();

	bBack = new Touch::TButton(ID_BACK, "Back");

	if (_name.empty()) {
		bLogIn  = new Touch::TButton(ID_LOGIN,  "Log in");
		bCreate = new Touch::TButton(ID_CREATE, "Create account");
		buttons.push_back(bLogIn);
		buttons.push_back(bCreate);

		textBoxes.push_back(&tUser);
		textBoxes.push_back(&tPassword);

		// Straight into the name field: nobody opens this screen for any other
		// reason, and on a phone the keyboard is already up from the tap that
		// got them here.
		tUser.setFocus(minecraft);
	} else {
		bLogOut = new Touch::TButton(ID_LOGOUT, "Log out");
		buttons.push_back(bLogOut);
	}

	buttons.push_back(bBack);
}

void AccountScreen::setupPositions()
{
	const int by = height - 28;

	bBack->width = 96;
	bBack->height = 24;
	bBack->x = width / 2 - bBack->width / 2;
	bBack->y = by;

	if (!_name.empty()) {
		bLogOut->width = 140;
		bLogOut->height = 24;
		bLogOut->x = width / 2 - bLogOut->width / 2;
		bLogOut->y = height / 2 - 6;
		return;
	}

	const int fieldW = 150;
	const int fieldH = 18;
	const int btnH   = 24;

	/* Captions beside the boxes rather than above them, for the reason
	 * CreateServerScreen documents: the touch GUI is about 175 units tall and
	 * a caption above a box lands a pixel or two under the box before it,
	 * which at 2.75x reads as a rendering fault. */
	const int labelSpace = 56;
	const int groupX = (width - (labelSpace + fieldW)) / 2;
	const int fx = groupX + labelSpace;

	const int top = 26;
	int gap = (by - 8 - top - btnH - fieldH * 2) / 3;
	if (gap < 4)  gap = 4;
	if (gap > 14) gap = 14;

	tUser.x = tPassword.x = fx;
	tUser.w = tPassword.w = fieldW;
	tUser.h = tPassword.h = fieldH;

	tUser.y     = top;
	tPassword.y = top + fieldH + gap;

	/* Log in is the wider of the two and sits on the left, because signing in
	 * is what almost everyone is here to do and making an account is what you
	 * do once. Both on one row so neither is the one you reach by accident. */
	const int row = tPassword.y + fieldH + gap;
	bLogIn->width = 96;
	bCreate->width = 120;
	bLogIn->height = bCreate->height = btnH;
	const int total = bLogIn->width + 8 + bCreate->width;
	bLogIn->x  = width / 2 - total / 2;
	bCreate->x = bLogIn->x + bLogIn->width + 8;
	bLogIn->y  = bCreate->y = row;
}

void AccountScreen::submit(bool createNew)
{
	if (tUser.text.empty()) {
		_error = "Type a name";
		return;
	}
	if (tPassword.text.empty()) {
		_error = "Type a password";
		return;
	}

	_error.clear();
	lostFocus();   // the keyboard has no more to say to this screen
	_creating = createNew;
	minecraft->platform()->logIn(tUser.text, tPassword.text, createNew);
	_state = STATE_SENDING;
}

void AccountScreen::buttonClicked(Button* button)
{
	if (_state != STATE_EDITING)
		return;

	if (button->id == ID_BACK) {
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	if (button->id == ID_LOGIN)  { submit(false); return; }
	if (button->id == ID_CREATE) { submit(true);  return; }

	if (button->id == ID_LOGOUT) {
		/* Nothing is told to forget anything at the far end -- the token is
		 * signed rather than stored there, which is what stops a restart of
		 * the manager signing everybody out. Dropping it here is what signing
		 * out means, and on a shared machine the real answer is to change the
		 * password, which invalidates every token ever issued. */
		minecraft->platform()->logOut();
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
	}
}

bool AccountScreen::handleBackEvent(bool isDown)
{
	if (!isDown && _state == STATE_EDITING)
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
	return true;
}

void AccountScreen::tick()
{
	if (_state != STATE_SENDING)
		return;

	const int status = minecraft->platform()->logInStatus();
	if (status < 0)
		return;

	if (status == 1) {
		// Back to the list, which is where the answer shows: the button that
		// said "Sign in" now says the name, and worlds that are yours have
		// their Settings and Delete.
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	/* The manager's own words where it gave any -- "that name is taken",
	 * "password: at least 6 characters". It knows which rule was broken and
	 * this does not, and a screen that says "could not sign in" to somebody
	 * whose password is five characters long is a screen they will try four
	 * more times. */
	_error = minecraft->platform()->logInError();
	if (_error.empty())
		_error = _creating ? "Could not create the account" : "Wrong name or password";
	_state = STATE_EDITING;
}

void AccountScreen::drawLabel(const std::string& caption, const TextBox& field)
{
	const int w = minecraft->font->width(caption);
	drawString(minecraft->font, caption, field.x - 6 - w, field.y + (field.h - 8) / 2, 0xffa0a0a0);
}

void AccountScreen::render(int xm, int ym, float a)
{
	renderBackground();

	if (_name.empty()) {
		drawCenteredString(minecraft->font, "Sign in", width / 2, 6, 0xffffffff);
		drawLabel("Name",     tUser);
		drawLabel("Password", tPassword);

		super::render(xm, ym, a);

		if (_state == STATE_SENDING)
			drawCenteredString(minecraft->font, _creating ? "Creating account..." : "Signing in...",
			                   width / 2, height - 48, 0xffffffff);
		else if (!_error.empty())
			drawCenteredString(minecraft->font, _error, width / 2, height - 48, 0xffff5555);
		else
			// Said here rather than nowhere: somebody looking at a login on a
			// game that never had one deserves to know what it is for.
			drawCenteredString(minecraft->font, "Only for your servers. The game works signed out.",
			                   width / 2, height - 48, 0xffa0a0a0);
		return;
	}

	drawCenteredString(minecraft->font, "Account", width / 2, 6, 0xffffffff);
	drawCenteredString(minecraft->font, "Signed in as " + _name, width / 2, height / 2 - 26, 0xffffffff);

	super::render(xm, ym, a);

	drawCenteredString(minecraft->font, "Your servers are yours on any device you sign in on.",
	                   width / 2, height - 48, 0xffa0a0a0);
}

bool AccountScreen::isInGameScreen() { return false; }
