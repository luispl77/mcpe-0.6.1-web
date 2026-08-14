#ifndef APPPLATFORM_H__
#define APPPLATFORM_H__

#include <vector>
#include <string>
#include <cstring>
#include "client/renderer/TextureData.h"

typedef std::vector<std::string> StringVector;

/*
typedef struct UserInput
{
    static const int STATUS_INVALID = -1;
    static const int STATUS_NOTINITED = -2;
    static const int STATUS_OK = 1;
    static const int STATUS_CANCEL = 0;

    UserInput(int id)
    :   _id(id),
        status(STATUS_NOTINITED)
    {}
    UserInput(int id, int status)
    :   _id(id),
        status(status)
    {}
    int getId() { return _id; }

    int status;
private:
    int _id;
} UserInput;


class UserInputStatus {
	int _status;
public:
	UserInputStatus(int status)
	:	_status(status)
	{}
	bool isAnswered() { return _status >= 0; }
	bool isOk() { return _status == UserInput::STATUS_OK; }
	bool isCancel() { return _status == UserInput::STATUS_CANCEL; }
};
*/

class BinaryBlob {
public:
	BinaryBlob()
	:	data(NULL),
		size(-1) {}

	BinaryBlob(unsigned char* data, unsigned int size)
	:	data(data),
		size(size) {}

	unsigned char* data;
	int size;
};

class PlatformStringVars {
public:
	static const int DEVICE_BUILD_MODEL = 0;
};

class AppPlatform
{
public:
	AppPlatform() : keyboardVisible(false) {}
    virtual ~AppPlatform() {}

	virtual void saveScreenshot(const std::string& filename, int glWidth, int glHeight) {}
	virtual TextureData loadTexture(const std::string& filename_, bool textureFolder) { return TextureData(); }

    virtual void playSound(const std::string& fn, float volume, float pitch) {}

	virtual void showDialog(int dialogId) {}
    virtual void createUserInput() {}
	
	bool is_big_endian(void)  {
		union {
			unsigned int i;
			char c[4];
		} bint = {0x01020304};
		return bint.c[0] == 1;
	} 

	void createUserInput(int dialogId)
	{
		showDialog(dialogId);
		createUserInput();
	}
	virtual int getUserInputStatus() { return 0; }
	virtual StringVector getUserInput() { return StringVector(); }

	/** Dedicated worlds, on deployments that have somewhere to put one.

	    False everywhere except a web build whose page has been given a manager
	    to talk to, which is what keeps the button off github.io without a
	    second build: the page decides, the same way it decides whether there is
	    a lobby or a relay at all. Asking here rather than testing for a URL
	    keeps the screen free of anything web-shaped.

	    createServer() is fire-and-poll for the same reason the dialogs are: the
	    answer comes back over the network whenever it comes back, and a screen
	    that polls is a screen that never blocks a frame. Status follows the
	    USERINPUT convention -- NOTINITED while it is still going. */
	virtual bool canCreateServers() { return false; }
	virtual void createServer(const std::string& name, const std::string& mode,
	                          const std::string& seed, const std::string& password) {}
	virtual int  createServerStatus() { return 0; }

	/** Managing one that already exists.

	    Keyed by the route the game dials rather than by any id of the manager's,
	    because a route is the only handle the game has ever held. The page knows
	    both and does the translation.

	    canManageServer() is what keeps a Delete button off a world somebody else
	    made: asked before the button is drawn, so a stranger's world has no
	    button rather than one that answers 403 when pressed. */
	virtual bool canManageServer(unsigned int route) { return false; }
	virtual void deleteServer(unsigned int route) {}
	virtual void configureServer(unsigned int route, const std::string& name,
	                             const std::string& password, bool setPassword) {}

	virtual std::string getDateString(int s) { return ""; }
	//virtual void createUserInputScreen(const char* types) {}

    virtual int checkLicense() { return 0; }
	virtual bool hasBuyButtonWhenInvalidLicense() { return false; }

	virtual void uploadPlatformDependentData(int id, void* data) {}
	virtual BinaryBlob readAssetFile(const std::string& filename) { return BinaryBlob(); }
	virtual void _tick() {}

	virtual int getScreenWidth() { return 854; }
	virtual int getScreenHeight() { return 480; }
    virtual float getPixelsPerMillimeter() { return 10; }

	virtual bool isNetworkEnabled(bool onlyWifiAllowed) { return true; }

	virtual bool isPowerVR() {
		return false;
	}
	virtual int getKeyFromKeyCode(int keyCode, int metaState, int deviceId) {return 0;}
#ifdef __APPLE__
    virtual bool isSuperFast() = 0;
#endif

	virtual void buyGame() {}

	virtual void finish() {}
	
	virtual bool supportsTouchscreen() { return true; }
	
	virtual void vibrate(int milliSeconds) {}

	virtual std::string getPlatformStringVar(int stringId) {
		return "<getPlatformStringVar NotImplemented>";
	}

	virtual void showKeyboard() {
		keyboardVisible = true;
	}
	virtual void hideKeyboard() {
		keyboardVisible = false;
	}
	virtual bool isKeyboardVisible() {return keyboardVisible;}
protected:
	bool keyboardVisible;
};

#endif /*APPPLATFORM_H__*/
