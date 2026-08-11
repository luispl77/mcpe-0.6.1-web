#ifndef NET_MINECRAFT_CLIENT__OptionsFile_H__
#define NET_MINECRAFT_CLIENT__OptionsFile_H__

//package net.minecraft.client;
#include <string>
#include <vector>
typedef std::vector<std::string> StringVector;
class OptionsFile
{
public:
	OptionsFile();
    void save(const StringVector& settings);
	StringVector getOptionStrings();

	/** Point the file at a writable directory. Without this the path is
	    relative to the working directory, which is wherever the binary happened
	    to be launched from -- and on the web is a non-persistent MEMFS root. */
	void setDirectory(const std::string& dir);

private:
	std::string settingsPath;
};

#endif /* NET_MINECRAFT_CLIENT__OptionsFile_H__ */
