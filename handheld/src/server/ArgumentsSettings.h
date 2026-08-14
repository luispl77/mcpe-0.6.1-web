#ifndef NET_MINECRAFT_WORLD_LEVEL__ArgumentsSettings_H__
#define NET_MINECRAFT_WORLD_LEVEL__ArgumentsSettings_H__
#include <string>
class ArgumentsSettings {
public:
	ArgumentsSettings(int numArguments, char** arguments);
	std::string getExternalPath();
	std::string getLevelName();
	std::string getServerKey();
	std::string getCachePath();
	std::string getLevelDir();
	bool getShowHelp();
	int getPort();
	/// 1 for Creative, 0 for Survival. Creative is what this always was.
	int getGameType();
	/// Blank means the server picks one from the clock, as it always did.
	std::string getSeed();
private:
	std::string cachePath;
	std::string externalPath;
	std::string levelName;
	std::string levelDir;
	std::string serverKey;
	bool showHelp;
	int port;
	int gameType;
	std::string seed;
};

#endif
