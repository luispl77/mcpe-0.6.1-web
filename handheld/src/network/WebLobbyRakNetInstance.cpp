#include "WebLobbyRakNetInstance.h"

#if defined(MC_WASM)

#include <emscripten.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <string>

/* The page owns the lobby connection, the same way it owns touch detection: it
 * needs the endpoint for itself, and two halves disagreeing about whether the
 * lobby is on is worse than one. window.mcpeLobby is defined in shell.html, and
 * every bridge below tolerates it being absent -- a build whose shell.html is
 * out of step, or a page with the lobby switched off, gets an empty list rather
 * than an error. That is the same thing an empty LAN looks like, which is
 * exactly the behaviour this had before the lobby existed. */

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

/** The board as "name\tworld\tkey\n" per player, as a malloc'd string.

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

/// The world name shown beside the player. Set from Minecraft::setLevel, which
/// is the first point at which the level is known.
void mcpeLobbySetWorld(const std::string& worldName)
{
	EM_ASM({
		if (window.mcpeLobby) window.mcpeLobby.setWorld(UTF8ToString($0));
	}, worldName.c_str());
}

WebLobbyRakNetInstance::WebLobbyRakNetInstance()
:	_listing(false)
{
}

WebLobbyRakNetInstance::~WebLobbyRakNetInstance()
{
	mcpe_lobby_leave();
}

bool WebLobbyRakNetInstance::host(const std::string& localName, int /*port*/, int /*maxConnections*/)
{
	// No port is opened and none could be. This is the game saying "a world is
	// running now", which is the only part of hosting the lobby cares about.
	mcpe_lobby_announce(localName.c_str(), "");
	return true;
}

void WebLobbyRakNetInstance::announceServer(const std::string& localName)
{
	mcpe_lobby_announce(localName.c_str(), "");
}

void WebLobbyRakNetInstance::disconnect()
{
	mcpe_lobby_leave();
}

bool WebLobbyRakNetInstance::connect(const char* /*host*/, int /*port*/)
{
	// Listing is not joining; nothing relays the game's traffic yet. The touch
	// Join screen never pushes its bJoin into buttons, so on the web nothing
	// reaches this today -- it returns false rather than pretending, so that
	// whoever does wire a join gets a refusal instead of a silent hang.
	return false;
}

void WebLobbyRakNetInstance::pingForHosts(int /*basePort*/)
{
	_listing = true;
	mcpe_lobby_listen(1);
}

void WebLobbyRakNetInstance::stopPingForHosts()
{
	_listing = false;
	mcpe_lobby_listen(0);
	_servers.clear();
}

void WebLobbyRakNetInstance::clearServerList()
{
	_servers.clear();
}

const ServerList& WebLobbyRakNetInstance::getServerList()
{
	if (_listing)
		refresh();
	return _servers;
}

/** Builds a SystemAddress that no packet will ever be sent to.

    The list is keyed by address throughout the screens -- selection survives a
    refresh by matching on it -- so every player still needs a distinct, stable
    one. It is derived from the lobby id rather than counted, because entries
    come and go and an index would slide the selection onto somebody else.

    Set field by field on purpose: FromString() would go through getaddrinfo,
    and the point of this class is that nothing here touches a socket. */
static RakNet::SystemAddress addressFromKey(unsigned long key)
{
	RakNet::SystemAddress out;
	out.address.addr4.sin_family = AF_INET;
	out.address.addr4.sin_addr.s_addr = htonl((unsigned int)key);
	out.address.addr4.sin_port = htons(19132);
	out.debugPort = 19132;
	return out;
}

void WebLobbyRakNetInstance::refresh()
{
	char* raw = mcpe_lobby_records();
	if (!raw)
		return;

	const std::string records = raw;
	free(raw);

	_servers.clear();

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
		const std::string key   = line.substr(t2 + 1);

		// JoinGameScreen::tick drops entries with an empty name, so a player
		// without one would silently never appear.
		if (name.empty())
			continue;

		const std::string label = world.empty() ? name : (name + " - " + world);

		PingedCompatibleServer server;
		server.name = label.c_str();
		server.address = addressFromKey(strtoul(key.c_str(), NULL, 10));
		server.pingTime = 0;
		server.isSpecial = false;
		_servers.push_back(server);
	}
}

#endif /* MC_WASM */
