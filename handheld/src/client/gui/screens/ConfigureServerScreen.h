#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS__ConfigureServerScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS__ConfigureServerScreen_H__

#include "../Screen.h"
#include "ConfirmScreen.h"
#include "../components/Button.h"
#include "../components/TextBox.h"

/** Changing a dedicated world after it exists: its name, and its password.

    Not its game mode. That is written into level.dat when the world is
    generated, so offering to change it here would only be the board telling a
    story the world disagreed with.

    A world is named by the route the game dials, because a route is the only
    handle the game has ever held; the page knows the manager's id for it and
    does the translation. Only ever reached for a world this browser made --
    the button is absent otherwise, rather than present and refused. */
class ConfigureServerScreen: public Screen
{
	typedef Screen super;
public:
	ConfigureServerScreen(unsigned int route, const std::string& name, bool locked);
	~ConfigureServerScreen();

	virtual void init();
	virtual void setupPositions();
	virtual void tick();
	virtual void render(int xm, int ym, float a);
	virtual bool handleBackEvent(bool isDown);
	virtual bool isInGameScreen();

protected:
	virtual void buttonClicked(Button* button);

private:
	void drawLabel(const std::string& caption, const TextBox& field);

	unsigned int _route;
	bool _locked;

	Button* bSave;
	Button* bCancel;
	Button* bClear;

	TextBox tName;
	TextBox tPassword;

	enum State { STATE_EDITING, STATE_SENDING };
	State _state;
	std::string _error;
};

/** The confirm in front of deleting one.

    Modelled on TouchDeleteWorldScreen, which is what the game already puts in
    front of losing a world. It stays up while the manager answers rather than
    dropping straight back to the list: a delete that quietly failed would look
    exactly like one that worked until the row came back. */
class DeleteServerScreen: public ConfirmScreen
{
	typedef ConfirmScreen super;
public:
	DeleteServerScreen(unsigned int route, const std::string& name);

	virtual void tick();
	virtual void render(int xm, int ym, float a);

protected:
	virtual void postResult(bool isOk);

private:
	unsigned int _route;
	bool _deleting;
	std::string _error;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS__ConfigureServerScreen_H__*/
