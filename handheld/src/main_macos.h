#ifndef MAIN_MACOS_H__
#define MAIN_MACOS_H__

/** SDL2 entry point for desktop macOS.

    Replaces the Win32 HWND + EGL setup in main_win32.h. macOS has no EGL, so
    App.h already turns swapBuffers() into a no-op here (NO_EGL) and we present
    the frame ourselves after each App::update(). */

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

#include "client/renderer/gles.h"
#include "AppPlatform_macos.h"
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
	// context, which is what this fixed-function renderer needs.
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	int windowWidth = 854;
	int windowHeight = 480;

	g_window = SDL_CreateWindow(
		"Minecraft Pocket Edition v0.6.1 alpha",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		windowWidth, windowHeight,
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

	// On a Retina display the drawable is larger than the window in points;
	// the game works purely in drawable pixels, so mouse coords get scaled.
	int drawableWidth = windowWidth;
	int drawableHeight = windowHeight;
	SDL_GL_GetDrawableSize(g_window, &drawableWidth, &drawableHeight);
	float mouseScaleX = (float)drawableWidth / (float)windowWidth;
	float mouseScaleY = (float)drawableHeight / (float)windowHeight;

	const std::string dataDir = MC_DATA_DIR;

	std::string storagePath = getenv("HOME") ? getenv("HOME") : ".";
	storagePath += "/Library/Application Support/mcpe-0.6.1";
	makeDirs(storagePath);
	printf("Assets:  %s\nSaves:   %s\n", dataDir.c_str(), storagePath.c_str());

	AppContext appContext;
	appContext.doRender = true;
	appContext.platform = new AppPlatform_macos(dataDir, drawableWidth, drawableHeight);

	MAIN_CLASS* app = new MAIN_CLASS();
	app->externalStoragePath = storagePath;
	app->externalCacheStoragePath = storagePath;

	// NinecraftApp::init() hides App::init(AppContext&), so go through the base.
	((App*)app)->init(appContext);
	app->setSize(drawableWidth, drawableHeight);

	SDL_StartTextInput();

	while (g_running && !app->wantToQuit())
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type) {
			case SDL_QUIT:
				g_running = false;
				break;

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					windowWidth = event.window.data1;
					windowHeight = event.window.data2;
					SDL_GL_GetDrawableSize(g_window, &drawableWidth, &drawableHeight);
					mouseScaleX = (float)drawableWidth / (float)windowWidth;
					mouseScaleY = (float)drawableHeight / (float)windowHeight;
					((AppPlatform_macos*)appContext.platform)->setScreenSize(drawableWidth, drawableHeight);
					app->setSize(drawableWidth, drawableHeight);
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
					const short x = (short)(event.button.x * mouseScaleX);
					const short y = (short)(event.button.y * mouseScaleY);
					Mouse::feed(button, down, x, y);
					Multitouch::feed(button, down, x, y, 0);
				}
				break;
			}

			case SDL_MOUSEMOTION: {
				const short x = (short)(event.motion.x * mouseScaleX);
				const short y = (short)(event.motion.y * mouseScaleY);
				Multitouch::feed(0, 0, x, y, 0);
				Mouse::feed(MouseAction::ACTION_MOVE, 0, x, y,
							(short)event.motion.xrel, (short)event.motion.yrel);
				break;
			}

			case SDL_MOUSEWHEEL: {
				int mx = 0, my = 0;
				SDL_GetMouseState(&mx, &my);
				Mouse::feed(MouseAction::ACTION_WHEEL, 0,
							(short)(mx * mouseScaleX), (short)(my * mouseScaleY),
							0, (short)event.wheel.y);
				break;
			}

			default:
				break;
			}
		}

		app->update();
		SDL_GL_SwapWindow(g_window);
	}

	delete app;
	appContext.platform->finish();
	delete appContext.platform;

	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(g_window);
	SDL_Quit();

	return 0;
}

#endif /*MAIN_MACOS_H__*/
