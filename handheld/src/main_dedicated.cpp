#include <iostream>
#include "NinecraftApp.h"
#include "AppPlatform.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "world/level/LevelSettings.h"
#include "world/level/Level.h"
#include "server/ArgumentsSettings.h"
#include "platform/time.h"
#include "SharedConstants.h"

#define MAIN_CLASS NinecraftApp
static App* g_app = 0;
static int g_exitCode = 0;
void signal_callback_handler(int signum) {
	std::cout << "Signum caught: " << signum << std::endl;
	if(signum == 2 || signum == 3){ // SIGINT ||  SIGQUIT

		if(g_app != 0) {
			g_app->quit();
		} else {
			exit(g_exitCode);
		}
	}
}

int main(int numArguments, char* pszArgs[]) {
	ArgumentsSettings aSettings(numArguments, pszArgs);
	if(aSettings.getShowHelp()) {
		ArgumentsSettings defaultSettings(0, NULL);
		printf("Minecraft Pockect Edition Server %s\n", Common::getGameVersionString("").c_str());
		printf("-------------------------------------------------------\n");
		printf("--cachepath - Path to where the server can store temp stuff (not sure if this is used) [default: \"%s\"]\n", defaultSettings.getCachePath().c_str());
		printf("--externalpath - The path to the place where the server should store the levels. [default: \"%s\"]\n", defaultSettings.getExternalPath().c_str());
		printf("--levelname - The name of the server [default: \"%s\"]\n", defaultSettings.getLevelName().c_str());
		printf("--leveldir - The name of the server [default: \"%s\"]\n", defaultSettings.getLevelDir().c_str());
		printf("--help - Shows this message.\n");
		printf("--port - The port to run the server on. [default: %d]\n", defaultSettings.getPort());
		printf("--gamemode - survival or creative. [default: creative]\n");
		printf("--serverkey - The key that the server should use for API calls. [default: \"%s\"]\n", defaultSettings.getServerKey().c_str());
		printf("-------------------------------------------------------\n");
		return 0;
	}
	printf("Level Name: %s\n", aSettings.getLevelName().c_str());
	AppContext appContext;
	appContext.platform = new AppPlatform();
	App* app = new MAIN_CLASS();
	signal(SIGINT, signal_callback_handler);
	g_app = app;
	((MAIN_CLASS*)g_app)->externalStoragePath = aSettings.getExternalPath();
	((MAIN_CLASS*)g_app)->externalCacheStoragePath = aSettings.getCachePath();

	g_app->init(appContext);
	// Creative unless asked otherwise, which is what this always did -- but it is
	// now a choice the caller makes rather than one compiled in, because a
	// dedicated world people actually play on is usually meant to be survival.
	LevelSettings settings(getEpochTimeS(),
	                       aSettings.getGameType() == 0 ? GameType::Survival : GameType::Creative);
	float startTime = getTimeS();
	((MAIN_CLASS*)g_app)->selectLevel(aSettings.getLevelDir(), aSettings.getLevelName(),  settings);
	((MAIN_CLASS*)g_app)->hostMultiplayer(aSettings.getPort());

	std::cout << "Level has been generated in " << getTimeS() - startTime << std::endl;
	((MAIN_CLASS*)g_app)->level->saveLevelData();
	std::cout << "Level has been saved!" << std::endl;
	while(!app->wantToQuit()) {
		app->update();
		//pthread_yield();
		// Milliseconds, not seconds. sleep(20) is twenty seconds a lap, and the
		// tick accounting hides how badly that misses: Timer::advanceTime clamps
		// the gap it will account for to one second and then clamps again at
		// MAX_TICKS_PER_UPDATE, so a 20s lap buys 10 ticks rather than 400.
		// Against timer(20) that is 0.5 ticks/s -- measured at 36 ticks in 60s,
		// where a server keeping up owes 1200.
		usleep(20 * 1000);
	}
	((MAIN_CLASS*)g_app)->level->saveLevelData();
	delete app;
	appContext.platform->finish();
	delete appContext.platform;
	std::cout << "Quit correctly" << std::endl;
	return g_exitCode;
}