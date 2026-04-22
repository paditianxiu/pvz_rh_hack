#include "pch.h"

#include "src/Bootstrap.hpp"

extern "C" __declspec(dllexport) BOOL __stdcall RequestFastUnload() {
    return bootstrap::RequestFastUnload() ? TRUE : FALSE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        if (GetModuleHandleW(L"PlantsVsZombiesRH.exe")) {
            DisableThreadLibraryCalls(hModule);
            bootstrap::SetModuleHandle(hModule);
            bootstrap::Start();
        }
        break;

    case DLL_PROCESS_DETACH:
        bootstrap::NotifyProcessDetach(lpReserved != nullptr);
        break;
    }

    return TRUE;
}
