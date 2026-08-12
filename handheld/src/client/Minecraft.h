#ifndef NET_MINECRAFT_CLIENT__Minecraft_H__
#define NET_MINECRAFT_CLIENT__Minecraft_H__

#include "Options.h"
#ifndef STANDALONE_SERVER
#include "MouseHandler.h"
#endif
#include "Timer.h"
#include "player/input/ITurnInput.h"
#ifndef STANDALONE_SERVER
#include "gui/Gui.h"
#include "gui/screens/ScreenChooser.h"
#endif
//#include "../network/RakNetInstance.h"
#include "../world/phys/HitResult.h"

class User;
class Level;
class LocalPlayer;
class IInputHolder;
class Mob;
class Player;
class LevelRenderer;
class GameRenderer;
class ParticleEngine;
class Entity;
class ICreator;
class GameMode;
class Textures;
class CThread;
class SoundEngine;
class Screen;
class Font;
class LevelStorageSource;
class BuildActionIntention;
class PerfRenderer;
class LevelSettings;
class IRakNetInstance;
class NetEventCallback;
class CommandServer;
struct PingedCompatibleServer;
//class ExternalFileLevelStorageSource;


#include "../App.h"
#include "PixelCalc.h"
class AppPlatform;
class AppPlatform_android;

class Minecraft: public App
{
protected:
	Minecraft();
public:
	virtual ~Minecraft();

	void init();
	void setSize(int width, int height);
	void reloadOptions();

	bool supportNonTouchScreen();
	bool useTouchscreen();
	void grabMouse();
	void releaseMouse();
	void updateMusic();

	void handleBuildAction(BuildActionIntention*);

	void toggleDimension(){}
	bool isCreativeMode();
	void setIsCreativeMode(bool isCreative);
	void setScreen(Screen*);

	virtual void selectLevel(const std::string& levelId, const std::string& levelName, const LevelSettings& settings);
	virtual void setLevel(Level* level, const std::string& message = "", LocalPlayer* forceInsertPlayer = NULL);

	void generateLevel( const std::string& message, Level* level );
	LevelStorageSource* getLevelSource();

	bool isLookingForMultiplayer;
	void locateMultiplayer();
	void cancelLocateMultiplayer();
	bool joinMultiplayer(const PingedCompatibleServer& server);
	void hostMultiplayer(int port=19132);
	Player* respawnPlayer(int playerId);
	void respawnPlayer();
	void resetPlayer(Player* player);
	void doActuallyRespawnPlayer();

	void update();

	void tick(int nTick, int maxTick);
	void tickInput();

	bool isOnlineClient();
	bool isOnline();
	void pauseGame(bool isBackPaused);
	void gameLostFocus();

	void prepareLevel(const std::string& message);

	/** prepareLevel, in three parts, so the web can run it across frames.

	    Every other platform generates on a thread while the main loop keeps
	    drawing ProgressScreen, which is where the loading bar comes from. There
	    are no threads in a browser, so prepareLevel ran inline and blocked the
	    one thread the page has: nothing painted, and a world load looked like
	    the menu freezing.

	    Nothing here is a new state for the game. isGeneratingLevel is already a
	    supported condition with the level half-built and the main loop running
	    -- it is exactly what the threaded platforms sit in -- so the web now
	    drives that same state cooperatively instead of from a thread.

	    prepareLevel() itself still calls all three back to back, so every other
	    platform runs the identical sequence it always did. */
	void prepareLevelBegin();
	/// Runs at most maxIterations of the chunk pass, or all of them when
	/// maxIterations <= 0. Returns true once the pass is finished.
	bool prepareLevelChunkStep(int maxIterations);
	void prepareLevelFinish();

	void leaveGame(bool renameLevel = false);

	int getProgressStatusId();
	const char* getProgressMessage();

	ICreator* getCreator();

	// void onGraphicsLost() {}
	void onGraphicsReset();

	bool isLevelGenerated();
	int getLicenseId();

    void audioEngineOn();
    void audioEngineOff();
    
	bool isPowerVR() { return _powerVr; }
	bool isKindleFire(int kindleVersion);
	bool transformResolution(int* w, int* h);
	void optionUpdated(const Options::Option* option, bool value);
	void optionUpdated(const Options::Option* option, float value);
	void optionUpdated(const Options::Option* option, int value);
#ifdef __APPLE__
    bool _isSuperFast;
    bool isSuperFast() { return _isSuperFast; }
#endif

protected:
	void _levelGenerated();

private:
	static void* prepareLevel_tspawn(void *p_param);

	void handleMouseClick(int button);
	void handleMouseDown(int button, bool down);

	void _reloadInput();
public:
	int width;
	int height;

	// Vars that the platform is allowed to use in the future
	int commandPort;
	int reserved_d1, reserved_d2;
	float reserved_f1, reserved_f2;

	Options options;

	static bool useAmbientOcclusion;
	//static bool threadInterrupt;

	volatile bool pause;

	LevelRenderer*  levelRenderer;
	GameRenderer*   gameRenderer;
	ParticleEngine* particleEngine;
	SoundEngine*    soundEngine;

	GameMode* gameMode;
#ifndef STANDALONE_SERVER
	Textures* textures;
	ScreenChooser screenChooser;
	Font* font;
#endif
	IRakNetInstance*  raknetInstance;
	NetEventCallback* netCallback;

	int lastTime;
	int lastTickTime;
	float ticksSinceLastUpdate;

	User*  user;
	Level* level;

	LocalPlayer*	player;
	IInputHolder*	inputHolder;
	Mob*			cameraTargetPlayer;
#ifndef STANDALONE_SERVER
	Gui gui;
#endif
	CThread* generateLevelThread;
	Screen* screen;
	static int customDebugId;

	static const int CDI_NONE = 0;
	static const int CDI_GRAPHICS = 1;
#ifndef STANDALONE_SERVER
	MouseHandler mouseHandler;
#endif
	bool mouseGrabbed;

	// What updateMusic() last told the page, so it only speaks on a change.
	// The volume starts at an impossible value to force that first push.
	bool _musicOn;
	float _musicVolume;

    PixelCalc pixelCalc;
    PixelCalc pixelCalcUi;

	HitResult hitResult;
	volatile int progressStagePercentage;

	// This field is initialized in main()
	// It sets the base path to where worlds can be written (sdcard on android)
	std::string externalStoragePath;
	std::string externalCacheStoragePath;
protected:
	Timer timer;
    // @note @attn @warn: this is dangerous as fuck!
	volatile bool isGeneratingLevel;
	bool _hasSignaledGeneratingLevelFinished;

	/// How far the chunk pass has got, so it can be resumed next frame.
	int _prepChunkIndex;
	/// Set while the web is driving prepareLevel a slice at a time.
	bool _prepInProgress;

	LevelStorageSource* storageSource;
	bool _running;
	bool _powerVr;

private:
	volatile int progressStageStatusId;
	static const char* progressMessages[];

	int missTime;
	int ticks;
	bool screenMutex;
	bool hasScheduledScreen;
	Screen* scheduledScreen;

	int _licenseId;
	bool _supportsNonTouchscreen;

	bool _isCreativeMode;
	//int _respawnPlayerTicks;
	Player* _pendingRemovePlayer; // @attn @todo @fix: remove this shait and fix the respawn behaviour

	PerfRenderer* _perfRenderer;
	CommandServer* _commandServer;
};

#endif /*NET_MINECRAFT_CLIENT__Minecraft_H__*/
