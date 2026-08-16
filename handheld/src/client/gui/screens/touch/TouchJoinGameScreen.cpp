#include "TouchJoinGameScreen.h"
#include "../StartMenuScreen.h"
#include "../ProgressScreen.h"
#include "../CreateServerScreen.h"
#include "../ConfigureServerScreen.h"
#include "../AccountScreen.h"
#include "../ServerPasswordScreen.h"
#include "../../../../network/WebRakNetInstance.h"
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
	/* Lit while a finger is on it, and lit again once it is the selection.
	 *
	 * The second half is new and the list had nowhere to put it: 0.6.1's own
	 * selection highlight is commented out in RolledSelectionListV, which makes
	 * sense for the screen it was written for -- a tap joined a game there and
	 * then, so no row was ever selected for longer than a frame. Rows are
	 * picked and then acted on now, and a row that does not say it is picked
	 * leaves you guessing which one Delete is about to take. */
	if ((startSelected == i && Multitouch::getFirstActivePointerIdEx() >= 0) ||
	    selectedItem == i) {
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
	/* The second line is the host's LAN address, and on the web there isn't
	 * one: entries come from the lobby, and their address is a number derived
	 * from the player's id purely so the list has something to key on. Printing
	 * it would be printing a fake IP.
	 *
	 * What goes there instead is what the row cannot say otherwise -- whether
	 * this is somebody's tab or a world that will still be here tomorrow, and
	 * whether it will ask for a password. Half the board is each now, and rows
	 * that all looked alike were rows nobody could choose between. */
	if (s.isDedicated) {
		drawString(minecraft->font, s.name.C_String(), xx1, y + 4 + 2, color);
		std::string detail = s.isLocked ? "Server - password" : "Server";
		drawString(minecraft->font, detail, xx2, y + 18, color2);
	} else {
		drawString(minecraft->font, s.name.C_String(), xx1, y + 8, color);
	}
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
:	bJoin(    2, "Join"),
	bSettings(5, "Settings"),
	bDelete(  6, "Delete"),
	bBack(    3, "Back"),
	bCreate(  4, "New Server"),
	bAccount( 7, "Sign in"),
	bHeader(  0, ""),
	gamesList(NULL),
	_canCreate(false),
	_canManage(false),
	_managedItem(-1),
	_hasAccounts(false)
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
	/* Tapping a row used to join it there and then, which left nowhere to put
	 * anything else you might want to do with a server. A row is a selection
	 * now and the row of buttons below is what acts on it -- the same shape
	 * Select world already uses for local worlds. */
	buttons.push_back(&bJoin);
	buttons.push_back(&bSettings);
	buttons.push_back(&bDelete);
	buttons.push_back(&bBack);

	/* Top right, where Select world already keeps Create new -- making a world
	 * and making a server should not be in two different places on the screen.
	 * Absent rather than greyed out where there is nowhere to make one, so
	 * github.io's Join card looks exactly as it did before any of this. */
	_canCreate = minecraft->platform()->canCreateServers();
	if (_canCreate) buttons.push_back(&bCreate);

	/* The account, beside New Server, because the two belong together: signing
	 * in is what makes a server yours, and this is the only screen in the game
	 * that has anything to do with either. Absent where there is no server list
	 * to sign in to, which is what leaves github.io's Join card as it was.
	 *
	 * It says the name once there is one, so that "am I signed in" is answered
	 * by looking rather than by pressing. */
	_hasAccounts = minecraft->platform()->hasAccounts();
	if (_hasAccounts) {
		_accountName = minecraft->platform()->accountName();
		bAccount.msg = _accountName.empty() ? "Sign in" : _accountName;

		// Sized to its label: a name can be sixteen characters and "Sign in" is
		// seven, and a fixed width would either clip one or waste the other.
		int w = minecraft->font->width(bAccount.msg) + 14;
		if (w < 50) w = 50;
		if (w > 92) w = 92;
		bAccount.width = w;

		buttons.push_back(&bAccount);
	}

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

	// Tucked in beside New Server, or against the right edge where there is no
	// New Server to sit beside.
	if (_hasAccounts) {
		bAccount.x = (_canCreate ? bCreate.x : width) - bAccount.width;
		bAccount.y = 0;
	}

	/* Three across the bottom. Even thirds rather than fitted widths, because
	 * two of them come and go with what is selected and buttons that moved
	 * sideways as you picked different rows would be worse than a gap. */
	const int third = width / 3;
	bJoin.width = bSettings.width = third;
	bDelete.width = width - third * 2;
	bJoin.height = bSettings.height = bDelete.height = 28;
	bJoin.x     = 0;
	bSettings.x = third;
	bDelete.x   = third * 2;
	bJoin.y = bSettings.y = bDelete.y = height - 28;
	bHeader.x = bBack.width;
	// The header fills whatever the others leave, which is not the same width on
	// both deployments -- there is no New Server button and nothing to sign in
	// to where there is nowhere to make a world.
	bHeader.width = width - bHeader.x
	              - (_canCreate ? bCreate.width : 0)
	              - (_hasAccounts ? bAccount.width : 0);
}

void JoinGameScreen::buttonClicked(Button* button)
{
	if (button->id == bJoin.id)
	{
		if (isIndexValid(gamesList->selectedItem))
		{
			PingedCompatibleServer selectedServer = gamesList->copiedServerList[gamesList->selectedItem];
#if defined(MC_WASM)
			/* No joining a *dedicated* world without a name of your own
			 * (2026-08-16).
			 *
			 * Signed out, every player is the same "Steve", and a dedicated
			 * world keys everything by name -- most recently the per-player
			 * save file, so strangers were sharing one position and one
			 * inventory. The board stays browsable signed out; the way *in* is
			 * what asks.
			 *
			 * Only dedicated, because the first cut asked for all of them and
			 * that was wrong: somebody else's tab is not this service's world.
			 * Its saves live in the host's browser, nothing here keys anything
			 * on the joiner's name, and an account buys that join exactly
			 * nothing -- while costing two friends on phones a registration
			 * before they can play together, which is the whole use this build
			 * exists for. */
			if (selectedServer.isDedicated && _hasAccounts && _accountName.empty())
			{
				minecraft->setScreen(new AccountScreen());
				return;
			}

			/* Locked servers get asked first.
			 *
			 * The lock lives in the relay's switch, which drops datagrams for a
			 * server this socket has not opened -- so joining one without the
			 * password is not a refusal, it is a silence, and RakNet abandons
			 * the attempt after about six seconds of it. The page used to spot
			 * that first dropped datagram and put up a prompt, which meant
			 * finding and typing a password against a countdown that had
			 * already started. Asked before joinMultiplayer(), nothing is
			 * counting and the connection that starts is one that can finish. */
			if (selectedServer.isLocked)
			{
				minecraft->setScreen(new ServerPasswordScreen(selectedServer,
				                                              mcpeRouteOf(selectedServer.address)));
				return;
			}
#endif
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
	if ((button->id == bSettings.id || button->id == bDelete.id) &&
	    isIndexValid(gamesList->selectedItem) && _canManage)
	{
		const PingedCompatibleServer& s = gamesList->copiedServerList[gamesList->selectedItem];
		const unsigned int route = mcpeRouteOf(s.address);
		if (button->id == bSettings.id)
			minecraft->setScreen(new ConfigureServerScreen(route, s.name.C_String(), s.isLocked));
		else
			minecraft->setScreen(new DeleteServerScreen(route, s.name.C_String()));
		return;
	}
	if (_hasAccounts && button->id == bAccount.id)
	{
		minecraft->setScreen(new AccountScreen());
		return;
	}
	if (button->id == bCreate.id)
	{
		/* Sign in first, where there is anything to sign in to.
		 *
		 * Not a rule the manager enforces -- it still makes worlds for anyone
		 * who asks, which is what stops every world made before today from
		 * becoming unowned. It is a rule about what is worth offering: a world
		 * made signed out belongs to a browser, so it is not yours on your
		 * phone and it stops being yours when you clear site data, and finding
		 * that out a week later is worse than a screen in the way now. */
		if (_hasAccounts && _accountName.empty())
		{
			minecraft->setScreen(new AccountScreen());
			return;
		}

		// A screen of the game's own rather than a panel in the page. It owns
		// the whole exchange -- asking, sending, and saying what went wrong --
		// and comes back here when there is something new to list.
		minecraft->setScreen(new CreateServerScreen());
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
	bCreate.active = _canCreate;

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

	const int selected = gamesList->selectedItem;
	const bool haveSelection = isIndexValid(selected);
	bJoin.active = haveSelection;

	/* Whether the selection is a world this browser made. Asked when the
	 * selection moves rather than every frame -- it is a call out to the page,
	 * and the answer cannot change while the same row stays picked. */
	if (selected != _managedItem) {
		_managedItem = selected;
		_canManage = false;
		if (haveSelection) {
			const PingedCompatibleServer& s = gamesList->copiedServerList[selected];
			if (s.isDedicated)
				_canManage = minecraft->platform()->canManageServer(mcpeRouteOf(s.address));
		}
	}

	// Greyed rather than gone: a fixed row of three that never moves is easier
	// to hit than two buttons that slide sideways as the selection changes.
	bSettings.active = bDelete.active = haveSelection && _canManage;
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
		// The header is whatever the buttons leave, and an account button with a
		// long name in it can leave less than this sentence needs. Shortened
		// rather than allowed to run under the buttons on either side.
		if (minecraft->font->width(s) + 16 > bHeader.width)
			s = "Looking...";
		drawCenteredString(minecraft->font, s, baseX, 8, 0xffffffff);

		const int textWidth = minecraft->font->width(s);
		const int spinnerX = baseX + textWidth / 2 + 6;

		static const char* spinnerTexts[] = {"-", "\\", "|", "/"};
		int n = ((int)(5.5f * getTimeS()) % 4);
		drawCenteredString(minecraft->font, spinnerTexts[n], spinnerX, 8, 0xffffffff);
	} else {
		drawCenteredString(minecraft->font, "WiFi is disabled", baseX, 8, 0xffffffff);
	}
}

bool JoinGameScreen::isInGameScreen() { return false; }

};
