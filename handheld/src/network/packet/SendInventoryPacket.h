#ifndef NET_MINECRAFT_NETWORK_PACKET__SendInventoryPacket_H__
#define NET_MINECRAFT_NETWORK_PACKET__SendInventoryPacket_H__

#include "../Packet.h"

/** A player's whole inventory in one message, in either direction.

    This used to travel exactly once per life: the client sent it, with
    ExtraDrop set, at the moment of death. Which meant the server's copy of a
    player's inventory was empty their entire life and complete the instant
    they lost everything -- so a dedicated server saving player data had
    nothing real to save.

    Now it also travels on login (server to client: what this world remembers
    of you) and whenever the client's inventory has changed for a couple of
    seconds (client to server: what I am actually holding). The client is the
    authority in this protocol -- crafting and container moves never reach the
    server as anything else -- so the server takes what it is told and writes
    that to disk.

    ExtraLinks guards the hotbar layout on the wire, so a peer built before it
    existed still parses the rest: the flag is only set by a sender that will
    write the bytes, and only a reader that sees it looks for them. */
class SendInventoryPacket: public Packet
{
public:
	SendInventoryPacket()
	:	entityId(0),
		numItems(0),
		extra(0)
	{
		for (int i = 0; i < NumLinks; ++i)
			links[i] = -1;
	}

	SendInventoryPacket(Player* player, bool dropItems)
	:	entityId(player->entityId),
		extra((dropItems? ExtraDrop : 0) | ExtraLinks)
	{
        Inventory* inv = player->inventory;
		numItems = 0;
        for (int i = Inventory::MAX_SELECTION_SIZE; i < inv->getContainerSize(); ++i) {
			++numItems;
			ItemInstance* item = inv->getItem(i);
			items.push_back(item? *item : ItemInstance());
        }
        for (int i = 0; i < NumArmorItems; ++i) {
            ItemInstance* item = player->getArmor(i);
            items.push_back(item? *item : ItemInstance());
        }
		for (int i = 0; i < NumLinks; ++i)
			links[i] = (char)inv->linkedSlots[i].inventorySlot;
	}

	void write(RakNet::BitStream* bitStream)
	{
		bitStream->Write((RakNet::MessageID)(ID_USER_PACKET_ENUM + PACKET_SENDINVENTORY));
		bitStream->Write(entityId);
		bitStream->Write(extra);
		bitStream->Write(numItems);
        // Inventory
		for (int i = 0; i < numItems; ++i)
			PacketUtil::writeItemInstance(items[i], bitStream);
        // Armor
        for (int i = 0; i < NumArmorItems; ++i)
            PacketUtil::writeItemInstance(items[i + numItems], bitStream);
        // Hotbar links, behind their flag
        if (extra & ExtraLinks)
            for (int i = 0; i < NumLinks; ++i)
                bitStream->Write(links[i]);
	}

	void read(RakNet::BitStream* bitStream)
	{
		bitStream->Read(entityId);
		bitStream->Read(extra);
		bitStream->Read(numItems);
		items.clear();
        // Inventory, Armor
		for (int i = 0; i < numItems + NumArmorItems; ++i)
			items.push_back(PacketUtil::readItemInstance(bitStream));
		for (int i = 0; i < NumLinks; ++i)
			links[i] = -1;
		if (extra & ExtraLinks)
			for (int i = 0; i < NumLinks; ++i)
				bitStream->Read(links[i]);
	}

	void handle(const RakNet::RakNetGUID& source, NetEventCallback* callback)
	{
		callback->handle(source, (SendInventoryPacket*)this);
	}

    int entityId;
	std::vector<ItemInstance> items;
	short numItems;
	unsigned char extra;
	char links[9];

	static const int ExtraDrop = 1;
	static const int ExtraLinks = 2;
    static const int NumArmorItems = 4;
	static const int NumLinks = 9;   // == Inventory::MAX_SELECTION_SIZE, fixed on the wire
};

#endif /*NET_MINECRAFT_NETWORK_PACKET__SendInventoryPacket_H__*/
