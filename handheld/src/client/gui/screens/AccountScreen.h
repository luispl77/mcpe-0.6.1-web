#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS__AccountScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS__AccountScreen_H__

#include "../Screen.h"
#include "../components/Button.h"
#include "../components/TextBox.h"

/** Signing in to the server list.

    Deliberately not a login for the game. Single player, joining somebody's
    world and hosting your own tab all work signed out and never ask -- this is
    reached from the Join Game screen and from nowhere else, because the one
    thing worth protecting is a world on somebody else's box that outlives the
    tab that made it.

    What it replaces is a secret in localStorage. That was the most ownership
    that could be checked without accounts, and it meant your own worlds were
    strangers' worlds on your phone and stopped being yours the moment you
    cleared site data. Worlds made the old way keep working exactly as they
    did; both kinds of ownership are checked, and neither is migrated behind
    anybody's back.

    One screen with two buttons rather than two screens: Log in and Create
    account ask for the same two things and differ only in which route the page
    posts to, so a second screen would be the same form with a different title. */
class AccountScreen: public Screen
{
	typedef Screen super;
public:
	AccountScreen();
	~AccountScreen();

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
	void submit(bool createNew);

	Button* bLogIn;
	Button* bCreate;
	Button* bLogOut;
	Button* bBack;

	TextBox tUser;
	TextBox tPassword;

	/* Read once, in init(), rather than every frame: which of the two layouts
	 * this screen is deciding what buttons exist, and buttons that came and
	 * went underneath a finger would be worse than a screen that is rebuilt. */
	std::string _name;

	enum State { STATE_EDITING, STATE_SENDING };
	State _state;
	bool _creating;
	std::string _error;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS__AccountScreen_H__*/
