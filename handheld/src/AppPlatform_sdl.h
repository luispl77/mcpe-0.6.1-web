#ifndef APPPLATFORM_SDL_H__
#define APPPLATFORM_SDL_H__

#include "AppPlatform.h"
#include "platform/log.h"
#include "client/renderer/gles.h"
#include <string>

/** Platform layer shared by the SDL2 targets: desktop macOS and Emscripten.

    Modelled on AppPlatform_win32, but with the asset root supplied at runtime
    instead of being hard coded to "../../data/", and with the png loader
    normalising whatever colour type the file happens to use into RGBA8888.

    Everything here is portable C++ (libpng, stdio). The one part that isn't is
    the text-input dialog, which each target supplies in its own file --
    AppPlatform_sdl_dialog_cocoa.mm on macOS, AppPlatform_sdl_dialog_web.cpp on
    the web -- so exactly one of those is compiled into any given build. */
class AppPlatform_sdl: public AppPlatform
{
public:
	static const int USERINPUT_CANCEL    =  0;
	static const int USERINPUT_OK        =  1;
	static const int USERINPUT_NOTINITED = -2;

	AppPlatform_sdl(const std::string& dataDir, int width, int height, bool touchscreen)
	:	_dataDir(dataDir),
		_width(width),
		_height(height),
		_touchscreen(touchscreen),
		_userInputStatus(USERINPUT_NOTINITED),
		_dialogFields(0)
	{}

	/** The game asks the platform to collect text (world name, chat line, ...)
	    through showDialog() + getUserInputStatus() + getUserInput(). On Android
	    and iOS that's a native dialog; on the web it is an overlay in the page.

	    The answer arrives whenever the player gives it, which can be many frames
	    after showDialog() returns. That needs nothing special from the callers:
	    they poll getUserInputStatus() every tick and treat USERINPUT_NOTINITED
	    as "keep waiting", so a dialog that takes its time is only a longer wait.
	    See AppPlatform_sdl_dialog_web.cpp. */
	void showDialog(int dialogId);
	int getUserInputStatus();
	StringVector getUserInput();

	/** Raising and dropping the soft keyboard, which on this target means
	    focusing and blurring an off-screen input in the page.

	    Only a phone needs it: a desktop browser sends SDL_TEXTINPUT for anything
	    typed at the canvas, so showKeyboard() there is a call that costs nothing
	    and changes nothing. See AppPlatform_sdl_dialog_web.cpp. */
	void showKeyboard();
	void hideKeyboard();

	/// Dedicated worlds, when the page has been given a manager to talk to.
	bool canCreateServers();
	void createServer(const std::string& name, const std::string& mode,
	                  const std::string& seed, const std::string& password);
	int  createServerStatus();

	bool canManageServer(unsigned int route);
	void deleteServer(unsigned int route);
	void configureServer(unsigned int route, const std::string& name,
	                     const std::string& password, bool setPassword);

	void unlockServer(unsigned int route, const std::string& password);
	bool hasServerPassword(unsigned int route);

	BinaryBlob readAssetFile(const std::string& filename);
	TextureData loadTexture(const std::string& filename_, bool textureFolder);
	void saveScreenshot(const std::string& filename, int glWidth, int glHeight);

	std::string getDateString(int s);
	std::string getPlatformStringVar(int stringId);

	/// No licensing on this target: 0 means "licensed".
	int checkLicense() { return 0; }
	bool hasBuyButtonWhenInvalidLicense() { return false; }

	/// Pure virtual on __APPLE__ builds; every Mac clears the bar the iOS
	/// devices of 2013 were being sorted against.
	bool isSuperFast() { return true; }

	/// Decided once at startup and never changed: it picks which input holder
	/// Minecraft builds (touch d-pad vs keyboard + mouse), which screens
	/// ScreenChooser hands out, and the default for Options::useTouchScreen.
	/// A desktop browser says false and gets the pointer-lock controls; a
	/// phone or tablet says true and gets the game's original touch UI.
	bool supportsTouchscreen() { return _touchscreen; }

	int getScreenWidth()  { return _width; }
	int getScreenHeight() { return _height; }
	void setScreenSize(int width, int height) { _width = width; _height = height; }

	float getPixelsPerMillimeter();

	const std::string& getDataDir() const { return _dataDir; }

private:
	std::string _dataDir;
	int _width;
	int _height;
	bool _touchscreen;

	int _userInputStatus;
	StringVector _userInput;

	/// How many fields the dialog currently on screen is collecting, and so also
	/// whether there is one: zero means nothing is waiting to be answered.
	int _dialogFields;
};

#endif /*APPPLATFORM_SDL_H__*/
