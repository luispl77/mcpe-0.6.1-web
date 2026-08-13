/* Does a RakNet connection complete with no threads and no sockets?
 *
 * This is the part of the web multiplayer change that cannot be checked by
 * reading it: Startup() no longer spawns UpdateNetworkLoop or RecvFromLoop, the
 * game loop calls RunUpdateCycleOnce() instead, and every datagram goes through
 * a SocketLayerOverride. If that combination does not complete a handshake,
 * nothing else matters.
 *
 * Two peers in one process, wired to each other through the override the same
 * way the browser wires one peer to the relay. Built against the same
 * libraknet.a the game links, so the MC_WASM branches under test are the ones
 * that ship.
 */

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <netinet/in.h>

#include "RakPeerInterface.h"
#include "RakPeer.h"
#include "SocketLayer.h"
#include "MessageIdentifiers.h"
#include "RakNetTypes.h"
#include "MTUSize.h"
#include "GetTime.h"

static int failures = 0;

static void check(const char* what, bool ok, const char* detail = 0)
{
	printf("%s %s", ok ? "ok  " : "FAIL", what);
	if (detail) printf("  -> %s", detail);
	printf("\n");
	if (!ok) failures++;
}

struct Datagram
{
	unsigned int from;
	std::string  bytes;
};

/// One tab: a peer, the route it answers to, and what is waiting for it.
struct Endpoint
{
	RakNet::RakPeerInterface* peer;
	unsigned int              route;
	std::deque<Datagram>      inbox;
};

static Endpoint g_a;
static Endpoint g_b;

/* Whichever endpoint is being pumped right now. Single-threaded, and every send
 * RakNet makes happens inside that peer's RunUpdateCycleOnce, so this is enough
 * to know who a datagram is from -- the same question the relay answers by
 * looking at which socket a frame arrived on. */
static Endpoint* g_current = 0;

static unsigned int routeOf(const RakNet::SystemAddress& address)
{
	return ntohl(address.address.addr4.sin_addr.s_addr) & 0x00FFFFFFu;
}

static RakNet::SystemAddress addressOf(unsigned int route, unsigned short port)
{
	RakNet::SystemAddress out;
	out.address.addr4.sin_family = AF_INET;
	out.address.addr4.sin_addr.s_addr = htonl(0x0A000000u | (route & 0x00FFFFFFu));
	out.address.addr4.sin_port = htons(port);
	out.debugPort = port;
	return out;
}

static Endpoint* endpointFor(unsigned int route)
{
	if (route == g_a.route) return &g_a;
	if (route == g_b.route) return &g_b;
	return 0;
}

/// Stands in for both the page's WebSocket and the relay's switching table.
class LoopbackTransport : public RakNet::SocketLayerOverride
{
public:
	int swallowed;

	LoopbackTransport() : swallowed(0) {}

	virtual int RakNetSendTo(SOCKET s, const char* data, int length, const RakNet::SystemAddress& systemAddress)
	{
		(void) s;

		// The same filter WebRakNetInstance applies: Startup's test send and
		// Shutdown's unblock send both go to loopback, and must not be routed.
		if ((ntohl(systemAddress.address.addr4.sin_addr.s_addr) & 0xFF000000u) != 0x0A000000u)
		{
			swallowed++;
			return length;
		}

		Endpoint* target = endpointFor(routeOf(systemAddress));
		if (target && g_current)
		{
			Datagram d;
			d.from = g_current->route;
			d.bytes.assign(data, data + length);
			target->inbox.push_back(d);
		}
		return length;
	}

	virtual int RakNetRecvFrom(const SOCKET sIn, RakNet::RakPeer* rakPeerIn, char dataOut[MAXIMUM_MTU_SIZE], RakNet::SystemAddress* senderOut, bool calledFromMainThread)
	{
		(void) sIn; (void) rakPeerIn; (void) calledFromMainThread;

		if (!g_current || g_current->inbox.empty())
			return 0;

		const Datagram d = g_current->inbox.front();
		g_current->inbox.pop_front();

		if ((int) d.bytes.size() > MAXIMUM_MTU_SIZE)
			return 0;

		memcpy(dataOut, d.bytes.data(), d.bytes.size());
		// The host listens on 19132; a joiner's port is its own. Answering with
		// the port the sender is reachable on is what the relay's header does.
		*senderOut = addressOf(d.from, d.from == g_a.route ? 19132 : 19133);
		return (int) d.bytes.size();
	}
};

static void pump(Endpoint& e)
{
	g_current = &e;
	static_cast<RakNet::RakPeer*>(e.peer)->RunUpdateCycleOnce();
	g_current = 0;
}

int main()
{
	LoopbackTransport transport;
	RakNet::SocketLayer::SetSocketLayerOverride(&transport);

	g_a.peer = RakNet::RakPeerInterface::GetInstance();
	g_b.peer = RakNet::RakPeerInterface::GetInstance();
	g_a.route = 1;
	g_b.route = 2;

	g_a.peer->SetTimeoutTime(20000, RakNet::UNASSIGNED_SYSTEM_ADDRESS);
	g_b.peer->SetTimeoutTime(20000, RakNet::UNASSIGNED_SYSTEM_ADDRESS);

	// The host. Exactly what WebRakNetInstance::host does.
	RakNet::SocketDescriptor hostSocket(19132, 0);
	hostSocket.socketFamily = AF_INET;
	g_a.peer->SetMaximumIncomingConnections(4);
	const RakNet::StartupResult hostResult = g_a.peer->Startup(4, &hostSocket, 1);

	check("host Startup returns RAKNET_STARTED with no threads", hostResult == RakNet::RAKNET_STARTED);
	check("host is active", g_a.peer->IsActive());
	check("Startup's loopback test send was swallowed, not routed", transport.swallowed > 0);

	// The joiner. Exactly what WebRakNetInstance::connect does.
	RakNet::SocketDescriptor joinSocket(19133, 0);
	joinSocket.socketFamily = AF_INET;
	const RakNet::StartupResult joinResult = g_b.peer->Startup(4, &joinSocket, 1);
	check("joiner Startup returns RAKNET_STARTED", joinResult == RakNet::RAKNET_STARTED);

	const RakNet::SystemAddress hostAddress = addressOf(g_a.route, 19132);
	char hostText[64];
	strcpy(hostText, hostAddress.ToString(false));

	const RakNet::ConnectionAttemptResult attempt =
		g_b.peer->Connect(hostText, 19132, NULL, 0, NULL, 0, 12, 500, 0);
	check("Connect to a synthetic 10/8 address is accepted",
	      attempt == RakNet::CONNECTION_ATTEMPT_STARTED, hostText);

	// Turn the crank the way Minecraft::update does, and watch for the two
	// events the game actually keys off.
	bool joinerConnected = false;
	bool hostSawClient = false;
	RakNet::RakNetGUID joinerGuid;

	for (int frame = 0; frame < 4000 && !(joinerConnected && hostSawClient); ++frame)
	{
		pump(g_a);
		pump(g_b);

		RakNet::Packet* p;
		while ((p = g_a.peer->Receive()) != NULL)
		{
			if (p->data[0] == ID_NEW_INCOMING_CONNECTION) { hostSawClient = true; joinerGuid = p->guid; }
			g_a.peer->DeallocatePacket(p);
		}
		while ((p = g_b.peer->Receive()) != NULL)
		{
			if (p->data[0] == ID_CONNECTION_REQUEST_ACCEPTED) joinerConnected = true;
			if (p->data[0] == ID_CONNECTION_ATTEMPT_FAILED)   { printf("     (connection attempt failed)\n"); frame = 4000; }
			g_a.peer->DeallocatePacket(p);
		}
	}

	check("joiner got ID_CONNECTION_REQUEST_ACCEPTED", joinerConnected);
	check("host got ID_NEW_INCOMING_CONNECTION", hostSawClient);

	// A game packet, which is the thing all of this exists to carry.
	bool payloadArrived = false;
	if (joinerConnected && hostSawClient)
	{
		const char message[] = "\x86""hello from the joiner";
		g_b.peer->Send(message, (int) sizeof(message) - 1, HIGH_PRIORITY, RELIABLE_ORDERED, 0,
		               RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);

		for (int frame = 0; frame < 600 && !payloadArrived; ++frame)
		{
			pump(g_a);
			pump(g_b);

			RakNet::Packet* p;
			while ((p = g_a.peer->Receive()) != NULL)
			{
				if (p->data[0] == 0x86 && p->length == sizeof(message) - 1 &&
				    memcmp(p->data, message, p->length) == 0)
					payloadArrived = true;
				g_a.peer->DeallocatePacket(p);
			}
			while ((p = g_b.peer->Receive()) != NULL) g_b.peer->DeallocatePacket(p);
		}
	}
	check("a user packet crosses intact", payloadArrived);

	// Leaving a world must not spin: this is the Shutdown(0) path.
	const bool wasConnected = joinerConnected;
	const RakNet::SystemAddress leaving = g_b.peer->GetSystemAddressFromIndex(0);
	check("joiner can name the host it is leaving",
	      leaving != RakNet::UNASSIGNED_SYSTEM_ADDRESS, leaving.ToString(true));

	g_b.peer->CloseConnection(leaving, true, 0);

	/* Spread over real time rather than four calls in a microsecond. RakNet's
	   update cycle is driven by the clock -- it decides what to send from how
	   long it has been -- so back-to-back calls are not four frames, they are
	   one frame counted four times. This is what a game loop gives it for free
	   and what a teardown has to arrange for itself. */
	const RakNet::TimeMS flushUntil = RakNet::GetTimeMS() + 60;
	while (RakNet::GetTimeMS() < flushUntil) { pump(g_b); pump(g_a); }

	g_b.peer->Shutdown(0);
	check("joiner Shutdown(0) returns", true);
	check("joiner is no longer active", !g_b.peer->IsActive());

	bool hostSawDisconnect = false;
	for (int frame = 0; frame < 200 && !hostSawDisconnect; ++frame)
	{
		pump(g_a);
		RakNet::Packet* p;
		while ((p = g_a.peer->Receive()) != NULL)
		{
			if (p->data[0] == ID_DISCONNECTION_NOTIFICATION || p->data[0] == ID_CONNECTION_LOST)
				hostSawDisconnect = true;
			g_a.peer->DeallocatePacket(p);
		}
	}
	check("host is told the joiner left, rather than waiting for a timeout",
	      wasConnected ? hostSawDisconnect : true);

	g_a.peer->Shutdown(0);
	check("host Shutdown(0) returns", true);

	RakNet::SocketLayer::SetSocketLayerOverride(0);
	RakNet::RakPeerInterface::DestroyInstance(g_a.peer);
	RakNet::RakPeerInterface::DestroyInstance(g_b.peer);

	printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
	return failures ? 1 : 0;
}
