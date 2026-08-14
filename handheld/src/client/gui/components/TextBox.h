#ifndef NET_MINECRAFT_CLIENT_GUI_COMPONENTS__TextBox_H__
#define NET_MINECRAFT_CLIENT_GUI_COMPONENTS__TextBox_H__

//package net.minecraft.client.gui;

#include <string>
#include "../GuiComponent.h"
#include "../../Options.h"

class Font;
class Minecraft;

/** An editable field, drawn by the game rather than by the platform.

    0.6.1 shipped this class as a stub and then never used it: every piece of
    text the game collects -- chat, rename world, create world -- goes out to
    AppPlatform::createUserInput() instead, which was a native dialog on the
    devices of 2013 and is an overlay in the page here. That is fine for one
    line of chat and wrong for a screen that is asking four questions, so this
    is finished off and the screens that need a form use it.

    Characters arrive through Screen::keyboardNewChar, which is already fed from
    SDL_TEXTINPUT, so a desktop browser needs nothing else. A phone does: no
    soft keyboard appears unless something in the page holds focus, which is
    what AppPlatform::showKeyboard() is for on this target. */
class TextBox: public GuiComponent
{
public:
	TextBox(int id, const std::string& msg);
    TextBox(int id, int x, int y, const std::string& msg);
    TextBox(int id, int x, int y, int w, int h, const std::string& msg);

	virtual void setFocus(Minecraft* minecraft);
	virtual bool loseFocus(Minecraft* minecraft);

    virtual void render(Minecraft* minecraft, int xm, int ym);

	bool isInside(int mx, int my) const;

	/// One typed character. Anything unprintable is dropped here rather than at
	/// the call site, so every path into a field agrees about what a field holds.
	void charTyped(char inputChar);
	/// The editing keys a text event does not carry -- backspace, so far.
	void keyPressed(int eventKey);

public:
	int w, h;
	int x, y;

	std::string text;
	int id;
	bool focused;

	/// Drawn in grey while the field is empty and unfocused: "Server name".
	std::string placeholder;

	/// Rendered as bullets. Nothing in 2013 asked the game for a password; a
	/// world you want to keep yours does.
	bool password;

	unsigned int maxLength;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_COMPONENTS__TextBox_H__*/
