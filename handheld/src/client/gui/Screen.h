#ifndef NET_MINECRAFT_CLIENT_GUI__Screen_H__
#define NET_MINECRAFT_CLIENT_GUI__Screen_H__

//package net.minecraft.client.gui;

#include <vector>
#include "GuiComponent.h"

class Font;
class Minecraft;
class Button;
class TextBox;
struct IntRectangle;

class Screen: public GuiComponent
{
public:
	Screen();

    virtual void render(int xm, int ym, float a);

    void init(Minecraft* minecraft, int width, int height);
	virtual void init();

    void setSize(int width, int height);
	virtual void setupPositions() {};

	virtual void updateEvents();
    virtual void mouseEvent();
    virtual void keyboardEvent();
	virtual void keyboardTextEvent();
	virtual bool handleBackEvent(bool isDown);

    virtual void tick() {}

    virtual void removed() {}

    virtual void renderBackground();
    virtual void renderBackground(int vo);
    virtual void renderDirtBackground(int vo);
	// query
	virtual bool renderGameBehind();
	virtual bool hasClippingArea(IntRectangle& out);

    virtual bool isPauseScreen();
	virtual bool isErrorScreen();
	virtual bool isInGameScreen();
    virtual bool closeOnPlayerHurt();

    virtual void confirmResult(bool result, int id) {}
	virtual void lostFocus();
	virtual void toGUICoordinate(int& x, int& y);

	/** Whether a tap at these GUI coordinates would land in an editable field.

	    Asked by the page, from inside the real touch event, because a mobile
	    browser only raises its keyboard for a focus() that happens during a user
	    gesture -- and by the time the game has seen the tap, drained it from the
	    SDL queue on the next frame and focused a field, that gesture is over. So
	    the page asks first and focuses its off-screen input itself. */
	bool textBoxAt(int x, int y);
protected:
	void updateTabButtonSelection();

	virtual void buttonClicked(Button* button) {}
	virtual void mouseClicked(int x, int y, int buttonNum);
	virtual void mouseReleased(int x, int y, int buttonNum);

	virtual void keyPressed(int eventKey);
	virtual void keyboardNewChar(char inputChar);

	/** The field a keystroke belongs to, or NULL when none is focused.

	    Screens with no textBoxes -- which is every screen the game shipped --
	    get NULL and behave exactly as they did. */
	TextBox* focusedTextBox();
public:
	int width;
	int height;
	bool passEvents;
	//GuiParticles* particles;
protected:
	Minecraft* minecraft;
	std::vector<Button*> buttons;
	std::vector<TextBox*> textBoxes;

	std::vector<Button*> tabButtons;
	int tabButtonIndex;

	Font* font;
private:
	Button* clickedButton;
};

#endif /*NET_MINECRAFT_CLIENT_GUI__Screen_H__*/
