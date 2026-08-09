#include "MoveFolder.h"

// The iOS Xcode project supplied this via a prefix header; the macOS CMake
// build compiles this file on its own, so import it explicitly.
#import <Foundation/Foundation.h>

void moveFolder(const std::string& src, const std::string& dst) {
    NSError* error = NULL;
    [[NSFileManager defaultManager]
        moveItemAtPath:[NSString stringWithUTF8String:src.c_str()]
        toPath:[NSString stringWithUTF8String:dst.c_str()] error:&error];

    if (error)
        NSLog(@"Error: %@\n: %@\n", [error description], [error debugDescription]);
}
