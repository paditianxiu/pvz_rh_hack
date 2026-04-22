//////////////////////////////////////////////////////////////////////////////
//
//  Module Enumeration Functions (modules.cpp of detours.lib)
//
//  Microsoft Research Detours Package, Version 4.0.1
//
//  Copyright (c) Microsoft Corporation.  All rights reserved.
//
//  Module enumeration functions.
//

// #define DETOUR_DEBUG 1
#define DETOURS_INTERNAL
#include "detours.h"

#if DETOURS_VERSION != 0x4c0c1   // 0xMAJORcMINORcPATCH
#error detours.h version mismatch
#endif

#define CLR_DIRECTORY OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR]
#define IAT_DIRECTORY OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT]

//////////////////////////////////////////////////////////////////////////////
//
const GUID DETOUR_EXE_RESTORE_GUID = {0xbda26f34, 0xbc82, 0x4829, {0x9e, 0x64, 0x74, 0x2c, 0x4, 0xc8, 0x4f, 0xa0}};

auto WINAPI DetourFindFunction(_In_ LPCSTR pszModule, _In_ LPCSTR pszFunction) -> PVOID {
    if (pszFunction == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }

    /////////////////////////////////////////////// First, try GetProcAddress.
    //
    #pragma prefast(suppress:28752, "We don't do the unicode conversion for LoadLibraryExA.")
    HMODULE hModule = LoadLibraryExA(pszModule, nullptr, 0);
    if (hModule == nullptr) {
        return nullptr;
    }

    auto pbCode = (PBYTE)GetProcAddress(hModule, pszFunction);
    if (pbCode) {
        return pbCode;
    }

    ////////////////////////////////////////////////////// Then try ImageHelp.
    //
    DETOUR_TRACE(("DetourFindFunction(%hs, %hs)\n", pszModule, pszFunction));
    PDETOUR_SYM_INFO pSymInfo = DetourLoadImageHlp();
    if (pSymInfo == nullptr) {
        DETOUR_TRACE(("DetourLoadImageHlp failed: %lu\n", GetLastError()));
        return nullptr;
    }

    if (pSymInfo->pfSymLoadModule64(pSymInfo->hProcess, nullptr, (PCHAR)pszModule, nullptr, (DWORD64)hModule, 0) == 0) {
        if (ERROR_SUCCESS != GetLastError()) {
            DETOUR_TRACE(("SymLoadModule64(%p) failed: %lu\n", pSymInfo->hProcess, GetLastError()));
            return nullptr;
        }
    }

    HRESULT hrRet;
    CHAR szFullName[512];
    IMAGEHLP_MODULE64 modinfo;
    ZeroMemory(&modinfo, sizeof(modinfo));
    modinfo.SizeOfStruct = sizeof(modinfo);
    if (!pSymInfo->pfSymGetModuleInfo64(pSymInfo->hProcess, (DWORD64)hModule, &modinfo)) {
        DETOUR_TRACE(("SymGetModuleInfo64(%p, %p) failed: %lu\n", pSymInfo->hProcess, hModule, GetLastError()));
        return nullptr;
    }

    hrRet = StringCchCopyA(szFullName, sizeof(szFullName) / sizeof(CHAR), modinfo.ModuleName);
    if (FAILED(hrRet)) {
        DETOUR_TRACE(("StringCchCopyA failed: %08lx\n", hrRet));
        return nullptr;
    }
    hrRet = StringCchCatA(szFullName, sizeof(szFullName) / sizeof(CHAR), "!");
    if (FAILED(hrRet)) {
        DETOUR_TRACE(("StringCchCatA failed: %08lx\n", hrRet));
        return nullptr;
    }
    hrRet = StringCchCatA(szFullName, sizeof(szFullName) / sizeof(CHAR), pszFunction);
    if (FAILED(hrRet)) {
        DETOUR_TRACE(("StringCchCatA failed: %08lx\n", hrRet));
        return nullptr;
    }

    struct CFullSymbol : SYMBOL_INFO {
        CHAR szRestOfName[512];
    } symbol;
    ZeroMemory(&symbol, sizeof(symbol));
    //symbol.ModBase = (ULONG64)hModule;
    symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
    #ifdef DBHLPAPI
    symbol.MaxNameLen = sizeof(symbol.szRestOfName) / sizeof(symbol.szRestOfName[0]);
    #else
    symbol.MaxNameLength = sizeof(symbol.szRestOfName) / sizeof(symbol.szRestOfName[0]);
    #endif

    if (!pSymInfo->pfSymFromName(pSymInfo->hProcess, szFullName, &symbol)) {
        DETOUR_TRACE(("SymFromName(%hs) failed: %lu\n", szFullName, GetLastError()));
        return nullptr;
    }

    #if defined(DETOURS_IA64)
    // On the IA64, we get a raw code pointer from the symbol engine
    // and have to convert it to a wrapped [code pointer, global pointer].
    //
    PPLABEL_DESCRIPTOR pldEntry = (PPLABEL_DESCRIPTOR)DetourGetEntryPoint(hModule); PPLABEL_DESCRIPTOR pldSymbol = new PLABEL_DESCRIPTOR; pldSymbol->EntryPoint = symbol.Address; pldSymbol->
            GlobalPointer = pldEntry->GlobalPointer; return (PBYTE)pldSymbol;
    #elif defined(DETOURS_ARM)
    // On the ARM, we get a raw code pointer, which we must convert into a
    // valied Thumb2 function pointer.
    return DETOURS_PBYTE_TO_PFUNC(symbol.Address);
    #else
    return (PBYTE)symbol.Address;
    #endif
}

//////////////////////////////////////////////////////////////////////////////
//
auto DetourLoadImageHlp(VOID) -> PDETOUR_SYM_INFO {
    static DETOUR_SYM_INFO symInfo;
    static PDETOUR_SYM_INFO pSymInfo = nullptr;
    static BOOL failed = false;

    if (failed) {
        return nullptr;
    }
    if (pSymInfo != nullptr) {
        return pSymInfo;
    }

    ZeroMemory(&symInfo, sizeof(symInfo));
    // Create a real handle to the process.
    #if 0
    DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &symInfo.hProcess, 0, FALSE, DUPLICATE_SAME_ACCESS);
    #else
    symInfo.hProcess = GetCurrentProcess();
    #endif

    symInfo.hDbgHelp = LoadLibraryExW(L"dbghelp.dll", nullptr, 0);
    if (symInfo.hDbgHelp == nullptr) {
    abort:
        failed = true;
        if (symInfo.hDbgHelp != nullptr) {
            FreeLibrary(symInfo.hDbgHelp);
        }
        symInfo.pfImagehlpApiVersionEx = nullptr;
        symInfo.pfSymInitialize = nullptr;
        symInfo.pfSymSetOptions = nullptr;
        symInfo.pfSymGetOptions = nullptr;
        symInfo.pfSymLoadModule64 = nullptr;
        symInfo.pfSymGetModuleInfo64 = nullptr;
        symInfo.pfSymFromName = nullptr;
        return nullptr;
    }

    symInfo.pfImagehlpApiVersionEx = (PF_ImagehlpApiVersionEx)GetProcAddress(symInfo.hDbgHelp, "ImagehlpApiVersionEx");
    symInfo.pfSymInitialize = (PF_SymInitialize)GetProcAddress(symInfo.hDbgHelp, "SymInitialize");
    symInfo.pfSymSetOptions = (PF_SymSetOptions)GetProcAddress(symInfo.hDbgHelp, "SymSetOptions");
    symInfo.pfSymGetOptions = (PF_SymGetOptions)GetProcAddress(symInfo.hDbgHelp, "SymGetOptions");
    symInfo.pfSymLoadModule64 = (PF_SymLoadModule64)GetProcAddress(symInfo.hDbgHelp, "SymLoadModule64");
    symInfo.pfSymGetModuleInfo64 = (PF_SymGetModuleInfo64)GetProcAddress(symInfo.hDbgHelp, "SymGetModuleInfo64");
    symInfo.pfSymFromName = (PF_SymFromName)GetProcAddress(symInfo.hDbgHelp, "SymFromName");

    API_VERSION av;
    ZeroMemory(&av, sizeof(av));
    av.MajorVersion = API_VERSION_NUMBER;

    if (symInfo.pfImagehlpApiVersionEx == nullptr || symInfo.pfSymInitialize == nullptr || symInfo.pfSymLoadModule64 == nullptr || symInfo.pfSymGetModuleInfo64 == nullptr || symInfo.pfSymFromName ==
        nullptr) {
        goto abort;
    }

    symInfo.pfImagehlpApiVersionEx(&av);
    if (av.MajorVersion < API_VERSION_NUMBER) {
        goto abort;
    }

    if (!symInfo.pfSymInitialize(symInfo.hProcess, nullptr, FALSE)) {
        // We won't retry the initialize if it fails.
        goto abort;
    }

    if (symInfo.pfSymGetOptions != nullptr && symInfo.pfSymSetOptions != nullptr) {
        DWORD dw = symInfo.pfSymGetOptions();

        dw &= ~(SYMOPT_CASE_INSENSITIVE | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | 0);
        dw |= (
            #if defined(SYMOPT_EXACT_SYMBOLS)
            SYMOPT_EXACT_SYMBOLS |
            #endif
            #if defined(SYMOPT_NO_UNQUALIFIED_LOADS)
            SYMOPT_NO_UNQUALIFIED_LOADS |
            #endif
            SYMOPT_DEFERRED_LOADS |
            #if defined(SYMOPT_FAIL_CRITICAL_ERRORS)
            SYMOPT_FAIL_CRITICAL_ERRORS |
            #endif
            #if defined(SYMOPT_INCLUDE_32BIT_MODULES)
            SYMOPT_INCLUDE_32BIT_MODULES |
            #endif
            0);
        symInfo.pfSymSetOptions(dw);
    }

    pSymInfo = &symInfo;
    return pSymInfo;
}

//////////////////////////////////////////////////// Module Image Functions.
//

auto WINAPI DetourGetContainingModule(_In_ PVOID pvAddr) -> HMODULE {
    MEMORY_BASIC_INFORMATION mbi;
    ZeroMemory(&mbi, sizeof(mbi));

    __try {
        if (VirtualQuery(pvAddr, &mbi, sizeof(mbi)) <= 0) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            return nullptr;
        }

        // Skip uncommitted regions and guard pages.
        //
        if ((mbi.State != MEM_COMMIT) || ((mbi.Protect & 0xff) == PAGE_NOACCESS) || (mbi.Protect & PAGE_GUARD)) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            return nullptr;
        }

        auto pDosHeader = static_cast<PIMAGE_DOS_HEADER>(mbi.AllocationBase);
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            return nullptr;
        }

        auto pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader + pDosHeader->e_lfanew);
        if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
            SetLastError(ERROR_INVALID_EXE_SIGNATURE);
            return nullptr;
        }
        if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
            SetLastError(ERROR_EXE_MARKED_INVALID);
            return nullptr;
        }
        SetLastError(NO_ERROR);

        return (HMODULE)pDosHeader;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        SetLastError(ERROR_INVALID_EXE_SIGNATURE);
        return nullptr;
    }
}

auto WINAPI DetourEnumerateModules(_In_opt_ HMODULE hModuleLast) -> HMODULE {
    PBYTE pbLast = (PBYTE)hModuleLast + MM_ALLOCATION_GRANULARITY;

    MEMORY_BASIC_INFORMATION mbi;
    ZeroMemory(&mbi, sizeof(mbi));

    // Find the next memory region that contains a mapped PE image.
    //
    for (;; pbLast = static_cast<PBYTE>(mbi.BaseAddress) + mbi.RegionSize) {
        if (VirtualQuery(pbLast, &mbi, sizeof(mbi)) <= 0) {
            break;
        }

        // Skip uncommitted regions and guard pages.
        //
        if ((mbi.State != MEM_COMMIT) || ((mbi.Protect & 0xff) == PAGE_NOACCESS) || (mbi.Protect & PAGE_GUARD)) {
            continue;
        }

        __try {
            auto pDosHeader = (PIMAGE_DOS_HEADER)pbLast;
            if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE || static_cast<DWORD>(pDosHeader->e_lfanew) > mbi.RegionSize || static_cast<DWORD>(pDosHeader->e_lfanew) < sizeof(*pDosHeader)) {
                continue;
            }

            auto pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader + pDosHeader->e_lfanew);
            if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
                continue;
            }

            SetLastError(NO_ERROR);
            return (HMODULE)pDosHeader;
        }
        #pragma prefast(suppress:28940, "A bad pointer means this probably isn't a PE header.")
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
    }
    return nullptr;
}

auto WINAPI DetourGetEntryPoint(_In_opt_ HMODULE hModule) -> PVOID {
    auto pDosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (hModule == nullptr) {
        pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(nullptr);
    }

    __try {
        #pragma warning(suppress:6011) // GetModuleHandleW(NULL) never returns NULL.
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            return nullptr;
        }

        auto pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader + pDosHeader->e_lfanew);
        if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
            SetLastError(ERROR_INVALID_EXE_SIGNATURE);
            return nullptr;
        }
        if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
            SetLastError(ERROR_EXE_MARKED_INVALID);
            return nullptr;
        }

        PDETOUR_CLR_HEADER pClrHeader = nullptr;
        if (pNtHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            if (((PIMAGE_NT_HEADERS32)pNtHeader)->CLR_DIRECTORY.VirtualAddress != 0 && ((PIMAGE_NT_HEADERS32)pNtHeader)->CLR_DIRECTORY.Size != 0) {
                pClrHeader = (PDETOUR_CLR_HEADER)(((PBYTE)pDosHeader) + ((PIMAGE_NT_HEADERS32)pNtHeader)->CLR_DIRECTORY.VirtualAddress);
            }
        } else if (pNtHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            if (pNtHeader->CLR_DIRECTORY.VirtualAddress != 0 && pNtHeader->CLR_DIRECTORY.Size != 0) {
                pClrHeader = (PDETOUR_CLR_HEADER)(((PBYTE)pDosHeader) + pNtHeader->CLR_DIRECTORY.VirtualAddress);
            }
        }

        if (pClrHeader != nullptr) {
            // For MSIL assemblies, we want to use the _Cor entry points.

            HMODULE hClr = GetModuleHandleW(L"MSCOREE.DLL");
            if (hClr == nullptr) {
                return nullptr;
            }

            SetLastError(NO_ERROR);
            return (PVOID)GetProcAddress(hClr, "_CorExeMain");
        }

        SetLastError(NO_ERROR);

        // Pure resource DLLs have neither an entry point nor CLR information
        // so handle them by returning NULL (LastError is NO_ERROR)
        if (pNtHeader->OptionalHeader.AddressOfEntryPoint == 0) {
            return nullptr;
        }

        return ((PBYTE)pDosHeader) + pNtHeader->OptionalHeader.AddressOfEntryPoint;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        SetLastError(ERROR_EXE_MARKED_INVALID);
        return nullptr;
    }
}

auto WINAPI DetourGetModuleSize(_In_opt_ HMODULE hModule) -> ULONG {
    auto pDosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (hModule == nullptr) {
        pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(nullptr);
    }

    __try {
        #pragma warning(suppress:6011) // GetModuleHandleW(NULL) never returns NULL.
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            return NULL;
        }

        auto pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader + pDosHeader->e_lfanew);
        if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
            SetLastError(ERROR_INVALID_EXE_SIGNATURE);
            return NULL;
        }
        if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
            SetLastError(ERROR_EXE_MARKED_INVALID);
            return NULL;
        }
        SetLastError(NO_ERROR);

        return (pNtHeader->OptionalHeader.SizeOfImage);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        SetLastError(ERROR_EXE_MARKED_INVALID);
        return NULL;
    }
}

static inline auto RvaAdjust(_Pre_notnull_ PIMAGE_DOS_HEADER pDosHeader, _In_ DWORD raddr) -> PBYTE {
    if (raddr != NULL) {
        return ((PBYTE)pDosHeader) + raddr;
    }
    return nullptr;
}

auto WINAPI DetourEnumerateExports(_In_ HMODULE hModule, _In_opt_ PVOID pContext, _In_ PF_DETOUR_ENUMERATE_EXPORT_CALLBACK pfExport) -> BOOL {
    if (pfExport == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    auto pDosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (hModule == nullptr) {
        pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(nullptr);
    }

    __try {
        #pragma warning(suppress:6011) // GetModuleHandleW(NULL) never returns NULL.
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            return NULL;
        }

        auto pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader + pDosHeader->e_lfanew);
        if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
            SetLastError(ERROR_INVALID_EXE_SIGNATURE);
            return FALSE;
        }
        if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
            SetLastError(ERROR_EXE_MARKED_INVALID);
            return FALSE;
        }

        auto pExportDir = (PIMAGE_EXPORT_DIRECTORY)RvaAdjust(pDosHeader, pNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

        if (pExportDir == nullptr) {
            SetLastError(ERROR_EXE_MARKED_INVALID);
            return FALSE;
        }

        PBYTE pExportDirEnd = (PBYTE)pExportDir + pNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        auto pdwFunctions = (PDWORD)RvaAdjust(pDosHeader, pExportDir->AddressOfFunctions);
        auto pdwNames = (PDWORD)RvaAdjust(pDosHeader, pExportDir->AddressOfNames);
        auto pwOrdinals = (PWORD)RvaAdjust(pDosHeader, pExportDir->AddressOfNameOrdinals);

        for (DWORD nFunc = 0; nFunc < pExportDir->NumberOfFunctions; nFunc++) {
            PBYTE pbCode = (pdwFunctions != nullptr) ? RvaAdjust(pDosHeader, pdwFunctions[nFunc]) : nullptr;
            PCHAR pszName = nullptr;

            // if the pointer is in the export region, then it is a forwarder.
            if (pbCode > (PBYTE)pExportDir && pbCode < pExportDirEnd) {
                pbCode = nullptr;
            }

            for (DWORD n = 0; n < pExportDir->NumberOfNames; n++) {
                if (pwOrdinals[n] == nFunc) {
                    pszName = (pdwNames != nullptr) ? (PCHAR)RvaAdjust(pDosHeader, pdwNames[n]) : nullptr;
                    break;
                }
            }
            ULONG nOrdinal = pExportDir->Base + nFunc;

            if (!pfExport(pContext, nOrdinal, pszName, pbCode)) {
                break;
            }
        }
        SetLastError(NO_ERROR);
        return TRUE;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        SetLastError(ERROR_EXE_MARKED_INVALID);
        return NULL;
    }
}

auto WINAPI DetourEnumerateImportsEx(_In_opt_ HMODULE hModule,
                                     _In_opt_ PVOID pContext,
                                     _In_opt_ PF_DETOUR_IMPORT_FILE_CALLBACK pfImportFile,
                                     _In_opt_ PF_DETOUR_IMPORT_FUNC_CALLBACK_EX pfImportFunc) -> BOOL {
    auto pDosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (hModule == nullptr) {
        pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(nullptr);
    }

    __try {
        #pragma warning(suppress:6011) // GetModuleHandleW(NULL) never returns NULL.
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            return FALSE;
        }

        auto pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader + pDosHeader->e_lfanew);
        if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
            SetLastError(ERROR_INVALID_EXE_SIGNATURE);
            return FALSE;
        }
        if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
            SetLastError(ERROR_EXE_MARKED_INVALID);
            return FALSE;
        }

        auto iidp = (PIMAGE_IMPORT_DESCRIPTOR)RvaAdjust(pDosHeader, pNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

        if (iidp == nullptr) {
            SetLastError(ERROR_EXE_MARKED_INVALID);
            return FALSE;
        }

        for (; iidp->OriginalFirstThunk != 0; iidp++) {
            PCSTR pszName = (PCHAR)RvaAdjust(pDosHeader, iidp->Name);
            if (pszName == nullptr) {
                SetLastError(ERROR_EXE_MARKED_INVALID);
                return FALSE;
            }

            auto pThunks = (PIMAGE_THUNK_DATA)RvaAdjust(pDosHeader, iidp->OriginalFirstThunk);
            auto pAddrs = (PVOID*)RvaAdjust(pDosHeader, iidp->FirstThunk);

            HMODULE hFile = DetourGetContainingModule(pAddrs[0]);

            if (pfImportFile != nullptr) {
                if (!pfImportFile(pContext, hFile, pszName)) {
                    break;
                }
            }

            DWORD nNames = 0;
            if (pThunks) {
                for (; pThunks[nNames].u1.Ordinal; nNames++) {
                    DWORD nOrdinal = 0;
                    PCSTR pszFunc = nullptr;

                    if (IMAGE_SNAP_BY_ORDINAL(pThunks[nNames].u1.Ordinal)) {
                        nOrdinal = static_cast<DWORD>(IMAGE_ORDINAL(pThunks[nNames].u1.Ordinal));
                    } else {
                        pszFunc = (PCSTR)RvaAdjust(pDosHeader, static_cast<DWORD>(pThunks[nNames].u1.AddressOfData) + 2);
                    }

                    if (pfImportFunc != nullptr) {
                        if (!pfImportFunc(pContext, nOrdinal, pszFunc, &pAddrs[nNames])) {
                            break;
                        }
                    }
                }
                if (pfImportFunc != nullptr) {
                    pfImportFunc(pContext, 0, nullptr, nullptr);
                }
            }
        }
        if (pfImportFile != nullptr) {
            pfImportFile(pContext, nullptr, nullptr);
        }
        SetLastError(NO_ERROR);
        return TRUE;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        SetLastError(ERROR_EXE_MARKED_INVALID);
        return FALSE;
    }
}

// Context for DetourEnumerateImportsThunk, which adapts "regular" callbacks for use with "Ex".
struct _DETOUR_ENUMERATE_IMPORTS_THUNK_CONTEXT {
    PVOID pContext;
    PF_DETOUR_IMPORT_FILE_CALLBACK pfImportFile;
    PF_DETOUR_IMPORT_FUNC_CALLBACK pfImportFunc;
};

// Callback for DetourEnumerateImportsEx that adapts DetourEnumerateImportsEx
// for use with a DetourEnumerateImports callback -- derefence the IAT and pass the value on.

static auto CALLBACK DetourEnumerateImportsThunk(_In_ PVOID VoidContext, _In_ DWORD nOrdinal, _In_opt_ PCSTR pszFunc, _In_opt_ PVOID* ppvFunc) -> BOOL {
    const _DETOUR_ENUMERATE_IMPORTS_THUNK_CONTEXT* const pContext = static_cast<_DETOUR_ENUMERATE_IMPORTS_THUNK_CONTEXT*>(VoidContext);
    return pContext->pfImportFunc(pContext->pContext, nOrdinal, pszFunc, ppvFunc ? *ppvFunc : nullptr);
}

static auto CALLBACK DetourEnumerateImportsFile(_In_ PVOID VoidContext, _In_opt_ HMODULE hModule, _In_opt_ LPCSTR pszFile) -> BOOL {
    const _DETOUR_ENUMERATE_IMPORTS_THUNK_CONTEXT* const pContext = static_cast<_DETOUR_ENUMERATE_IMPORTS_THUNK_CONTEXT*>(VoidContext);
    return pContext->pfImportFile(pContext->pContext, hModule, pszFile);
}

auto WINAPI DetourEnumerateImports(_In_opt_ HMODULE hModule,
                                   _In_opt_ PVOID pContext,
                                   _In_opt_ PF_DETOUR_IMPORT_FILE_CALLBACK pfImportFile,
                                   _In_opt_ PF_DETOUR_IMPORT_FUNC_CALLBACK pfImportFunc) -> BOOL {
    if (pfImportFile == nullptr || pfImportFunc == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    const _DETOUR_ENUMERATE_IMPORTS_THUNK_CONTEXT context = {pContext, pfImportFile, pfImportFunc};
    return DetourEnumerateImportsEx(hModule, (PVOID)&context, &DetourEnumerateImportsFile, &DetourEnumerateImportsThunk);
}

static auto WINAPI GetPayloadSectionFromModule(HMODULE hModule) -> PDETOUR_LOADED_BINARY {
    auto pDosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (hModule == nullptr) {
        pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(nullptr);
    }

    __try {
        #pragma warning(suppress:6011) // GetModuleHandleW(NULL) never returns NULL.
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            return nullptr;
        }

        auto pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader + pDosHeader->e_lfanew);
        if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
            SetLastError(ERROR_INVALID_EXE_SIGNATURE);
            return nullptr;
        }
        if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
            SetLastError(ERROR_EXE_MARKED_INVALID);
            return nullptr;
        }

        auto pSectionHeaders = (PIMAGE_SECTION_HEADER)((PBYTE)pNtHeader + sizeof(pNtHeader->Signature) + sizeof(pNtHeader->FileHeader) + pNtHeader->FileHeader.SizeOfOptionalHeader);

        for (DWORD n = 0; n < pNtHeader->FileHeader.NumberOfSections; n++) {
            if (strcmp((PCHAR)pSectionHeaders[n].Name, ".detour") == 0) {
                if (pSectionHeaders[n].VirtualAddress == 0 || pSectionHeaders[n].SizeOfRawData == 0) {
                    break;
                }

                PBYTE pbData = (PBYTE)pDosHeader + pSectionHeaders[n].VirtualAddress;
                auto pHeader = (DETOUR_SECTION_HEADER*)pbData;
                if (pHeader->cbHeaderSize < sizeof(DETOUR_SECTION_HEADER) || pHeader->nSignature != DETOUR_SECTION_HEADER_SIGNATURE) {
                    break;
                }

                if (pHeader->nDataOffset == 0) {
                    pHeader->nDataOffset = pHeader->cbHeaderSize;
                }
                SetLastError(NO_ERROR);
                return (PBYTE)pHeader;
            }
        }
        SetLastError(ERROR_EXE_MARKED_INVALID);
        return nullptr;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        SetLastError(ERROR_EXE_MARKED_INVALID);
        return nullptr;
    }
}

_Writable_bytes_(*pcbData)_Readable_bytes_(*pcbData)_Success_(return != NULL)auto WINAPI DetourFindPayload(_In_opt_ HMODULE hModule, _In_ REFGUID rguid, _Out_opt_ DWORD* pcbData) -> PVOID {
    PBYTE pbData = nullptr;
    if (pcbData) {
        *pcbData = 0;
    }

    PDETOUR_LOADED_BINARY pBinary = GetPayloadSectionFromModule(hModule);
    if (pBinary == nullptr) {
        // Error set by GetPayloadSectionFromModule.
        return nullptr;
    }

    __try {
        auto pHeader = static_cast<DETOUR_SECTION_HEADER*>(pBinary);
        if (pHeader->cbHeaderSize < sizeof(DETOUR_SECTION_HEADER) || pHeader->nSignature != DETOUR_SECTION_HEADER_SIGNATURE) {
            SetLastError(ERROR_INVALID_EXE_SIGNATURE);
            return nullptr;
        }

        PBYTE pbBeg = ((PBYTE)pHeader) + pHeader->nDataOffset;
        PBYTE pbEnd = ((PBYTE)pHeader) + pHeader->cbDataSize;

        for (pbData = pbBeg; pbData < pbEnd;) {
            auto pSection = (DETOUR_SECTION_RECORD*)pbData;

            if (DetourAreSameGuid(pSection->guid, rguid)) {
                if (pcbData) {
                    *pcbData = pSection->cbBytes - sizeof(*pSection);
                }
                SetLastError(NO_ERROR);
                return (PBYTE)(pSection + 1);
            }

            pbData = (PBYTE)pSection + pSection->cbBytes;
        }
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
}

_Writable_bytes_(*pcbData)_Readable_bytes_(*pcbData)_Success_(return != NULL)auto WINAPI DetourFindPayloadEx(_In_ REFGUID rguid, _Out_opt_ DWORD* pcbData) -> PVOID {
    for (HMODULE hMod = nullptr; (hMod = DetourEnumerateModules(hMod)) != nullptr;) {
        PVOID pvData;

        pvData = DetourFindPayload(hMod, rguid, pcbData);
        if (pvData != nullptr) {
            return pvData;
        }
    }
    SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
}

auto WINAPI DetourGetSizeOfPayloads(_In_opt_ HMODULE hModule) -> DWORD {
    PDETOUR_LOADED_BINARY pBinary = GetPayloadSectionFromModule(hModule);
    if (pBinary == nullptr) {
        // Error set by GetPayloadSectionFromModule.
        return 0;
    }

    __try {
        auto pHeader = static_cast<DETOUR_SECTION_HEADER*>(pBinary);
        if (pHeader->cbHeaderSize < sizeof(DETOUR_SECTION_HEADER) || pHeader->nSignature != DETOUR_SECTION_HEADER_SIGNATURE) {
            SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
        SetLastError(NO_ERROR);
        return pHeader->cbDataSize;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
}

auto WINAPI DetourFreePayload(_In_ PVOID pvData) -> BOOL {
    BOOL fSucceeded = FALSE;

    // If you have any doubts about the following code, please refer to the comments in DetourCopyPayloadToProcess.
    HMODULE hModule = DetourGetContainingModule(pvData);
    DETOUR_ASSERT(hModule != NULL);
    if (hModule != nullptr) {
        fSucceeded = VirtualFree(hModule, 0, MEM_RELEASE);
        DETOUR_ASSERT(fSucceeded);
        if (fSucceeded) {
            hModule = nullptr;
        }
    }

    return fSucceeded;
}

auto WINAPI DetourRestoreAfterWith() -> BOOL {
    PVOID pvData;
    DWORD cbData;

    pvData = DetourFindPayloadEx(DETOUR_EXE_RESTORE_GUID, &cbData);

    if (pvData != nullptr && cbData != 0) {
        return DetourRestoreAfterWithEx(pvData, cbData);
    }
    SetLastError(ERROR_MOD_NOT_FOUND);
    return FALSE;
}

auto WINAPI DetourRestoreAfterWithEx(_In_reads_bytes_(cbData) PVOID pvData, _In_ DWORD cbData) -> BOOL {
    auto pder = static_cast<PDETOUR_EXE_RESTORE>(pvData);

    if (pder->cb != sizeof(*pder) || pder->cb > cbData) {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        return FALSE;
    }

    DWORD dwPermIdh = ~0u;
    DWORD dwPermInh = ~0u;
    DWORD dwPermClr = ~0u;
    DWORD dwIgnore;
    BOOL fSucceeded = FALSE;
    BOOL fUpdated32To64 = FALSE;

    if (pder->pclr != nullptr && pder->clr.Flags != ((PDETOUR_CLR_HEADER)pder->pclr)->Flags) {
        // If we had to promote the 32/64-bit agnostic IL to 64-bit, we can't restore
        // that.
        fUpdated32To64 = TRUE;
    }

    if (DetourVirtualProtectSameExecute(pder->pidh, pder->cbidh, PAGE_EXECUTE_READWRITE, &dwPermIdh)) {
        if (DetourVirtualProtectSameExecute(pder->pinh, pder->cbinh, PAGE_EXECUTE_READWRITE, &dwPermInh)) {
            CopyMemory(pder->pidh, &pder->idh, pder->cbidh);
            CopyMemory(pder->pinh, &pder->inh, pder->cbinh);

            if (pder->pclr != nullptr && !fUpdated32To64) {
                if (DetourVirtualProtectSameExecute(pder->pclr, pder->cbclr, PAGE_EXECUTE_READWRITE, &dwPermClr)) {
                    CopyMemory(pder->pclr, &pder->clr, pder->cbclr);
                    VirtualProtect(pder->pclr, pder->cbclr, dwPermClr, &dwIgnore);
                    fSucceeded = TRUE;
                }
            } else {
                fSucceeded = TRUE;
            }
            VirtualProtect(pder->pinh, pder->cbinh, dwPermInh, &dwIgnore);
        }
        VirtualProtect(pder->pidh, pder->cbidh, dwPermIdh, &dwIgnore);
    }
    // Delete the payload after successful recovery to prevent repeated restore
    if (fSucceeded) {
        DetourFreePayload(pder);
        pder = nullptr;
    }
    return fSucceeded;
}

//  End of File
