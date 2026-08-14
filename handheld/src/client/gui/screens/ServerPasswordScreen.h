#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS__ServerPasswordScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS__ServerPasswordScreen_H__

#include "../Screen.h"
#include "../components/Button.h"
#include "../components/TextBox.h"
#include "../../../network/RakNetInstance.h"

/** The password for a locked server, asked before joining it.

    The lock is enforced by the relay's switch, which drops datagrams for a
    server this socket has not opened. That is the right place for it -- being
    refused should cost a stranger a datagram rather than a seat -- but it makes
    the refusal silent, so something has to notice and ask.

    That something used to be the page, which watched for the first datagram to
    a locked route and put a prompt up. It works, and it is the wrong moment:
    RakNet has begun its connection attempt by then and abandons it after about
    six seconds, which is not enough time to read a prompt, remember a password
    and type it on a phone. Every join to a locked server was a race against a
    countdown that had already started.

    Asked here, nothing is counting. The route is opened first and joinMultiplayer
    is not called until it is, so the connection that starts is one that can
    finish. */
class ServerPasswordScreen: public Screen
{
	typedef Screen super;
public:
	ServerPasswordScreen(const PingedCompatibleServer& server, unsigned int route);
	~ServerPasswordScreen();

	virtual void init();
	virtual void setupPositions();
	virtual void tick();
	virtual void render(int xm, int ym, float a);
	virtual bool handleBackEvent(bool isDown);
	virtual bool isInGameScreen();

protected:
	virtual void buttonClicked(Button* button);

private:
	PingedCompatibleServer _server;
	unsigned int _route;

	Button* bJoin;
	Button* bCancel;

	TextBox tPassword;

	enum State { STATE_EDITING, STATE_SENDING };
	State _state;
	std::string _error;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS__ServerPasswordScreen_H__*/
