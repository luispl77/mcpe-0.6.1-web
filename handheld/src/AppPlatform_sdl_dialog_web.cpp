#include "AppPlatform_sdl.h"
#include "client/gui/screens/DialogDefinitions.h"

#include <emscripten.h>
#include <cstdlib>
#include <string>

/** Web counterpart to AppPlatform_sdl_dialog_cocoa.mm.

    window.prompt/confirm are synchronous and block the whole JS thread, which
    is exactly the modal behaviour the caller expects: showDialog() must have
    latched an answer by the time it returns, because SelectWorldScreen::tick()
    polls getUserInputStatus() on the very next frame.

    That does mean we can't build a single multi-field dialog the way NSAlert
    can, so the create-world flow is three prompts in a row. Cancelling any of
    them cancels the whole thing. */

/** Returns null if the user dismissed the prompt, otherwise a malloc'd UTF-8
    string that the caller owns. */
EM_JS(char*, mcpe_prompt, (const char* message, const char* initial), {
	var answer = window.prompt(UTF8ToString(message), UTF8ToString(initial));
	if (answer === null) return 0;
	var len = lengthBytesUTF8(answer) + 1;
	var buf = _malloc(len);
	stringToUTF8(answer, buf, len);
	return buf;
});

/// Wraps mcpe_prompt so the malloc'd result can't leak past the copy.
static bool promptString(const char* message, const char* initial, std::string& out)
{
	char* raw = mcpe_prompt(message, initial);
	if (!raw)
		return false;

	out = raw;
	free(raw);
	return true;
}

void AppPlatform_sdl::showDialog(int dialogId)
{
	_userInput.clear();
	_userInputStatus = USERINPUT_NOTINITED;

	switch (dialogId) {
	case DialogDefinitions::DIALOG_CREATE_NEW_WORLD: {
		std::string name, seed;

		if (!promptString("World name", "World", name)) {
			_userInputStatus = USERINPUT_CANCEL;
			return;
		}
		if (!promptString("Seed (leave blank for random)", "", seed)) {
			_userInputStatus = USERINPUT_CANCEL;
			return;
		}

		// No game mode here. Every other platform's dialog is a native sheet
		// that can hold a picker alongside the text fields; a browser's is one
		// question at a time, and asking "Creative mode? OK / Cancel" is a worse
		// version of a screen the game already has. Touch::SelectWorldScreen
		// hands off to SimpleChooseLevelScreen for it instead.
		//
		// SelectWorldScreen::tick() reads these back positionally, so the order
		// and count here are load bearing.
		_userInput.push_back(name);
		_userInput.push_back(seed);
		break;
	}

	case DialogDefinitions::DIALOG_RENAME_MP_WORLD:
	case DialogDefinitions::DIALOG_NEW_CHAT_MESSAGE: {
		const bool isChat = (dialogId == DialogDefinitions::DIALOG_NEW_CHAT_MESSAGE);
		std::string text;

		if (!promptString(isChat ? "Chat message" : "Rename world", "", text)) {
			_userInputStatus = USERINPUT_CANCEL;
			return;
		}

		_userInput.push_back(text);
		break;
	}

	default:
		// Nothing to collect for informational dialogs.
		_userInputStatus = USERINPUT_CANCEL;
		return;
	}

	_userInputStatus = USERINPUT_OK;
}

int AppPlatform_sdl::getUserInputStatus()
{
	// Consume-once: the screens poll this every tick and only reset their own
	// state when it reads back as something other than "not inited".
	const int status = _userInputStatus;
	_userInputStatus = USERINPUT_NOTINITED;
	return status;
}

StringVector AppPlatform_sdl::getUserInput()
{
	return _userInput;
}
