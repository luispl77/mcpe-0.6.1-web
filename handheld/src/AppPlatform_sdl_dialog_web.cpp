#include "AppPlatform_sdl.h"
#include "client/gui/screens/DialogDefinitions.h"

#include <emscripten.h>
#include <cstdlib>
#include <string>

/** Text entry for the web, as an overlay in the page.

    This was window.prompt, which is synchronous: it blocks the whole JS thread,
    so showDialog() had an answer latched before it returned and this file never
    had to think about time. Convenient, and not worth what it cost. A prompt
    cannot hold two fields, so creating a world was a queue of popups with a
    Cancel on each; it cannot be styled, so it arrived in the middle of the game
    looking like nothing else on the page; and Chrome drops fullscreen for the
    duration of any modal dialog, which on a phone means being thrown out of the
    game to name a world.

    So the answer now comes back whenever the player gets round to it. Nothing on
    the game's side had to change for that, which is worth saying plainly because
    it looks like it should have: the screens already call getUserInputStatus()
    every tick and test it with `status > -1`, and USERINPUT_NOTINITED is -2. Not
    having answered yet was always a state they sat in quite happily -- it simply
    never lasted longer than one frame before. */

/// Opens the overlay. A field whose label is empty is not shown.
EM_JS(void, mcpe_dialog_open, (const char* title, const char* okLabel,
                               const char* label0, const char* value0,
                               const char* label1, const char* value1), {
	if (!window.mcpeDialog) return;
	window.mcpeDialog.open({
		title:   UTF8ToString(title),
		okLabel: UTF8ToString(okLabel),
		fields: [
			{ label: UTF8ToString(label0), value: UTF8ToString(value0) },
			{ label: UTF8ToString(label1), value: UTF8ToString(value1) }
		].filter(function (f) { return f.label !== ''; })
	});
});

/** USERINPUT_NOTINITED while the overlay is still up, then CANCEL or OK.

    Cancels rather than hangs if the page has no dialog in it: a build whose
    shell.html is out of step should drop the player back on the world list, not
    leave a screen polling something that will never answer. */
EM_JS(int, mcpe_dialog_status, (), {
	return window.mcpeDialog ? window.mcpeDialog.status() : 0;
});

/// Field i of the answer, as a malloc'd UTF-8 string the caller owns.
EM_JS(char*, mcpe_dialog_value, (int index), {
	var text = window.mcpeDialog ? window.mcpeDialog.value(index) : '';
	var len = lengthBytesUTF8(text) + 1;
	var buf = _malloc(len);
	stringToUTF8(text, buf, len);
	return buf;
});

/// Wraps mcpe_dialog_value so the malloc'd result can't leak past the copy.
static std::string takeValue(int index)
{
	char* raw = mcpe_dialog_value(index);
	if (!raw)
		return std::string();

	std::string out = raw;
	free(raw);
	return out;
}

void AppPlatform_sdl::showDialog(int dialogId)
{
	_userInput.clear();
	_userInputStatus = USERINPUT_NOTINITED;
	_dialogFields = 0;

	switch (dialogId) {
	case DialogDefinitions::DIALOG_CREATE_NEW_WORLD:
		// Name and seed only. The game mode is not asked for here: 0.6.1 has a
		// screen of its own for it, and Touch::SelectWorldScreen hands off to
		// SimpleChooseLevelScreen once this comes back.
		_dialogFields = 2;
		mcpe_dialog_open("Create world", "Create",
		                 "World name", "World",
		                 "Seed (blank for random)", "");
		break;

	case DialogDefinitions::DIALOG_RENAME_MP_WORLD:
		_dialogFields = 1;
		mcpe_dialog_open("Rename world", "Rename", "New name", "", "", "");
		break;

	case DialogDefinitions::DIALOG_NEW_CHAT_MESSAGE:
		_dialogFields = 1;
		mcpe_dialog_open("Chat", "Send", "Message", "", "", "");
		break;

	default:
		// Nothing to collect for informational dialogs.
		_userInputStatus = USERINPUT_CANCEL;
		return;
	}
}

int AppPlatform_sdl::getUserInputStatus()
{
	// With a dialog up, this is where its answer is noticed -- the screens poll
	// here and nothing else runs JS on their behalf. Reporting the status also
	// consumes it, which is what the callers expect: they only reset their own
	// state when this reads back as something other than "not inited".
	if (_dialogFields > 0) {
		const int status = mcpe_dialog_status();
		if (status == USERINPUT_NOTINITED)
			return USERINPUT_NOTINITED;

		if (status == USERINPUT_OK) {
			for (int i = 0; i < _dialogFields; ++i)
				_userInput.push_back(takeValue(i));
		}

		_dialogFields = 0;
		return status;
	}

	const int status = _userInputStatus;
	_userInputStatus = USERINPUT_NOTINITED;
	return status;
}

StringVector AppPlatform_sdl::getUserInput()
{
	return _userInput;
}
