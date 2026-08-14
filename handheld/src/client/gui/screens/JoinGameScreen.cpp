#include "JoinGameScreen.h"
#include "StartMenuScreen.h"
#include "ProgressScreen.h"
#include "DialogDefinitions.h"
#include "../Font.h"
#include "../../../network/RakNetInstance.h"

JoinGameScreen::JoinGameScreen()
:	bJoin(  2, "Join Game"),
	bBack(  3, "Back"),
	bCreate(4, "New Server"),
	gamesList(NULL),
	_canCreate(false),
	_creating(CREATE_IDLE)
{
	bJoin.active = false;
	//gamesList->yInertia = 0.5f;
}

JoinGameScreen::~JoinGameScreen()
{
	delete gamesList;
}

void JoinGameScreen::buttonClicked(Button* button)
{
	if (button->id == bJoin.id)
	{
		if (isIndexValid(gamesList->selectedItem))
		{
			PingedCompatibleServer selectedServer = gamesList->copiedServerList[gamesList->selectedItem];
			minecraft->joinMultiplayer(selectedServer);
			{
				bJoin.active = false;
				bBack.active = false;
				minecraft->setScreen(new ProgressScreen());
			}
		}
		//minecraft->locateMultiplayer();
		//minecraft->setScreen(new JoinGameScreen());
	}
	if (button->id == bCreate.id && _creating == CREATE_IDLE)
	{
		/* The name and password are collected by the platform, not by a text
		 * field of our own: 0.6.1's GUI has no password field and the platform
		 * already owns every other piece of text entry in the game. */
		_createError.clear();
		_creating = CREATE_ASKING;
		minecraft->platform()->createUserInput(DialogDefinitions::DIALOG_CREATE_SERVER);
	}
	if (button->id == bBack.id)
	{
		minecraft->cancelLocateMultiplayer();
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
	}
}

bool JoinGameScreen::handleBackEvent(bool isDown)
{
	if (!isDown)
	{
		minecraft->cancelLocateMultiplayer();
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
	}
	return true;
}


bool JoinGameScreen::isIndexValid( int index )
{
	return gamesList && index >= 0 && index < gamesList->getNumberOfItems();
}

void JoinGameScreen::tick()
{
	/* Two waits, one after the other, and neither of them blocks a frame: the
	 * player answering the dialog, then the manager answering the request. Both
	 * report USERINPUT_NOTINITED until they have something to say, so sitting
	 * here doing nothing is the ordinary state and not a stall. */
	if (_creating == CREATE_ASKING) {
		// 1 is OK and 0 is Cancel, the same bare values every other screen here
		// tests -- the named constants live on the SDL platform header, and a
		// screen that also builds for Android and win32 should not reach for it.
		const int status = minecraft->platform()->getUserInputStatus();
		if (status > -1) {
			if (status == 1) {
				const std::vector<std::string> answer = minecraft->platform()->getUserInput();
				const std::string name     = answer.size() > 0 ? answer[0] : std::string();
				const std::string password = answer.size() > 1 ? answer[1] : std::string();
				minecraft->platform()->createServer(name, password);
				_creating = CREATE_SENDING;
			} else {
				_creating = CREATE_IDLE;
			}
		}
	} else if (_creating == CREATE_SENDING) {
		const int status = minecraft->platform()->createServerStatus();
		if (status > -1) {
			// Nothing to do on success: the world announces itself to the board
			// and arrives in the list the same way everybody else's does.
			if (status != 1)
				_createError = "Could not create the server";
			_creating = CREATE_IDLE;
		}
	}

	bCreate.active = _canCreate && _creating == CREATE_IDLE;

	const ServerList& orgServerList = minecraft->raknetInstance->getServerList();
	ServerList serverList;
	for (unsigned int i = 0; i < orgServerList.size(); ++i)
		if (orgServerList[i].name.GetLength() > 0)
			serverList.push_back(orgServerList[i]);

	if (serverList.size() != gamesList->copiedServerList.size())
	{
		// copy the currently selected item
		PingedCompatibleServer selectedServer;
		bool hasSelection = false;
		if (isIndexValid(gamesList->selectedItem))
		{
			selectedServer = gamesList->copiedServerList[gamesList->selectedItem];
			hasSelection = true;
		}

		gamesList->copiedServerList = serverList;
		gamesList->selectItem(-1, false);

		// re-select previous item if it still exists
		if (hasSelection)
		{
			for (unsigned int i = 0; i < gamesList->copiedServerList.size(); i++)
			{
				if (gamesList->copiedServerList[i].address == selectedServer.address)
				{
					gamesList->selectItem(i, false);
					break;
				}
			}
		}
	} else {
		for (int i = (int)gamesList->copiedServerList.size()-1; i >= 0 ; --i) {
			for (int j = 0; j < (int) serverList.size(); ++j)
				if (serverList[j].address == gamesList->copiedServerList[i].address)
					gamesList->copiedServerList[i].name = serverList[j].name;
		}
	}

	bJoin.active = isIndexValid(gamesList->selectedItem);
}

void JoinGameScreen::init()
{
	buttons.push_back(&bJoin);
	buttons.push_back(&bBack);

	/* Absent rather than greyed out where there is nowhere to make one. On
	 * github.io there is no manager, so this is not a disabled button the
	 * player can wonder about -- it is a screen that looks exactly like it did
	 * before any of this existed. */
	_canCreate = minecraft->platform()->canCreateServers();
	if (_canCreate) buttons.push_back(&bCreate);

	minecraft->raknetInstance->clearServerList();
	gamesList = new AvailableGamesList(minecraft, width, height);

#ifdef ANDROID
	tabButtons.push_back(&bJoin);
	tabButtons.push_back(&bBack);
#endif
}

void JoinGameScreen::setupPositions() {
	int yBase = height - 26;

	//#ifdef ANDROID
	bJoin.y =	yBase;
	bBack.y =   yBase;

	bBack.width = bJoin.width = 120;
	//#endif

	// Center buttons
	bJoin.x = width / 2 - 4 - bJoin.width;
	bBack.x = width / 2 + 4;

	/* Above the other two rather than beside them: the row is already two
	 * 120-wide buttons on a 480-wide phone, and a third would either overlap
	 * or push Join somewhere the thumb does not expect it. */
	bCreate.width = 120;
	bCreate.x = width / 2 - bCreate.width / 2;
	bCreate.y = yBase - 24;
}

void JoinGameScreen::render( int xm, int ym, float a )
{
	bool hasNetwork = minecraft->platform()->isNetworkEnabled(true);
#ifdef WIN32
	hasNetwork = hasNetwork && !GetAsyncKeyState(VK_TAB);
#endif

	renderBackground();
	if (hasNetwork) gamesList->render(xm, ym, a);
	Screen::render(xm, ym, a);

	if (hasNetwork) {
#ifdef RPI
		std::string s = "Scanning for Local Network Games...";
#else
		std::string s = "Scanning for WiFi Games...";
#endif
		drawCenteredString(minecraft->font, s, width / 2, 8, 0xffffffff);

		const int textWidth = minecraft->font->width(s);
		const int spinnerX = width/2 + textWidth / 2 + 6;

		static const char* spinnerTexts[] = {"-", "\\", "|", "/"};
		int n = ((int)(5.5f * getTimeS()) % 4);
		drawCenteredString(minecraft->font, spinnerTexts[n], spinnerX, 8, 0xffffffff);

		/* Said here rather than on a screen of its own. Making a world takes a
		 * moment on the far side and the player is already looking at the list
		 * it will appear in, so the honest thing is a line above it rather than
		 * a modal that has to be dismissed before they can see the result. */
		if (_creating == CREATE_SENDING)
			drawCenteredString(minecraft->font, "Creating server...", width / 2, height - 46, 0xffffffff);
		else if (!_createError.empty())
			drawCenteredString(minecraft->font, _createError, width / 2, height - 46, 0xffff5555);
	} else {
		std::string s = "WiFi is disabled";
		const int yy = height / 2 - 8;
		drawCenteredString(minecraft->font, s, width / 2, yy, 0xffffffff);
	}
}

bool JoinGameScreen::isInGameScreen() { return false; }
