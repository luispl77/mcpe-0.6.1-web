#include "TextBox.h"
#include "../Font.h"
#include "../../Minecraft.h"
#include "../../../AppPlatform.h"
#include "../../../platform/time.h"
#include "../../../platform/input/Keyboard.h"

/* 200x24 is what a Button gets from its own default constructor. A field and a
 * button on the same screen that disagreed about how tall a row is would be the
 * first thing anybody noticed. */
TextBox::TextBox( int id, const std::string& msg )
 : w(200), h(24), x(0), y(0), text(msg), id(id), focused(false),
   password(false), maxLength(32) {
}

TextBox::TextBox( int id, int x, int y, const std::string& msg )
 : w(200), h(24), x(x), y(y), text(msg), id(id), focused(false),
   password(false), maxLength(32) {
}

TextBox::TextBox( int id, int x, int y, int w, int h, const std::string& msg )
 : w(w), h(h), x(x), y(y), text(msg), id(id), focused(false),
   password(false), maxLength(32) {
}

void TextBox::setFocus(Minecraft* minecraft) {
	if(!focused) {
		minecraft->platform()->showKeyboard();
		focused = true;
	}
}

bool TextBox::loseFocus(Minecraft* minecraft) {
	if(focused) {
		// Was showKeyboard(), which is how you can tell nothing ever called it:
		// giving up a field is the one moment the keyboard should go away.
		minecraft->platform()->hideKeyboard();
		focused = false;
		return true;
	}
	return false;
}

bool TextBox::isInside( int mx, int my ) const {
	return mx >= x && my >= y && mx < x + w && my < y + h;
}

void TextBox::charTyped( char inputChar ) {
	if (!focused) return;
	const unsigned char c = (unsigned char)inputChar;
	if (c < 32 || c == 127) return;
	if (text.length() >= maxLength) return;
	text += inputChar;
}

void TextBox::keyPressed( int eventKey ) {
	if (!focused) return;
	if (eventKey == Keyboard::KEY_BACKSPACE && !text.empty())
		text.erase(text.length() - 1);
}

void TextBox::render( Minecraft* minecraft, int xm, int ym ) {
	/* There is no field anywhere in 0.6.1 to copy the look from, so this is the
	 * desktop Minecraft field of the same era: a one-pixel border around a black
	 * well. Brighter while focused, because a form with four of these has to say
	 * which one the next keystroke lands in. */
	fill(x - 1, y - 1, x + w + 1, y + h + 1, focused ? 0xffffffff : 0xffa0a0a0);
	fill(x, y, x + w, y + h, 0xff000000);

	Font* font = minecraft->font;
	const int ty = y + (h - 8) / 2;

	if (text.empty() && !focused && !placeholder.empty()) {
		drawString(font, placeholder, x + 4, ty, 0xff707070);
		return;
	}

	const std::string shownFull = password ? std::string(text.length(), '*') : text;

	/* A value longer than the well scrolls instead of running out of it, and it
	 * is the tail that stays: that is where the cursor is and where the next
	 * character will appear. */
	std::string shown = shownFull;
	const int room = w - 8;
	while (shown.length() > 1 && font->width(shown) > room)
		shown.erase(0, 1);

	drawString(font, shown, x + 4, ty, 0xffe0e0e0);

	// Blinks off the wall clock rather than a tick count, the same way the
	// spinner on the join screen does -- nothing has to remember to tick a field.
	if (focused && ((int)(getTimeS() * 2)) % 2)
		drawString(font, "_", x + 4 + font->width(shown), ty, 0xffe0e0e0);
}
