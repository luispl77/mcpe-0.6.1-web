#include "WebRakNetInstance.h"

#if defined(MC_WASM)

#include "Packet.h"
#include "NetEventCallback.h"
#include "../raknet/RakPeerInterface.h"
#include "../raknet/RakPeer.h"
#include "../raknet/SocketLayer.h"
#include "../raknet/BitStream.h"
#include "../raknet/MessageIdentifiers.h"
#include "../raknet/GetTime.h"
#include "../AppConstants.h"

#include <emscripten.h>
#include <netinet/in.h>
#include <cstdlib>
#include <cstring>
#include <string>

/* The page owns both connections, the same way it owns touch detection: it needs
 * the endpoints for itself, and two halves disagreeing about whether multiplayer
 * is on is worse than one. window.mcpeLobby and window.mcpeNet are defined in
 * shell.html, and every bridge below tolerates either being absent -- a build
 * whose shell.html is out of step, or the github.io deployment which configures
 * neither, gets an empty list and a refused connect rather than an error. */

/// Begins heartbeating "<name> is playing <world>" until mcpe_lobby_leave.
EM_JS(void, mcpe_lobby_announce, (const char* name, const char* world), {
	if (!window.mcpeLobby) return;
	window.mcpeLobby.announce(UTF8ToString(name), UTF8ToString(world));
});

EM_JS(void, mcpe_lobby_leave, (), {
	if (window.mcpeLobby) window.mcpeLobby.leave();
});

/// Starts and stops the poll that keeps the list fresh while the card is open.
EM_JS(void, mcpe_lobby_listen, (int on), {
	if (!window.mcpeLobby) return;
	if (on) window.mcpeLobby.startListing();
	else window.mcpeLobby.stopListing();
});

/** The board as "name\tworld\troute\n" per player, as a malloc'd string.

    Records rather than JSON because the JS side already has JSON.parse and this
    side is C++03 with no parser in it. Handing over something a getline can
    read is a smaller seam than writing one. */
EM_JS(char*, mcpe_lobby_records, (), {
	var text = window.mcpeLobby ? window.mcpeLobby.records() : '';
	var len = lengthBytesUTF8(text) + 1;
	var buf = _malloc(len);
	stringToUTF8(text, buf, len);
	return buf;
});

/** Whether this deployment has multiplayer at all.

    Deliberately not "is the socket up this instant". A peer that starts while
    its socket is still connecting is fine -- the datagrams it sends meanwhile
    are lost, and losing datagrams is a case RakNet's connection attempts already
    retry through. Gating on the socket instead would mean the first world of a
    session silently came up unjoinable, because host() runs a round trip before
    the socket can possibly have answered. */
EM_JS(int, mcpe_net_enabled, (), {
	return (window.mcpeNet && window.mcpeNet.enabled()) ? 1 : 0;
});

EM_JS(void, mcpe_net_open, (), {
	if (window.mcpeNet) window.mcpeNet.open();
});

EM_JS(void, mcpe_net_close, (), {
	if (window.mcpeNet) window.mcpeNet.close();
});

/// Hands one datagram to the page, addressed to another tab's relay slot.
EM_JS(void, mcpe_net_send, (unsigned int route, const char* data, int len), {
	if (!window.mcpeNet) return;
	// slice, not subarray: this is a view onto the wasm heap, and the page may
	// still be holding it after the heap has moved or been written over.
	window.mcpeNet.send(route >>> 0, HEAPU8.slice(data, data + len));
});

/// Takes the oldest datagram the page is holding, or 0 if there is none.
EM_JS(int, mcpe_net_recv, (char* out, int max, unsigned int* routeOut), {
	if (!window.mcpeNet) return 0;
	var d = window.mcpeNet.recv();
	if (!d) return 0;
	// Longer than an MTU means the far side is not RakNet. Dropping it is what
	// a real socket would do to a datagram that does not fit the buffer.
	if (d.bytes.length > max) return 0;
	HEAPU8.set(d.bytes, out);
	HEAPU32[routeOut >> 2] = d.route >>> 0;
	return d.bytes.length;
});

/// The world name shown beside the player. Set from Minecraft::setLevel, which
/// is the first point at which the level is known.
void mcpeLobbySetWorld(const std::string& worldName)
{
	EM_ASM({
		if (window.mcpeLobby) window.mcpeLobby.setWorld(UTF8ToString($0));
	}, worldName.c_str());
}

namespace {

/// 0.6.1's LAN port. Nothing binds it here; it is the port half of the synthetic
/// addresses below, kept at the game's own value so anything that prints one
/// still reads like a Minecraft address.
const unsigned short RELAY_PORT = 19132;

/** A relay slot, encoded as an address RakNet will accept.

    RakNet keys every remote system by SystemAddress, so a peer needs one even
    though nothing here is routable. The relay hands each tab a small unique slot
    when its socket opens; putting that in the low 24 bits of 10.0.0.0/8 gives a
    value which is unique for as long as the tab is connected, is never
    0.0.0.0 or a broadcast address -- RakNet treats both specially -- and can be
    read straight back out again on the way to the wire.

    Set field by field rather than through FromString(), which would go via
    inet_addr for no reason when the bytes are already in hand. */
RakNet::SystemAddress addressFromRoute(unsigned int route)
{
	RakNet::SystemAddress out;
	out.address.addr4.sin_family = AF_INET;
	out.address.addr4.sin_addr.s_addr = htonl(0x0A000000u | (route & 0x00FFFFFFu));
	out.address.addr4.sin_port = htons(RELAY_PORT);
	out.debugPort = RELAY_PORT;
	return out;
}

unsigned int routeFromAddress(const RakNet::SystemAddress& address)
{
	return ntohl(address.address.addr4.sin_addr.s_addr) & 0x00FFFFFFu;
}

bool isRoutable(const RakNet::SystemAddress& address)
{
	return (ntohl(address.address.addr4.sin_addr.s_addr) & 0xFF000000u) == 0x0A000000u;
}

/** RakNet's own hook for "the sockets are somewhere else".

    SocketLayer::SendTo and the receive path in RunUpdateCycle both consult this
    before touching a real socket, which is why none of the game's networking
    had to change to move off UDP: everything above it still thinks it is
    talking to one. */
class WebRelayTransport : public RakNet::SocketLayerOverride
{
public:
	virtual int RakNetSendTo(SOCKET s, const char* data, int length, const RakNet::SystemAddress& systemAddress)
	{
		(void) s;

		/* Startup() test-sends four bytes to its own bound address, and
		   Shutdown() sends one there to unblock a recvfrom that does not exist.
		   Both are loopback, and the low bits of 127.0.0.1 are a perfectly
		   plausible relay slot -- so without this they would arrive as garbage
		   at whichever player happened to hold it. Swallowed rather than
		   refused: a non-negative return is what SocketLayer::SendTo reads as
		   success, and Startup() aborts with SOCKET_FAILED_TEST_SEND if it
		   sees anything else. */
		if (!isRoutable(systemAddress))
			return length;

		mcpe_net_send(routeFromAddress(systemAddress), data, length);
		return length;
	}

	virtual int RakNetRecvFrom(const SOCKET sIn, RakNet::RakPeer* rakPeerIn, char dataOut[MAXIMUM_MTU_SIZE], RakNet::SystemAddress* senderOut, bool calledFromMainThread)
	{
		(void) sIn;
		(void) rakPeerIn;
		(void) calledFromMainThread;

		unsigned int route = 0;
		const int len = mcpe_net_recv(dataOut, MAXIMUM_MTU_SIZE, &route);

		// 0 means "stop, and do not fall through to the real recvfrom" -- which
		// is both the empty case and the only correct answer here, since the
		// real one would be reading a descriptor that was never a socket.
		if (len <= 0)
			return 0;

		*senderOut = addressFromRoute(route);
		return len;
	}
};

WebRelayTransport g_transport;

} // namespace

WebRakNetInstance::WebRakNetInstance()
:	_peer(NULL),
	_listing(false),
	_isServer(false),
	_isLoggedIn(false)
{
	// Installed for the life of the process rather than per session: SocketLayer
	// holds it in a static, and every socket this build will ever have goes
	// through it. It has no state, so there is nothing to reset between worlds.
	RakNet::SocketLayer::SetSocketLayerOverride(&g_transport);

	_peer = RakNet::RakPeerInterface::GetInstance();
	_peer->SetTimeoutTime(20000, RakNet::UNASSIGNED_SYSTEM_ADDRESS);
	_peer->SetOccasionalPing(true);
}

WebRakNetInstance::~WebRakNetInstance()
{
	disconnect();
	mcpe_net_close();

	if (_peer)
	{
		RakNet::RakPeerInterface::DestroyInstance(_peer);
		_peer = NULL;
	}

	RakNet::SocketLayer::SetSocketLayerOverride(NULL);
}

RakNet::RakPeerInterface* WebRakNetInstance::getPeer()
{
	return _peer;
}

/** The peer as its concrete type, for the one call that is not on the interface.

    RakPeerInterface::GetInstance() only ever returns a RakPeer -- it is the sole
    implementation and DestroyInstance casts it back the same way -- so this is
    safe for the same reason the library's own teardown is. */
static RakNet::RakPeer* asRakPeer(RakNet::RakPeerInterface* peer)
{
	return static_cast<RakNet::RakPeer*>(peer);
}

bool WebRakNetInstance::host(const std::string& localName, int port, int maxConnections /* = 4 */)
{
	if (_peer->IsActive())
		_peer->Shutdown(0);

	// Before the announce, so that the route the announce carries is a real one
	// on the very first world of a session. The page re-announces when a route
	// arrives late, which covers the rest.
	mcpe_net_open();

	// Announced whether or not there is a relay: with none this is the github.io
	// build, where being listed is all that ever worked and connect() refuses.
	mcpe_lobby_announce(localName.c_str(), "");

	_isServer = true;
	_listing = false;

	if (!mcpe_net_enabled())
	{
		// No relay, so no peer -- starting one would only give the game a
		// network that goes nowhere. The world still runs and is still listed,
		// which is exactly what this build did before any of it existed.
		return true;
	}

	RakNet::SocketDescriptor socket(port, 0);
	socket.socketFamily = AF_INET;

	_peer->SetMaximumIncomingConnections(maxConnections);
	const RakNet::StartupResult result = _peer->Startup(maxConnections, &socket, 1);

	return (result == RakNet::RAKNET_STARTED);
}

void WebRakNetInstance::announceServer(const std::string& localName)
{
	/* ServerSideNetworkHandler::allowIncomingConnections calls this with the
	   username when the world is visible and with "" when it is not -- on a LAN
	   that swapped the name in the broadcast, and an empty one is filtered out
	   of everybody's list. Here it is the difference between being on the board
	   and not being on it, which is the same thing said in the lobby's terms. */
	if (localName.empty())
		mcpe_lobby_leave();
	else
		mcpe_lobby_announce(localName.c_str(), "");
}

bool WebRakNetInstance::connect(const char* host, int port)
{
	// github.io configures no relay, so there is nothing to carry the traffic.
	// Refusing is better than a screen that sits there: joinMultiplayer passes
	// this straight back to the Join card.
	if (!mcpe_net_enabled())
		return false;

	mcpe_net_open();

	_isLoggedIn = false;

	if (_peer->IsActive())
		_peer->Shutdown(0);

	// Port 0: a joiner needs no fixed slot of its own. Its address comes from
	// the relay, not from anything it binds.
	RakNet::SocketDescriptor socket(0, 0);
	socket.socketFamily = AF_INET;

	const RakNet::StartupResult result = _peer->Startup(4, &socket, 1);

	_isServer = false;
	_listing = false;

	if (result != RakNet::RAKNET_STARTED)
		return false;

	const RakNet::ConnectionAttemptResult connectResult =
		_peer->Connect(host, port, NULL, 0, NULL, 0, 12, 500, 0);

	return (connectResult == RakNet::CONNECTION_ATTEMPT_STARTED);
}

void WebRakNetInstance::disconnect()
{
	/* Shutdown(n) with n>0 spins for n milliseconds waiting for the remote
	   systems to go inactive, and expects the update thread to be flushing
	   disconnection notifications while it waits. There is no update thread, so
	   that wait can only ever run out the clock -- a stall on every world exit,
	   and the notifications still would not go.

	   So the crank gets turned by hand: close each connection, run update cycles
	   until the notifications are on the wire, then shut down with no block at
	   all. Anyone who misses them still drops us on the 20s timeout set in the
	   constructor, which is what used to happen to a phone leaving a LAN by
	   walking out of range. */
	if (_peer && _peer->IsActive())
	{
		const unsigned short peers = _peer->GetMaximumNumberOfPeers();
		for (unsigned short i = 0; i < peers; ++i)
		{
			const RakNet::SystemAddress address = _peer->GetSystemAddressFromIndex(i);
			if (address != RakNet::UNASSIGNED_SYSTEM_ADDRESS)
				_peer->CloseConnection(address, true, 0);
		}

		/* Over real time, not in a tight loop. RunUpdateCycle decides what to
		   send from how long it has been since the last one, so calling it four
		   times in a microsecond is one frame counted four times and the
		   notifications never leave. A game loop gives it that spacing for free;
		   a teardown has to arrange it.

		   The tab is blocked for this. It is worth it: 40ms is under three
		   frames, it is hidden inside a world exit that is already saving a
		   level, and the alternative is every other player watching a ghost
		   stand there for twenty seconds. */
		const RakNet::TimeMS flushUntil = RakNet::GetTimeMS() + 40;
		while (RakNet::GetTimeMS() < flushUntil)
			asRakPeer(_peer)->RunUpdateCycleOnce();

		_peer->Shutdown(0);
	}

	mcpe_lobby_leave();

	/* The relay socket deliberately stays up. It is the tab's line, not the
	   world's: Minecraft::hostMultiplayer calls disconnect() before host(), so
	   closing here meant every world start dropped the socket and announced
	   itself with route 0 -- listed, unjoinable, until the next heartbeat ten
	   seconds later. Keeping it also makes joining immediate rather than a round
	   trip. It is closed in the destructor, when the tab really is done. */

	_isLoggedIn = false;
	_isServer = false;
	_listing = false;
}

void WebRakNetInstance::setIsLoggedIn(bool status)
{
	_isLoggedIn = status;
}

bool WebRakNetInstance::isMyLocalGuid(const RakNet::RakNetGUID& guid)
{
	return _peer->IsActive() && _peer->GetMyGUID() == guid;
}

bool WebRakNetInstance::isProbablyBroken()
{
	return _peer->errorState < -100;
}

void WebRakNetInstance::resetIsBroken()
{
	_peer->errorState = 0;
}

void WebRakNetInstance::runEvents(NetEventCallback* callback)
{
	/* This is the update thread. Without it Connect() and Send() sit in the
	   buffered-command queues for ever, nothing is resent, and nothing times
	   out -- and incoming datagrams are never picked up either, because
	   RunUpdateCycle is where the SocketLayerOverride gets drained. It runs even
	   with no callback and outside multiplayer; an unstarted peer returns
	   immediately on its endThreads check. */
	if (_peer)
		asRakPeer(_peer)->RunUpdateCycleOnce();

	RakNet::Packet* currentEvent;

	while ((currentEvent = _peer->Receive()) != NULL)
	{
		int packetId = currentEvent->data[0];
		int length = currentEvent->length;

		RakNet::BitStream activeBitStream(currentEvent->data + 1, length - 1, false);

		if (callback) {
			if (packetId < ID_USER_PACKET_ENUM)
			{
				switch (packetId)
				{
				case ID_NEW_INCOMING_CONNECTION:
					callback->onNewClient(currentEvent->guid);
					break;
				case ID_CONNECTION_REQUEST_ACCEPTED:
					_serverGuid = currentEvent->guid;
					callback->onConnect(currentEvent->guid);
					break;
				case ID_CONNECTION_ATTEMPT_FAILED:
					callback->onUnableToConnect();
					break;
				case ID_DISCONNECTION_NOTIFICATION:
				case ID_CONNECTION_LOST:
					callback->onDisconnect(currentEvent->guid);
					break;
				// No ID_UNCONNECTED_PONG case, unlike RakNetInstance: nothing
				// pings, because the list comes from the lobby rather than from
				// a broadcast. See getServerList().
				}
			}
			else
			{
				int userPacketId = packetId - ID_USER_PACKET_ENUM;
				bool isStatusPacket = userPacketId <= PACKET_READY;

				if (isStatusPacket || _isServer || _isLoggedIn) {

					if (Packet* packet = MinecraftPackets::createPacket(packetId)) {
						packet->read(&activeBitStream);
						packet->handle(currentEvent->guid, callback);
						delete packet;
					}
				}
			}
		}

		_peer->DeallocatePacket(currentEvent);
	}
}

void WebRakNetInstance::send(Packet& packet)
{
	RakNet::BitStream bitStream;
	packet.write(&bitStream);
	if (_isServer)
	{
		// broadcast to all connected clients
		_peer->Send(&bitStream, packet.priority, packet.reliability, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
	}
	else
	{
		// send to server
		_peer->Send(&bitStream, packet.priority, packet.reliability, 0, _serverGuid, false);
	}
}

void WebRakNetInstance::send(const RakNet::RakNetGUID& guid, Packet& packet)
{
	RakNet::BitStream bitStream;
	packet.write(&bitStream);
	_peer->Send(&bitStream, packet.priority, packet.reliability, 0, guid, false);
}

void WebRakNetInstance::send(Packet* packet)
{
	send(*packet);
	delete packet;
}

void WebRakNetInstance::send(const RakNet::RakNetGUID& guid, Packet* packet)
{
	send(guid, *packet);
	delete packet;
}

void WebRakNetInstance::pingForHosts(int /*basePort*/)
{
	_listing = true;
	mcpe_lobby_listen(1);

	// Opened while the card is open so that a slot -- and therefore an address
	// other tabs can reach -- exists before anything is joined. Without this the
	// first join would have to wait a round trip for the socket to come up.
	mcpe_net_open();
}

void WebRakNetInstance::stopPingForHosts()
{
	_listing = false;
	mcpe_lobby_listen(0);
	_servers.clear();
}

void WebRakNetInstance::clearServerList()
{
	_servers.clear();
}

const ServerList& WebRakNetInstance::getServerList()
{
	if (_listing)
		refresh();
	return _servers;
}

void WebRakNetInstance::refresh()
{
	char* raw = mcpe_lobby_records();
	if (!raw)
		return;

	const std::string records = raw;
	free(raw);

	_servers.clear();

	// A player whose relay socket is down publishes a route of 0. With a relay
	// configured this card is a list of who can be joined, so they are left out
	// until their next heartbeat carries a real one; without one nobody has a
	// route at all and the card goes back to being a list of who is playing.
	const bool joinableOnly = mcpe_net_enabled() != 0;

	std::string::size_type pos = 0;
	while (pos < records.size())
	{
		const std::string::size_type end = records.find('\n', pos);
		const std::string line = records.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
		pos = (end == std::string::npos) ? records.size() : end + 1;

		if (line.empty())
			continue;

		const std::string::size_type t1 = line.find('\t');
		if (t1 == std::string::npos) continue;
		const std::string::size_type t2 = line.find('\t', t1 + 1);
		if (t2 == std::string::npos) continue;

		const std::string name  = line.substr(0, t1);
		const std::string world = line.substr(t1 + 1, t2 - t1 - 1);
		const std::string route = line.substr(t2 + 1);

		// JoinGameScreen::tick drops entries with an empty name, so a player
		// without one would silently never appear.
		if (name.empty())
			continue;

		const unsigned int routeNumber = (unsigned int) strtoul(route.c_str(), NULL, 10);
		if (joinableOnly && routeNumber == 0)
			continue;

		const std::string label = world.empty() ? name : (name + " - " + world);

		PingedCompatibleServer server;
		server.name = label.c_str();
		server.address = addressFromRoute(routeNumber);
		server.pingTime = 0;
		server.isSpecial = false;
		_servers.push_back(server);
	}
}

#endif /* MC_WASM */
