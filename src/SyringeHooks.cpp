#include <windows.h>

namespace
{
struct SyHookDecl
{
    unsigned address;
    unsigned size;
    const char* name;
    unsigned padding;
};

const SyHookDecl hooks[] __attribute__((section(".syhks00"), used, aligned(16))) =
{
    {0x55D377, 5, "SA_Time_Update", 0}, // Main loop, after the active-game check.
    {0x4F4589, 5, "SA_Time_Draw", 0},   // Render tail, before the composite is presented.
    {0x67E6E5, 7, "SA_Time_Reset", 0},  // Successful save load.
    {0x683E70, 5, "SA_Time_Reset", 0},  // New scenario, before ScenarioStarted is set.
};

struct SyringeHandshakeInfo
{
    int cbSize;
    int numHooks;
    unsigned checksum;
    DWORD exeFilesize;
    DWORD exeTimestamp;
    unsigned exeCRC;
    int messageCapacity;
    char* message;
};
}

extern "C" __declspec(dllexport) HRESULT __cdecl SyringeHandshake(SyringeHandshakeInfo* info)
{
    if (!info || info->cbSize < static_cast<int>(sizeof(SyringeHandshakeInfo))) return E_FAIL;

    const bool supported = info->exeFilesize == 4816864 && info->exeTimestamp == 0x3BDF544E &&
        info->exeCRC == 0x1B499086;
    if (info->message && info->messageCapacity > 0)
        lstrcpynA(info->message, supported ? "SA_Time 1.0.2" :
            "SA_Time requires the Mental Omega 3.3.6 game executable", info->messageCapacity);
    return supported ? S_OK : S_FALSE;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
