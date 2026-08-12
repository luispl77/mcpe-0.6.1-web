#ifndef _MINECRAFT_NETWORK_WEBLOBBYRAKNETINSTANCE_H_
#define _MINECRAFT_NETWORK_WEBLOBBYRAKNETINSTANCE_H_

#if defined(MC_WASM)

#include <string>

#include "RakNetInstance.h"

/** Sets the world name shown beside the player in the lobby list.

    Separate from host() because that is the call which announces a world is
    running and its signature carries only the player's name. Called from
    Minecraft::setLevel, where the level first exists. */
void mcpeLobbySetWorld(const std::string& worldName);

/** The Join Game list on the web, without RakNet under it.

    Two reasons this exists rather than the real RakNetInstance.

    The first is that RakNetInstance crashes the page. pingForHosts() calls
    RakPeer::Startup() and drops its return value, then pings regardless.
    Startup cannot succeed in a browser -- it ends with

        while (isRecvFromLoopThreadActive.GetValue() < socketDescriptorCount)
            RakSleep(10);

    waiting on threads that pthread_create cannot make in a build with no
    -pthread, so it either spins the one thread the page has for ever or bails
    out having emptied socketList. RakPeer::Ping then does

        RakAssert(connectionSocketIndex < socketList.Size());
    //	if ( IsActive() == false )
    //		return;
        ... socketList[realIndex]->boundAddress ...

    with the assert compiled out in release and the guard below it commented out
    in 2013, so it indexes an empty array and traps. Opening the card killed the
    tab.

    The second is that there is nothing for it to find anyway. Discovery is a
    UDP broadcast to 255.255.255.255 and a tab has no UDP. So the list is filled
    from a presence service instead -- tools/lobby -- which the page talks to and
    this class reads back through window.mcpeLobby.

    What it deliberately does not do is connect(). Appearing in the list and
    being joinable are different problems: joining needs the game's traffic
    relayed, which is a bridge and a patched RakNet, not a list. connect()
    returns false and the screen stays put.

    Everything inherited from IRakNetInstance is a no-op that returns "no", which
    is what the rest of the game already expects from a session that is hosting
    nothing and connected to nobody. */
class WebLobbyRakNetInstance : public IRakNetInstance
{
public:
	WebLobbyRakNetInstance();
	virtual ~WebLobbyRakNetInstance();

	/// Starts the page polling the lobby. The port is ignored; there is no LAN.
	virtual void pingForHosts(int basePort);
	virtual void stopPingForHosts();
	virtual const ServerList& getServerList();
	virtual void clearServerList();

	/// Both mean "I am playing now" -- see Minecraft::hostMultiplayer.
	virtual bool host(const std::string& localName, int port, int maxConnections = 4);
	virtual void announceServer(const std::string& localName);

	/// Stops announcing. Called on leaveGame.
	virtual void disconnect();

	/// Always false: see the note above about listing versus joining.
	virtual bool connect(const char* host, int port);

	/** A real peer that is never started. It has to be a real one.

	    ServerSideNetworkHandler is a LevelListener, and its tileChanged() -- one
	    of the most-fired callbacks there is while a world loads -- goes straight
	    to rakPeer->Send(), where rakPeer is whatever getPeer() handed back at
	    construction. Inheriting the base, which returns NULL, meant the first
	    tile change of the first world load dereferenced null and took the page
	    down with "memory access out of bounds".

	    RakNetInstance always built one in its constructor, so sending into an
	    unstarted peer is the behaviour every world load on the web has always
	    had, and it is harmless: the peer is inactive, so the sends go nowhere.
	    Startup() and Ping(), the two calls that actually cannot work in a
	    browser, are simply never reached from here. */
	virtual RakNet::RakPeerInterface* getPeer();

private:
	/// Pulls whatever the page last heard and rebuilds _servers from it.
	void refresh();

	RakNet::RakPeerInterface* _peer;
	ServerList _servers;
	bool       _listing;
};

#endif /* MC_WASM */

#endif /*_MINECRAFT_NETWORK_WEBLOBBYRAKNETINSTANCE_H_*/
