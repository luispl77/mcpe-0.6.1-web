#include "ScreenChooser.h"
#include "StartMenuScreen.h"
#include "SelectWorldScreen.h"
#include "JoinGameScreen.h"
#include "PauseScreen.h"
#include "RenameMPLevelScreen.h"
#include "IngameBlockSelectionScreen.h"
#include "touch/TouchStartMenuScreen.h"
#include "touch/TouchSelectWorldScreen.h"
#include "touch/TouchJoinGameScreen.h"
#include "touch/TouchIngameBlockSelectionScreen.h"

#include "../../Minecraft.h"

Screen* ScreenChooser::createScreen( ScreenId id )
{
	Screen* screen = NULL;

	// The menus outside the world -- title, world list, join -- are the game's
	// own artwork either way, and the touch versions are the ones people
	// remember: three cards side by side rather than a column of buttons. They
	// are driven by ordinary Screen mouse events, so a mouse works them fine.
	// The in-world screens still follow the controls, since those do differ:
	// the keyboard variant of the block picker is the one that closes on Escape.
	bool touchMenus = _mc->useTouchscreen();
#if defined(MC_WASM)
	switch (id) {
	case SCREEN_STARTMENU:
	case SCREEN_SELECTWORLD:
	case SCREEN_JOINGAME:
		touchMenus = true;
		break;
	default:
		break;
	}
#endif

	if (touchMenus) {
		switch (id) {
		case SCREEN_STARTMENU:	screen = new Touch::StartMenuScreen();	break;
		case SCREEN_SELECTWORLD:screen = new Touch::SelectWorldScreen();break;
		case SCREEN_JOINGAME:	screen = new Touch::JoinGameScreen();	break;
		case SCREEN_PAUSE:	    screen = new PauseScreen(false); break;
		case SCREEN_PAUSEPREV:	screen = new PauseScreen(true);	 break;
		case SCREEN_BLOCKSELECTION:	screen = new Touch::IngameBlockSelectionScreen();	break;

		case SCREEN_NONE:
		default:
			// Do nothing
			break;
		}
	} else {
		switch (id) {
		case SCREEN_STARTMENU:	screen = new StartMenuScreen();  break;
		case SCREEN_SELECTWORLD:screen = new SelectWorldScreen();break;
		case SCREEN_JOINGAME:	screen = new JoinGameScreen();   break;
		case SCREEN_PAUSE:	    screen = new PauseScreen(false); break;
		case SCREEN_PAUSEPREV:	screen = new PauseScreen(true);	 break;
		case SCREEN_BLOCKSELECTION:	screen = new IngameBlockSelectionScreen();	break;

		case SCREEN_NONE:
		default:
			// Do nothing
			break;
		}
	}
	return screen;
}

Screen* ScreenChooser::setScreen(ScreenId id)
{
	Screen* screen = createScreen(id);
	_mc->setScreen(screen);
	return screen;
}
