#include "ServerPasswordScreen.h"
#include "ScreenChooser.h"
#include "ProgressScreen.h"
#include "../Font.h"
#include "../../Minecraft.h"
#include "../../../AppPlatform.h"

static const int ID_JOIN   = 1;
static const int ID_CANCEL = 2;

ServerPasswordScreen::ServerPasswordScreen(const PingedCompatibleServer& server, unsigned int route)
:	_server(server),
	_route(route),
	bJoin(NULL),
	bCancel(NULL),
	tPassword(10, ""),
	_state(STATE_EDITING),
	_usedSaved(false)
{
	tPassword.placeholder = "Password";
	tPassword.maxLength = 24;
	tPassword.password = true;
}

ServerPasswordScreen::~ServerPasswordScreen()
{
	delete bJoin;
	delete bCancel;
}

void ServerPasswordScreen::init()
{
	bJoin   = new Touch::TButton(ID_JOIN,   "Join");
	bCancel = new Touch::TButton(ID_CANCEL, "Cancel");

	buttons.push_back(bJoin);
	buttons.push_back(bCancel);

	textBoxes.push_back(&tPassword);

	/* If this password has been typed once already, try it before asking again.
	   The screen still appears -- it is where "wrong password" has to be said
	   if the owner has changed it since -- but for the ordinary case it is a
	   frame of "Using saved password..." on the way into the world.

	   The game is never handed the secret. An empty password means "use the one
	   you kept", which cannot be confused with somebody submitting a blank
	   field because buttonClicked refuses to send one. */
	if (minecraft->platform()->hasServerPassword(_route)) {
		_usedSaved = true;
		_state = STATE_SENDING;
		minecraft->platform()->unlockServer(_route, std::string());
		return;
	}

	// There is one field and one reason to be here.
	tPassword.setFocus(minecraft);
}

void ServerPasswordScreen::setupPositions()
{
	const int fieldW = 150;
	const int fieldH = 18;

	tPassword.w = fieldW;
	tPassword.h = fieldH;
	tPassword.x = width / 2 - fieldW / 2;
	tPassword.y = height / 2 - 4;

	bJoin->width = bCancel->width = 96;
	bJoin->height = bCancel->height = 24;
	bCancel->x = width / 2 - 4 - bCancel->width;
	bJoin->x = width / 2 + 4;
	bJoin->y = bCancel->y = height - 28;
}

void ServerPasswordScreen::buttonClicked(Button* button)
{
	if (_state != STATE_EDITING)
		return;

	if (button->id == ID_CANCEL) {
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
		return;
	}

	if (button->id == ID_JOIN) {
		if (tPassword.text.empty()) {
			_error = "Type the password";
			return;
		}
		_error.clear();
		lostFocus();
		minecraft->platform()->unlockServer(_route, tPassword.text);
		_state = STATE_SENDING;
	}
}

bool ServerPasswordScreen::handleBackEvent(bool isDown)
{
	if (!isDown && _state == STATE_EDITING)
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
	return true;
}

void ServerPasswordScreen::tick()
{
	if (_state != STATE_SENDING)
		return;

	const int status = minecraft->platform()->createServerStatus();
	if (status < 0)
		return;

	if (status == 1) {
		// The door is open for this socket now, so the join that starts here is
		// one the switch will carry rather than silently drop.
		minecraft->joinMultiplayer(_server);
		minecraft->setScreen(new ProgressScreen());
		return;
	}

	/* Stays on this screen with the field intact. A wrong password is the
	 * ordinary reason to be here twice, and dropping back to the list would
	 * make it look like the join itself had failed. */
	if (_usedSaved) {
		// Said differently, because nobody typed anything: the password that
		// worked last time does not any more, which means the owner changed it.
		// The page has already dropped it, so the field below is the way in.
		_error = "The saved password no longer works";
		_usedSaved = false;
	} else {
		_error = "Wrong password";
	}
	_state = STATE_EDITING;
	tPassword.setFocus(minecraft);
}

void ServerPasswordScreen::render(int xm, int ym, float a)
{
	renderBackground();

	drawCenteredString(minecraft->font, "This server has a password", width / 2, 6, 0xffffffff);
	drawCenteredString(minecraft->font, _server.name.C_String(), width / 2, tPassword.y - 16, 0xffffffb0);

	super::render(xm, ym, a);

	if (_state == STATE_SENDING)
		drawCenteredString(minecraft->font, "Checking...", width / 2, height - 48, 0xffffffff);
	else if (!_error.empty())
		drawCenteredString(minecraft->font, _error, width / 2, height - 48, 0xffff5555);
}

bool ServerPasswordScreen::isInGameScreen() { return false; }
