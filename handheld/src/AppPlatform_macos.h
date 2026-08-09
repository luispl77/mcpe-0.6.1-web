#ifndef APPPLATFORM_MACOS_H__
#define APPPLATFORM_MACOS_H__

#include "AppPlatform.h"
#include "platform/log.h"
#include "client/renderer/gles.h"
#include <string>

/** Desktop macOS platform layer.

    Modelled on AppPlatform_win32, but with the asset root supplied at runtime
    instead of being hard coded to "../../data/", and with the png loader
    normalising whatever colour type the file happens to use into RGBA8888. */
class AppPlatform_macos: public AppPlatform
{
public:
	static const int USERINPUT_CANCEL    =  0;
	static const int USERINPUT_OK        =  1;
	static const int USERINPUT_NOTINITED = -2;

	AppPlatform_macos(const std::string& dataDir, int width, int height)
	:	_dataDir(dataDir),
		_width(width),
		_height(height),
		_userInputStatus(USERINPUT_NOTINITED)
	{}

	/** The game asks the platform to collect text (world name, chat line, ...)
	    through showDialog() + getUserInputStatus() + getUserInput(). On Android
	    and iOS that's a native dialog; here it's a Cocoa NSAlert, run modally,
	    so the answer is already latched by the time the screen next ticks.
	    Implemented in AppPlatform_macos_dialog.mm. */
	void showDialog(int dialogId);
	int getUserInputStatus();
	StringVector getUserInput();

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

	/// Reporting no touchscreen makes Minecraft pick the keyboard + mouse
	/// input holder and default Options::useTouchScreen to false.
	bool supportsTouchscreen() { return false; }

	int getScreenWidth()  { return _width; }
	int getScreenHeight() { return _height; }
	void setScreenSize(int width, int height) { _width = width; _height = height; }

	float getPixelsPerMillimeter();

	const std::string& getDataDir() const { return _dataDir; }

private:
	std::string _dataDir;
	int _width;
	int _height;

	int _userInputStatus;
	StringVector _userInput;
};

#endif /*APPPLATFORM_MACOS_H__*/
