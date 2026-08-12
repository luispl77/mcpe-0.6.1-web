#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS__DemoChooseLevelScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS__DemoChooseLevelScreen_H__

#include "ChooseLevelScreen.h"
class Button;

class SimpleChooseLevelScreen: public ChooseLevelScreen
{
public:
	SimpleChooseLevelScreen(const std::string& levelName);

	/** Used where the name and seed have already been collected -- on the web,
	    by the dialog that replaces the OS text-entry box this screen's platforms
	    had. The id is kept separate from the name because the caller has already
	    stripped it down to something legal for a directory. */
	SimpleChooseLevelScreen(const std::string& levelId, const std::string& levelName, int seed);

	virtual ~SimpleChooseLevelScreen();

	void init();

	void setupPositions();

	void render(int xm, int ym, float a);

	void buttonClicked(Button* button);
	bool handleBackEvent(bool isDown);

private:
	Button* bCreative;
	Button* bSurvival;
	Button* bBack;
	bool hasChosen;

	std::string levelName;
	std::string levelId;
	int seed;
	// False when built from the name alone, in which case the id is derived here
	// and the seed is the clock, as it always was.
	bool hasDetails;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS__DemoChooseLevelScreen_H__*/
