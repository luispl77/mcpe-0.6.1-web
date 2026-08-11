#include "MoveFolder.h"
#include "../../../platform/log.h"

#include <cstdio>
#include <cerrno>
#include <cstring>

/** Portable counterpart to MoveFolder.mm, used by the web build.

    The only caller is ExternalFileLevelStorageSource::renameLevel(), where both
    paths are always siblings inside the same save directory -- so a plain
    rename() covers it, and the cross-filesystem case NSFileManager handles
    (copy then delete) can't arise. */
void moveFolder(const std::string& src, const std::string& dst)
{
	if (rename(src.c_str(), dst.c_str()) != 0)
		LOGE("Couldn't rename %s -> %s: %s\n", src.c_str(), dst.c_str(), strerror(errno));
}
