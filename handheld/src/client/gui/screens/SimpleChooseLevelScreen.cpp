#include "SimpleChooseLevelScreen.h"
#include "ProgressScreen.h"
#include "ScreenChooser.h"
#include "../components/Button.h"
#include "../../Minecraft.h"
#include "../../../world/level/LevelSettings.h"
#include "../../../platform/time.h"

SimpleChooseLevelScreen::SimpleChooseLevelScreen(const std::string& levelName)
:	bCreative(0),
	bSurvival(0),
	bBack(0),
	levelName(levelName),
	seed(0),
	hasDetails(false),
	hasChosen(false)
{
}

SimpleChooseLevelScreen::SimpleChooseLevelScreen(const std::string& levelId, const std::string& levelName, int seed)
:	bCreative(0),
	bSurvival(0),
	bBack(0),
	levelName(levelName),
	levelId(levelId),
	seed(seed),
	hasDetails(true),
	hasChosen(false)
{
}

SimpleChooseLevelScreen::~SimpleChooseLevelScreen()
{
	delete bCreative;
	delete bSurvival;
	delete bBack;
}

void SimpleChooseLevelScreen::init()
{
	if (minecraft->useTouchscreen()) {
		bCreative = new Touch::TButton(1, "Creative mode");
		bSurvival = new Touch::TButton(2, "Survival mode");
		bBack	  = new Touch::TButton(3, "Back");
	} else {
		bCreative = new Button(1, "Creative mode");
		bSurvival = new Button(2, "Survival mode");
		bBack	  = new Button(3, "Back");
	}
	buttons.push_back(bCreative);
	buttons.push_back(bSurvival);
	buttons.push_back(bBack);

	tabButtons.push_back(bCreative);
	tabButtons.push_back(bSurvival);
	tabButtons.push_back(bBack);
}

void SimpleChooseLevelScreen::setupPositions()
{
	bCreative->width = bSurvival->width = bBack->width = 120;
	bCreative->x = (width - bCreative->width) / 2;
	bCreative->y = height/3 - 40;
	bSurvival->x = (width - bSurvival->width) / 2;
	bSurvival->y = 2*height/3 - 40;
	bBack->x = bSurvival->x + bSurvival->width - bBack->width;
	bBack->y = height - 40;
}

void SimpleChooseLevelScreen::render( int xm, int ym, float a )
{
	renderDirtBackground(0);
    glEnable2(GL_BLEND);

	drawCenteredString(minecraft->font, "Mobs, health and gather resources", width/2, bSurvival->y + bSurvival->height + 4, 0xffcccccc);
	drawCenteredString(minecraft->font, "Unlimited resources and flying", width/2, bCreative->y + bCreative->height + 4, 0xffcccccc);

	Screen::render(xm, ym, a);
    glDisable2(GL_BLEND);
}

void SimpleChooseLevelScreen::buttonClicked( Button* button )
{
	if (button == bBack) {
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
		return;
	}
	if (hasChosen)
		return;

	int gameType = GameType::Survival;

	if (button == bCreative)
		gameType = GameType::Creative;

	if (button == bSurvival)
		gameType = GameType::Survival;

	// Built from a name alone, the id is derived here and the seed is the clock.
	// Given the full set, the caller has already made the id legal and parsed a
	// seed the player typed, and the display name is kept as they wrote it.
	const std::string id   = hasDetails ? levelId : getUniqueLevelName(levelName);
	const std::string name = hasDetails ? levelName : id;

	LevelSettings settings(hasDetails ? seed : getEpochTimeS(), gameType);
	minecraft->selectLevel(id, name, settings);
	minecraft->hostMultiplayer();
	minecraft->setScreen(new ProgressScreen());
	hasChosen = true;
}

bool SimpleChooseLevelScreen::handleBackEvent(bool isDown) {
	if (!isDown)
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
	return true;
}
