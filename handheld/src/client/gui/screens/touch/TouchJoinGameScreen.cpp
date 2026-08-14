#include "TouchJoinGameScreen.h"
#include "../StartMenuScreen.h"
#include "../ProgressScreen.h"
#include "../DialogDefinitions.h"
#include "../../Font.h"
#include "../../../Minecraft.h"
#include "../../../renderer/Textures.h"

namespace Touch {

//
// Games list
//

void AvailableGamesList::selectStart( int item) {
	startSelected = item;
}

void AvailableGamesList::selectCancel() {
	startSelected = -1;
}

void AvailableGamesList::selectItem( int item, bool doubleClick ) {
	LOGI("selected an item! %d\n", item);
	selectedItem = item;
}

void AvailableGamesList::renderItem( int i, int x, int y, int h, Tesselator& t )
{
	if (startSelected == i && Multitouch::getFirstActivePointerIdEx() >= 0) {
		fill((int)x0, y, (int)x1, y+h, 0x809E684F);
	}

	//static int colors[2] = {0xffffb0, 0xcccc90};
	const PingedCompatibleServer& s = copiedServerList[i];
	unsigned int color  = s.isSpecial? 0x6090a0 : 0xffffb0;
	unsigned int color2 = 0xffffa0;//colors[i&1];

	int xx1 = (int)x0 + 24;
	int xx2 = xx1;

	if (s.isSpecial) {
		xx1 += 50;

		glEnable2(GL_TEXTURE_2D);
        glColor4f2(1,1,1,1);
        glEnable2(GL_BLEND);
		minecraft->textures->loadAndBindTexture("gui/badge/minecon140.png");
		blit(xx2, y + 6, 0, 0, 37, 8, 140, 240);
	}

#if defined(MC_WASM)
	// The second line is the host's LAN address, and on the web there isn't
	// one: entries come from the lobby, and their address is a number derived
	// from the player's id purely so the list has something to key on. Printing
	// it would be printing a fake IP. The name line carries the world instead.
	(void)color2;
	drawString(minecraft->font, s.name.C_String(), xx1, y + 8, color);
#else
	drawString(minecraft->font, s.name.C_String(), xx1, y + 4 + 2, color);
	drawString(minecraft->font, s.address.ToString(false), xx2, y + 18, color2);
#endif

	/*
	drawString(minecraft->font, copiedServerList[i].name.C_String(), (int)x0 + 24, y + 4, color);
	drawString(minecraft->font, copiedServerList[i].address.ToString(false), (int)x0 + 24, y + 18, color);
	*/
}


//
// Join Game screen
//
JoinGameScreen::JoinGameScreen()
:	bJoin(  2, "Join Game"),
	bBack(  3, "Back"),
	bCreate(4, "New Server"),
	bHeader(0, ""),
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

void JoinGameScreen::init()
{
	//buttons.push_back(&bJoin);
	buttons.push_back(&bBack);

	/* Top right, where Select world already keeps Create new -- making a world
	 * and making a server should not be in two different places on the screen.
	 * Absent rather than greyed out where there is nowhere to make one, so
	 * github.io's Join card looks exactly as it did before any of this. */
	_canCreate = minecraft->platform()->canCreateServers();
	if (_canCreate) buttons.push_back(&bCreate);

	buttons.push_back(&bHeader);

	minecraft->raknetInstance->clearServerList();
	gamesList = new AvailableGamesList(minecraft, width, height);

#ifdef ANDROID
	//tabButtons.push_back(&bJoin);
	tabButtons.push_back(&bBack);
#endif
}

void JoinGameScreen::setupPositions() {
	//int yBase = height - 26;

	//#ifdef ANDROID
	bJoin.y =	0;
	bBack.y =   0;
	bCreate.y = 0;
	bHeader.y = 0;
	//#endif

	// Center buttons
	//bJoin.x = width / 2 - 4 - bJoin.w;
	bBack.x = 0;//width / 2 + 4;
	bCreate.x = width - bCreate.width;
	bHeader.x = bBack.width;
	// The header fills whatever the two buttons leave, which is not the same
	// width on both deployments -- there is no New Server button where there is
	// nowhere to make one.
	bHeader.width = width - bHeader.x - (_canCreate ? bCreate.width : 0);
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
		/* Collected by the platform rather than by a text field of our own:
		 * 0.6.1's GUI has no password field, the platform already owns every
		 * other piece of text entry in the game, and on the web that dialog is
		 * an overlay in the page -- so the password can be a real password
		 * input instead of a second popup. */
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
	/* Two waits, one after the other, and neither blocks a frame: the player
	 * answering the dialog, then the manager answering the request. Both report
	 * NOTINITED (-2) until they have something to say, so sitting here doing
	 * nothing is the ordinary state rather than a stall.
	 *
	 * Before the join check below, because a tap that lands on the list while a
	 * dialog is up should not also start joining something. */
	if (_creating == CREATE_ASKING) {
		// 1 is OK and 0 is Cancel, the bare values every other screen here tests.
		const int status = minecraft->platform()->getUserInputStatus();
		if (status > -1) {
			if (status == 1) {
				const std::vector<std::string> answer = minecraft->platform()->getUserInput();
				const std::string name     = answer.size() > 0 ? answer[0] : std::string();
				const std::string mode     = answer.size() > 1 ? answer[1] : std::string();
				const std::string seed     = answer.size() > 2 ? answer[2] : std::string();
				const std::string password = answer.size() > 3 ? answer[3] : std::string();
				minecraft->platform()->createServer(name, mode, seed, password);
				_creating = CREATE_SENDING;
			} else {
				_creating = CREATE_IDLE;
			}
		}
		return;
	}
	if (_creating == CREATE_SENDING) {
		const int status = minecraft->platform()->createServerStatus();
		if (status > -1) {
			// Nothing to do on success: the world announces itself to the board
			// and arrives in this list the way everybody else's does.
			if (status != 1)
				_createError = "Could not create the server";
			_creating = CREATE_IDLE;
		}
		return;
	}

	bCreate.active = _canCreate;

	if (isIndexValid(gamesList->selectedItem)) {
		buttonClicked(&bJoin);
		return;
	}

	//gamesList->tick();

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

void JoinGameScreen::render( int xm, int ym, float a )
{
	bool hasNetwork = minecraft->platform()->isNetworkEnabled(true);
#ifdef WIN32
	hasNetwork = hasNetwork && !GetAsyncKeyState(VK_TAB);
#endif

	renderBackground();
	if (hasNetwork) gamesList->render(xm, ym, a);
	else gamesList->renderDirtBackground();
	Screen::render(xm, ym, a);

	const int baseX = bHeader.x + bHeader.width / 2;

	if (hasNetwork) {
#if defined(MC_WASM)
		// Nothing is being scanned for: there is no LAN to broadcast on, and the
		// list is polled from the lobby service. The spinner still earns its
		// place -- it is the only sign the poll is alive when nobody is on.
		std::string s = "Looking for players...";
#else
		std::string s = "Scanning for WiFi Games...";
#endif
		drawCenteredString(minecraft->font, s, baseX, 8, 0xffffffff);

		const int textWidth = minecraft->font->width(s);
		const int spinnerX = baseX + textWidth / 2 + 6;

		static const char* spinnerTexts[] = {"-", "\\", "|", "/"};
		int n = ((int)(5.5f * getTimeS()) % 4);
		drawCenteredString(minecraft->font, spinnerTexts[n], spinnerX, 8, 0xffffffff);

		/* Under the title bar rather than on a screen of its own. Making a world
		 * takes a moment on the far side and the player is already looking at
		 * the list it will appear in, so a line here beats a modal they would
		 * have to dismiss before they could see the result. */
		if (_creating == CREATE_SENDING)
			drawCenteredString(minecraft->font, "Creating server...", width / 2, 30, 0xffffffff);
		else if (!_createError.empty())
			drawCenteredString(minecraft->font, _createError, width / 2, 30, 0xffff5555);
	} else {
		drawCenteredString(minecraft->font, "WiFi is disabled", baseX, 8, 0xffffffff);
	}
}

bool JoinGameScreen::isInGameScreen() { return false; }

};
