#include "version.h"

#if defined(_WIN32)
#include <Windows.h>
#include <versionhelpers.h>
#endif

bool IsWindows11()
{
#if defined(_WIN32)
    if (!IsWindows10OrGreater())
        return false;

    OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0 };
   
    const DWORDLONG dwlConditionMask = 0;
    VerSetConditionMask(dwlConditionMask, VER_MAJORVERSION, VER_EQUAL);
    VerSetConditionMask(dwlConditionMask, VER_MINORVERSION, VER_EQUAL);
    VerSetConditionMask(dwlConditionMask, VER_BUILDNUMBER, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = 10;
    osvi.dwMinorVersion = 0;
    osvi.dwBuildNumber = 22000;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, dwlConditionMask) != 0;
#else
    return false;
#endif
}
