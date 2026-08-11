#ifndef MAIN_SDL_H__
#define MAIN_SDL_H__

/** SDL2 entry point, shared by desktop macOS and Emscripten.

    Replaces the Win32 HWND + EGL setup in main_win32.h. Neither target has EGL,
    so App.h already turns swapBuffers() into a no-op here (NO_EGL) and we
    present the frame ourselves after each App::update().

    The two targets differ in only two structural ways, both handled below:
      - the browser owns the event loop, so the frame body is a callback driven
        by emscripten_set_main_loop instead of a while() loop here;
      - saves live in IndexedDB rather than on a real filesystem, so they need
        an explicit syncfs in both directions. */

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

#if defined(__EMSCRIPTEN__)
	#include <emscripten.h>
#endif

#include "client/renderer/gles.h"
#include "AppPlatform_sdl.h"
#include "platform/input/Mouse.h"
#include "platform/input/Keyboard.h"
#include "platform/input/Multitouch.h"

static SDL_Window* g_window = 0;
static bool g_running = true;

/** SDL keysym -> the Windows-virtual-key-ish codes this codebase's Keyboard
    and Options key bindings are written against (see platform/input/Keyboard.h). */
static unsigned char transformKey(SDL_Keycode key)
{
	if (key >= SDLK_a && key <= SDLK_z) return (unsigned char)(key - SDLK_a + 65);
	if (key >= SDLK_0 && key <= SDLK_9) return (unsigned char)(key - SDLK_0 + 48);
	if (key >= SDLK_F1 && key <= SDLK_F12) return (unsigned char)(key - SDLK_F1 + 112);

	switch (key) {
	case SDLK_SPACE:     return 32;
	case SDLK_RETURN:    return 13;
	case SDLK_KP_ENTER:  return 13;
	case SDLK_BACKSPACE: return 8;
	case SDLK_ESCAPE:    return 27;
	case SDLK_TAB:       return 9;
	case SDLK_LSHIFT:    return 10;  // Keyboard::KEY_LSHIFT
	case SDLK_RSHIFT:    return 10;
	case SDLK_LEFT:      return 37;
	case SDLK_UP:        return 38;
	case SDLK_RIGHT:     return 39;
	case SDLK_DOWN:      return 40;
	default:             return 0;
	}
}

static void makeDirs(const std::string& path)
{
	for (size_t i = 1; i < path.size(); ++i) {
		if (path[i] == '/')
			mkdir(path.substr(0, i).c_str(), 0755);
	}
	mkdir(path.c_str(), 0755);
}

/** Everything the frame callback needs. On desktop this could all have stayed
    local to main(); the browser main loop is a callback, so it has to outlive
    the function that sets it up. */
struct SdlAppState
{
	MAIN_CLASS* app;
	AppContext appContext;

	int windowWidth;
	int windowHeight;
	int drawableWidth;
	int drawableHeight;

	// On a Retina display the drawable is larger than the window in points;
	// the game works purely in drawable pixels, so mouse coords get scaled.
	float mouseScaleX;
	float mouseScaleY;
};

static SdlAppState g_state;

#if defined(__EMSCRIPTEN__)
/** Set once IndexedDB has been read into the IDBFS mount. Nothing may touch the
    save directory before this, or the world list reads back empty. */
static bool g_storageReady = false;

extern "C" EMSCRIPTEN_KEEPALIVE void mcpe_storage_ready()
{
	g_storageReady = true;
}
#endif

static void handleEvent(const SDL_Event& event)
{
	switch (event.type) {
	case SDL_QUIT:
		g_running = false;
		break;

	case SDL_WINDOWEVENT:
		if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
			g_state.windowWidth = event.window.data1;
			g_state.windowHeight = event.window.data2;
			SDL_GL_GetDrawableSize(g_window, &g_state.drawableWidth, &g_state.drawableHeight);
			g_state.mouseScaleX = (float)g_state.drawableWidth / (float)g_state.windowWidth;
			g_state.mouseScaleY = (float)g_state.drawableHeight / (float)g_state.windowHeight;
			((AppPlatform_sdl*)g_state.appContext.platform)->setScreenSize(g_state.drawableWidth, g_state.drawableHeight);
			g_state.app->setSize(g_state.drawableWidth, g_state.drawableHeight);
		}
		break;

	case SDL_KEYDOWN: {
		const unsigned char k = transformKey(event.key.keysym.sym);
		if (k) Keyboard::feed(k, 1);
		break;
	}
	case SDL_KEYUP: {
		const unsigned char k = transformKey(event.key.keysym.sym);
		if (k) Keyboard::feed(k, 0);
		break;
	}
	case SDL_TEXTINPUT: {
		for (const char* c = event.text.text; *c; ++c) {
			if ((unsigned char)*c >= 32)
				Keyboard::feedText(*c);
		}
		break;
	}

	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP: {
		const char down = (event.type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
		char button = 0;
		if (event.button.button == SDL_BUTTON_LEFT)  button = MouseAction::ACTION_LEFT;
		else if (event.button.button == SDL_BUTTON_RIGHT) button = MouseAction::ACTION_RIGHT;
		if (button) {
			const short x = (short)(event.button.x * g_state.mouseScaleX);
			const short y = (short)(event.button.y * g_state.mouseScaleY);
			Mouse::feed(button, down, x, y);
			Multitouch::feed(button, down, x, y, 0);
		}
		break;
	}

	case SDL_MOUSEMOTION: {
		const short x = (short)(event.motion.x * g_state.mouseScaleX);
		const short y = (short)(event.motion.y * g_state.mouseScaleY);
		Multitouch::feed(0, 0, x, y, 0);
		Mouse::feed(MouseAction::ACTION_MOVE, 0, x, y,
					(short)event.motion.xrel, (short)event.motion.yrel);
		break;
	}

	case SDL_MOUSEWHEEL: {
		int mx = 0, my = 0;
		SDL_GetMouseState(&mx, &my);
		Mouse::feed(MouseAction::ACTION_WHEEL, 0,
					(short)(mx * g_state.mouseScaleX), (short)(my * g_state.mouseScaleY),
					0, (short)event.wheel.y);
		break;
	}

	default:
		break;
	}
}

static void runFrame()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
		handleEvent(event);

	g_state.app->update();
	SDL_GL_SwapWindow(g_window);

#if defined(__EMSCRIPTEN__)
	// beforeunload is best-effort at the best of times and is skipped outright
	// when a mobile browser discards a backgrounded tab, so don't rely on it as
	// the only flush. IDBFS diffs by mtime, so a periodic sync of an unchanged
	// tree is cheap. ~30s at 60fps.
	static int framesSinceSync = 0;
	if (++framesSinceSync >= 1800) {
		framesSinceSync = 0;
		EM_ASM(FS.syncfs(false, function () {}););
	}
#endif
}

/** Constructs the game once the storage it will read from is actually there. */
static void startApp(const std::string& dataDir, const std::string& storagePath)
{
	printf("Assets:  %s\nSaves:   %s\n", dataDir.c_str(), storagePath.c_str());

	g_state.appContext.doRender = true;
	g_state.appContext.platform = new AppPlatform_sdl(dataDir, g_state.drawableWidth, g_state.drawableHeight);

	g_state.app = new MAIN_CLASS();
	g_state.app->externalStoragePath = storagePath;
	g_state.app->externalCacheStoragePath = storagePath;

	// NinecraftApp::init() hides App::init(AppContext&), so go through the base.
	((App*)g_state.app)->init(g_state.appContext);
	g_state.app->setSize(g_state.drawableWidth, g_state.drawableHeight);

	SDL_StartTextInput();
}

#if defined(__EMSCRIPTEN__)
/** Runs instead of runFrame() until IndexedDB has finished loading, then swaps
    itself out. Spinning a frame callback is the only way to wait here: syncfs
    is asynchronous and the browser will not let us block for it. */
static void bootFrame()
{
	if (!g_storageReady)
		return;

	startApp(MC_DATA_DIR, "/saves");

	emscripten_cancel_main_loop();
	emscripten_set_main_loop(runFrame, 0, 0);
}
#endif

int main(int argc, char** argv)
{
	// LOGI/LOGW/LOGE are all printf here, and stdout is block-buffered when
	// redirected to a file, which hides everything up to a crash.
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("Couldn't initialize SDL: %s\n", SDL_GetError());
		return -1;
	}

	// No core-profile request: on macOS that yields the legacy 2.1 compatibility
	// context, which is what this fixed-function renderer needs. Under Emscripten
	// the context is WebGL either way and LEGACY_GL_EMULATION does the emulating.
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	g_state.windowWidth = 854;
	g_state.windowHeight = 480;

	g_window = SDL_CreateWindow(
		"Minecraft Pocket Edition v0.6.1 alpha",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		g_state.windowWidth, g_state.windowHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

	if (!g_window) {
		printf("Couldn't create window: %s\n", SDL_GetError());
		SDL_Quit();
		return -2;
	}

	SDL_GLContext glContext = SDL_GL_CreateContext(g_window);
	if (!glContext) {
		printf("Couldn't create GL context: %s\n", SDL_GetError());
		SDL_DestroyWindow(g_window);
		SDL_Quit();
		return -3;
	}
	SDL_GL_MakeCurrent(g_window, glContext);
	SDL_GL_SetSwapInterval(1);

	printf("GL_VERSION:  %s\n", (const char*)glGetString(GL_VERSION));
	printf("GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));

	glInit();

	g_state.drawableWidth = g_state.windowWidth;
	g_state.drawableHeight = g_state.windowHeight;
	SDL_GL_GetDrawableSize(g_window, &g_state.drawableWidth, &g_state.drawableHeight);
	g_state.mouseScaleX = (float)g_state.drawableWidth / (float)g_state.windowWidth;
	g_state.mouseScaleY = (float)g_state.drawableHeight / (float)g_state.windowHeight;

	const std::string dataDir = MC_DATA_DIR;

#if defined(__EMSCRIPTEN__)
	// Assets are baked into the preload package at MC_DATA_DIR (MEMFS). Saves
	// have to outlive the tab, so they get their own IDBFS mount -- but an IDBFS
	// mount starts empty and is only filled by syncfs(true), which is async.
	// So: mount, start the load, and let bootFrame() hold off app startup until
	// mcpe_storage_ready() fires.
	mkdir("/saves", 0755);
	EM_ASM(
		FS.mount(IDBFS, {}, '/saves');
		FS.syncfs(true, function (err) {
			if (err) console.error('mcpe: could not load saves from IndexedDB:', err);
			// Start anyway on failure -- a fresh world beats a hang.
			_mcpe_storage_ready();
		});

		// The game writes saves through plain stdio, which only reaches the
		// in-memory image of the mount. Nothing here has a shutdown path
		// (main() returns straight to the browser), so the flush hangs off the
		// page lifecycle instead.
		window.addEventListener('beforeunload', function () {
			FS.syncfs(false, function () {});
		});
		// Fires reliably where beforeunload doesn't, notably on mobile.
		document.addEventListener('visibilitychange', function () {
			if (document.visibilityState === 'hidden')
				FS.syncfs(false, function () {});
		});
	);

	// 0 fps means "drive from requestAnimationFrame", which matches the display.
	// The trailing 0 means don't simulate an infinite loop: main() returns and
	// the runtime is kept alive by the callback.
	emscripten_set_main_loop(bootFrame, 0, 0);
	return 0;
#else
	std::string storagePath = getenv("HOME") ? getenv("HOME") : ".";
	storagePath += "/Library/Application Support/mcpe-0.6.1";
	makeDirs(storagePath);

	startApp(dataDir, storagePath);

	while (g_running && !g_state.app->wantToQuit())
		runFrame();

	delete g_state.app;
	g_state.appContext.platform->finish();
	delete g_state.appContext.platform;

	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(g_window);
	SDL_Quit();

	return 0;
#endif
}

#endif /*MAIN_SDL_H__*/
