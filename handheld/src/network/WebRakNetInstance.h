#ifndef _MINECRAFT_NETWORK_WEBRAKNETINSTANCE_H_
#define _MINECRAFT_NETWORK_WEBRAKNETINSTANCE_H_

#if defined(MC_WASM)

#include <string>

#include "RakNetInstance.h"

/** Sets the world name shown beside the player in the lobby list.

    Separate from host() because that is the call which announces a world is
    running and its signature carries only the player's name. Called from
    Minecraft::setLevel, where the level first exists. */
void mcpeLobbySetWorld(const std::string& worldName);

/** Multiplayer on the web: real RakNet, over a WebSocket instead of UDP.

    Three things have to be true at once for 0.6.1's networking to work in a tab,
    and this class plus the MC_WASM branches in RakPeer/SocketLayer are them.

    **There is no UDP.** RakNet already has the seam for this --
    SocketLayer::SetSocketLayerOverride, whose RakNetSendTo/RakNetRecvFrom are
    called in place of sendto/recvfrom. The override here hands datagrams to the
    page, which puts them on one WebSocket to the relay; the relay is a switch
    that forwards them to whichever other tab they are addressed to. RakNet's own
    handshake, reliability and ordering ride on top unchanged, because from its
    point of view nothing has moved.

    **There are no threads.** Startup() normally spawns UpdateNetworkLoop and a
    RecvFromLoop per socket, and pthread_create cannot make either in a build
    with no -pthread. Startup() skips them under MC_WASM and runEvents() calls
    RakPeer::RunUpdateCycleOnce() once a frame instead. Receiving comes along for
    free: RunUpdateCycle already drains an installed override on the calling
    thread, which is a path RakNet shipped with in 2013.

    **There is no broadcast.** Discovery is a UDP ping to 255.255.255.255, so the
    server list is filled from the lobby service instead -- see tools/lobby --
    which the page talks to and this class reads back through window.mcpeLobby.

    With no relay configured the page leaves window.mcpeNet undefined, and this
    degrades to exactly what the build did before: the list still fills from the
    lobby if there is one, host() announces, and connect() refuses. That is what
    github.io serves. */
/** The relay slot an address was built out of.

    Declared here because the menus need it: a row in the games list carries an
    address, and every call that manages a dedicated world names it by route
    instead. Defined in WebRakNetInstance.cpp. */
unsigned int mcpeRouteOf(const RakNet::SystemAddress& address);

class WebRakNetInstance : public IRakNetInstance
{
public:
	WebRakNetInstance();
	virtual ~WebRakNetInstance();

	/// Starts the peer listening and tells the lobby a world is running.
	virtual bool host(const std::string& localName, int port, int maxConnections = 4);
	virtual void announceServer(const std::string& localName);

	/** Joins the player the address belongs to, via the relay.

	    The address is not routable and is not meant to be: getServerList() built
	    it out of the other tab's relay slot, and the override reads that slot
	    back out to address the datagram. See routeFromAddress(). */
	virtual bool connect(const char* host, int port);

	virtual void disconnect();
	virtual void setIsLoggedIn(bool status);

	/// The lobby list, not a broadcast. The port is ignored; there is no LAN.
	virtual void pingForHosts(int basePort);
	virtual void stopPingForHosts();
	virtual const ServerList& getServerList();
	virtual void clearServerList();

	virtual RakNet::RakPeerInterface* getPeer();
	virtual bool isMyLocalGuid(const RakNet::RakNetGUID& guid);

	/** Drives the peer and then dispatches whatever it produced.

	    Both halves matter and the order does. RunUpdateCycleOnce() is what
	    replaces the update thread -- without it Connect() and Send() sit in the
	    buffered-command queues for ever and nothing times out or resends. */
	virtual void runEvents(NetEventCallback* callback);

	virtual void send(Packet& packet);
	virtual void send(const RakNet::RakNetGUID& guid, Packet& packet);
	// @attn: Those delete the packet
	virtual void send(Packet* packet);
	virtual void send(const RakNet::RakNetGUID& guid, Packet* packet);

	virtual bool isServer() { return _isServer; }
	virtual bool isProbablyBroken();
	virtual void resetIsBroken();

private:
	/// Pulls whatever the page last heard from the lobby and rebuilds _servers.
	void refresh();

	RakNet::RakPeerInterface* _peer;
	RakNet::RakNetGUID        _serverGuid;

	ServerList _servers;
	bool       _listing;
	bool       _isServer;
	bool       _isLoggedIn;
};

#endif /* MC_WASM */

#endif /*_MINECRAFT_NETWORK_WEBRAKNETINSTANCE_H_*/
