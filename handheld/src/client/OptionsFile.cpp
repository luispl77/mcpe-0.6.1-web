#include "OptionsFile.h"
#include <stdio.h>
#include <string.h>

OptionsFile::OptionsFile() {
	// Only a fallback: Minecraft::reloadOptions redirects this to the writable
	// storage directory before anything reads or writes it.
#if defined(__APPLE__)	// iOS, in this codebase
	settingsPath = "./Documents/options.txt";
#else
	settingsPath = "options.txt";
#endif
}

void OptionsFile::setDirectory(const std::string& dir) {
	if (dir.empty()) return;
	settingsPath = dir + "/options.txt";
}

void OptionsFile::save(const StringVector& settings) {
	FILE* pFile = fopen(settingsPath.c_str(), "w");
	if(pFile != NULL) {
		for(StringVector::const_iterator it = settings.begin(); it != settings.end(); ++it) {
			fprintf(pFile, "%s\n", it->c_str());
		}
		fclose(pFile);
	}
}

StringVector OptionsFile::getOptionStrings() {
	StringVector returnVector;
	// This used to open with "w", which truncates the file and yields a stream
	// that cannot be read -- so loading options both returned nothing and
	// destroyed whatever had been saved. Nothing has ever persisted.
	FILE* pFile = fopen(settingsPath.c_str(), "r");
	if(pFile != NULL) {
		char lineBuff[256];
		while(fgets(lineBuff, sizeof lineBuff, pFile)) {
			// save() writes one "key:value" line per option, but Options::update
			// walks the vector two entries at a time expecting key then value.
			// Split here so the two halves agree.
			char* sep = strchr(lineBuff, ':');
			if (sep == NULL) continue;
			*sep = '\0';

			char* value = sep + 1;
			size_t len = strlen(value);
			while (len > 0 && (value[len-1] == '\n' || value[len-1] == '\r'))
				value[--len] = '\0';

			returnVector.push_back(std::string(lineBuff));
			returnVector.push_back(std::string(value));
		}
		fclose(pFile);
	}
	return returnVector;
}
