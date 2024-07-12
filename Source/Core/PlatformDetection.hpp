#pragma once

// GCC predefined compiler macros

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

#define NOMINMAX
#include <Windows.h>
#define RDNT_WINDOWS

#elif defined(__APPLE__)

#include "TargetConditionals.h"
#define RDNT_MACOS

#if TARGET_IPHONE_SIMULATOR
// iOS, tvOS, or watchOS Simulator
#elif TARGET_OS_MACCATALYST
// Mac's Catalyst (ports iOS API into Mac, like UIKit).
#elif TARGET_OS_IPHONE
// iOS, tvOS, or watchOS device
#elif TARGET_OS_MAC
// Other kinds of Apple platforms
#else

#error "Unknown Apple platform"

#endif

#elif defined(__ANDOIRD__)

#error "Android not supported!"

#elif defined(__linux__)

#define RDNT_LINUX

#else

#error "Unsupported platform!"

#endif
