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

#define TURBOSTROI_VERSION "v2.7.1"

bool IsWindows11();