#pragma once

#if defined(_MSC_VER)
#define TURBOSTROI_EXPORT __declspec(dllexport)
#define TURBOSTROI_IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
#define TURBOSTROI_EXPORT __attribute__((visibility("default")))
#define TURBOSTROI_IMPORT
#else
#define TURBOSTROI_EXPORT
#define TURBOSTROI_IMPORT
#pragma warning Unknown dynamic link import/export semantics.
#endif

#define TURBOSTROI_VERSION_STR "2.10.0"
#define TURBOSTROI_VERSION_MAJOR 2
#define TURBOSTROI_VERSION_MINOR 10
#define TURBOSTROI_VERSION_PATCH 0
#define TURBOSTROI_VERSION (((TURBOSTROI_VERSION_MAJOR & 0xF)<<16)|((TURBOSTROI_VERSION_MINOR & 0xF) <<8)|(TURBOSTROI_VERSION_PATCH & 0xF))
#define TURBOSTROI_VERSION_PRINT "v" TURBOSTROI_VERSION_STR

bool IsWindows11();