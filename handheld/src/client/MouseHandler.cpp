#include "MouseHandler.h"
#include "player/input/ITurnInput.h"

#ifdef RPI
#include <SDL/SDL.h>
#endif

#ifdef MC_SDL2
#include <SDL2/SDL.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

MouseHandler::MouseHandler( ITurnInput* turnInput )
:	_turnInput(turnInput)
{}

MouseHandler::MouseHandler()
:	_turnInput(0)
{}

MouseHandler::~MouseHandler() {
}

void MouseHandler::setTurnInput( ITurnInput* turnInput ) {
	_turnInput = turnInput;
}

void MouseHandler::grab() {
	xd = 0;
	yd = 0;

#if defined(RPI)
	//LOGI("Grabbing input!\n");
	SDL_WM_GrabInput(SDL_GRAB_ON);
	SDL_ShowCursor(0);
#elif defined(MC_SDL2)
	// Relative mode hides the cursor and gives us unbounded xrel/yrel deltas,
	// which is what MouseTurnInput::MODE_DELTA consumes.
	SDL_SetRelativeMouseMode(SDL_TRUE);

	#ifdef __EMSCRIPTEN__
		// A browser only grants pointer lock from inside a user gesture, and
		// almost nothing here asks for it from one: the usual caller is
		// setScreen(NULL) closing the menu, which runs in the frame callback.
		// The request above therefore gets refused, while Minecraft::grabMouse
		// has already set mouseGrabbed -- so the game believes it holds a cursor
		// the browser never handed over, and never asks again.
		//
		// emscripten_request_pointerlock's deferUntilInEventHandler doesn't help
		// either: it replays the request from Emscripten's own event handlers,
		// and the SDL2 port installs its own DOM listeners instead, so the
		// deferred request never gets its turn.
		//
		// So just record the intent. The page watches for a real mousedown on
		// the canvas and asks from inside that listener, which is a gesture the
		// browser accepts. SDL keeps its relative-mode state from the call
		// above, so the deltas are right once the lock lands.
		EM_ASM(window.mcpeWantPointerLock = true;);
	#endif
#endif
}

void MouseHandler::release() {
#if defined(RPI)
	//LOGI("Releasing input!\n");
	SDL_WM_GrabInput(SDL_GRAB_OFF);
	SDL_ShowCursor(1);
#elif defined(MC_SDL2)
	SDL_SetRelativeMouseMode(SDL_FALSE);

	#ifdef __EMSCRIPTEN__
		// Clear the intent first, so the mousedown listener can't re-capture the
		// cursor the player just asked to get back.
		EM_ASM(window.mcpeWantPointerLock = false;);
		emscripten_exit_pointerlock();
	#endif
#endif
}

void MouseHandler::poll() {
	if (_turnInput != 0) {
		TurnDelta td = _turnInput->getTurnDelta();
		xd = td.x;
		yd = td.y;
	}
}
