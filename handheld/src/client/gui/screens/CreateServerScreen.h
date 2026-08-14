#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS__CreateServerScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS__CreateServerScreen_H__

#include "../Screen.h"
#include "../components/Button.h"
#include "../components/TextBox.h"

/** Making a dedicated world, as a screen in the game rather than a panel in the
    page.

    It was the panel first, because 0.6.1 collects every other piece of text
    that way -- chat, rename world, create world all call
    AppPlatform::createUserInput() and let the platform put something on screen.
    On the phones of 2013 that was a native dialog and looked like the device;
    in a browser it looks like a web page sitting on top of Minecraft, which is
    exactly what it is.

    So this asks the same four questions with the game's own font, buttons and
    dirt background. The fields are TextBox, which the game shipped and never
    used; see there for what a phone needs before one of them can be typed into.

    Only ever reached where a manager exists to answer -- the button that opens
    it is absent otherwise -- so there is no "servers are unavailable" state to
    render here. */
class CreateServerScreen: public Screen
{
	typedef Screen super;
public:
	CreateServerScreen();
	~CreateServerScreen();

	virtual void init();
	virtual void setupPositions();
	virtual void tick();
	virtual void render(int xm, int ym, float a);
	virtual bool handleBackEvent(bool isDown);
	virtual bool isInGameScreen();

protected:
	virtual void buttonClicked(Button* button);

private:
	void updateModeLabel();
	void drawLabel(const std::string& caption, const TextBox& field);

	/* Allocated in init() rather than held by value, because which kind of
	 * button this screen wants is not known until it has a Minecraft to ask --
	 * and on the web the menus are the touch ones whatever the pointer is. */
	Button* bCreate;
	Button* bCancel;
	Button* bMode;

	TextBox tName;
	TextBox tSeed;
	TextBox tPassword;

	bool creative;

	enum State { STATE_EDITING, STATE_SENDING };
	State _state;
	std::string _error;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS__CreateServerScreen_H__*/
