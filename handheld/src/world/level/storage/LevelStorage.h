#ifndef NET_MINECRAFT_WORLD_LEVEL_STORAGE__LevelStorage_H__
#define NET_MINECRAFT_WORLD_LEVEL_STORAGE__LevelStorage_H__

//package net.minecraft.world.level.storage;

#include <vector>
#include <string>

class LevelData;
class ChunkStorage;
class Dimension;
class Player;
class Level;
class LevelChunk;

class LevelStorage
{
public:
	virtual ~LevelStorage() {}
	
    virtual LevelData* prepareLevel(Level* level) = 0;

	virtual ChunkStorage* createChunkStorage(Dimension* dimension) = 0;

    virtual void saveLevelData(LevelData& levelData, std::vector<Player*>* players) = 0;
	virtual void saveLevelData(LevelData& levelData) {
		saveLevelData(levelData, NULL);
	}

    virtual void closeAll() = 0;

	virtual void saveGame(Level* level) {}
	virtual void loadEntities(Level* level, LevelChunk* levelChunk) {}

	/** Where one named player was standing when they last left.

	    0.6.1 has no such thing. level.dat holds exactly one player -- LevelData
	    writes players[0] and nothing else -- which is right for the game it was
	    written for, where the one player who could be saved was the person
	    holding the phone. A dedicated world has no such person: whoever happened
	    to be first in the list got their position written and everybody else's
	    was dropped, so every join put you back at spawn.

	    Storage that has no files does nothing here and says so, which is why
	    these are not pure: MemoryLevelStorage genuinely cannot remember anybody. */
	virtual bool loadPlayerData(const std::string& name, Player* player) { return false; }
	virtual void savePlayerData(const std::string& name, Player* player) {}

	//void checkSession() throws LevelConflictException;
	//PlayerIO getPlayerIO();
};

#endif /*NET_MINECRAFT_WORLD_LEVEL_STORAGE__LevelStorage_H__*/
