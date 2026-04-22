/////////////////////////////////////////////////////////////////////////////
//
//  Core Detours Functionality (detours.h of detours.lib)
//
//  Microsoft Research Detours Package, Version 4.0.1
//
//  Copyright (c) Microsoft Corporation.  All rights reserved.
//

#pragma once
#ifndef _DETOURS_H_
#define _DETOURS_H_

#define DETOURS_VERSION     0x4c0c1   // 0xMAJORcMINORcPATCH

//////////////////////////////////////////////////////////////////////////////
//

#ifdef DETOURS_INTERNAL

#define _CRT_STDIO_ARBITRARY_WIDE_SPECIFIERS 1
#define _ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE 1

#pragma warning(disable:4068) // unknown pragma (suppress)

#if _MSC_VER >= 1900
#pragma warning(push)
#pragma warning(disable:4091) // empty typedef
#endif

// Suppress declspec(dllimport) for the sake of Detours
// users that provide kernel32 functionality themselves.
// This is ok in the mainstream case, it will just cost
// an extra instruction calling some functions, which
// LTCG optimizes away.
//
#define _KERNEL32_ 1
#define _USER32_ 1

#include <windows.h>
#if (_MSC_VER < 1310)
#else
#pragma warning(push)
#if _MSC_VER > 1400
#pragma warning(disable:6102 6103) // /analyze warnings
#endif
#include <intsafe.h>
#include <strsafe.h>
#pragma warning(pop)
#endif
#include <crtdbg.h>

// Allow Detours to cleanly compile with the MingW toolchain.
//
#ifdef __GNUC__
#define __try
#define __except(x) if (0)
#include <strsafe.h>
#include <intsafe.h>
#endif

// From winerror.h, as this error isn't found in some SDKs:
//
// MessageId: ERROR_DYNAMIC_CODE_BLOCKED
//
// MessageText:
//
// The operation was blocked as the process prohibits dynamic code generation.
//
#define ERROR_DYNAMIC_CODE_BLOCKED       1655L

#endif // DETOURS_INTERNAL

//////////////////////////////////////////////////////////////////////////////
//

#undef DETOURS_X64
#undef DETOURS_X86
#undef DETOURS_IA64
#undef DETOURS_ARM
#undef DETOURS_ARM64
#undef DETOURS_BITS
#undef DETOURS_32BIT
#undef DETOURS_64BIT

#ifndef DECLSPEC_HYBRID_PATCHABLE
#define DECLSPEC_HYBRID_PATCHABLE DECLSPEC_CHPE_PATCHABLE
#endif

#if defined(_X86_)
#define DETOURS_X86
#define DETOURS_OPTION_BITS 64

#elif defined(_AMD64_) || defined(_ARM64EC_)
#define DETOURS_X64
#define DETOURS_OPTION_BITS 32

#elif defined(_IA64_)
#define DETOURS_IA64
#define DETOURS_OPTION_BITS 32

#elif defined(_ARM_)
#define DETOURS_ARM

#elif defined(_ARM64_)
#define DETOURS_ARM64

#else
#error Unknown architecture (x86, amd64, ia64, arm, arm64, arm64ec)
#endif

#ifdef _WIN64
#undef DETOURS_32BIT
#define DETOURS_64BIT 1
#define DETOURS_BITS 64
// If all 64bit kernels can run one and only one 32bit architecture.
//#define DETOURS_OPTION_BITS 32
#else
#define DETOURS_32BIT 1
#undef DETOURS_64BIT
#define DETOURS_BITS 32
// If all 64bit kernels can run one and only one 32bit architecture.
//#define DETOURS_OPTION_BITS 32
#endif

/////////////////////////////////////////////////////////////// Helper Macros.
//
#define DETOURS_STRINGIFY_(x)    #x
#define DETOURS_STRINGIFY(x)    DETOURS_STRINGIFY_(x)

#define VER_DETOURS_BITS    DETOURS_STRINGIFY(DETOURS_BITS)

//////////////////////////////////////////////////////////////////////////////
//

#if (_MSC_VER < 1299) && !defined(__MINGW32__)
typedef LONG LONG_PTR;typedef ULONG ULONG_PTR;
#endif

///////////////////////////////////////////////// SAL 2.0 Annotations w/o SAL.
//
//  These definitions are include so that Detours will build even if the
//  compiler doesn't have full SAL 2.0 support.
//
#ifndef DETOURS_DONT_REMOVE_SAL_20

#ifdef DETOURS_TEST_REMOVE_SAL_20
#undef _Analysis_assume_
#undef _Benign_race_begin_
#undef _Benign_race_end_
#undef _Field_range_
#undef _Field_size_
#undef _In_
#undef _In_bytecount_
#undef _In_count_
#undef __in_ecount
#undef _In_opt_
#undef _In_opt_bytecount_
#undef _In_opt_count_
#undef _In_opt_z_
#undef _In_range_
#undef _In_reads_
#undef _In_reads_bytes_
#undef _In_reads_opt_
#undef _In_reads_opt_bytes_
#undef _In_reads_or_z_
#undef _In_z_
#undef _Inout_
#undef _Inout_opt_
#undef _Inout_z_count_
#undef _Out_
#undef _Out_opt_
#undef _Out_writes_
#undef _Outptr_result_maybenull_
#undef _Readable_bytes_
#undef _Success_
#undef _Writable_bytes_
#undef _Pre_notnull_
#endif

#if defined(_Deref_out_opt_z_) && !defined(_Outptr_result_maybenull_)
#define _Outptr_result_maybenull_ _Deref_out_opt_z_
#endif

#if defined(_In_count_) && !defined(_In_reads_)
#define _In_reads_(x) _In_count_(x)
#endif

#if defined(_In_opt_count_) && !defined(_In_reads_opt_)
#define _In_reads_opt_(x) _In_opt_count_(x)
#endif

#if defined(_In_opt_bytecount_) && !defined(_In_reads_opt_bytes_)
#define _In_reads_opt_bytes_(x) _In_opt_bytecount_(x)
#endif

#if defined(_In_bytecount_) && !defined(_In_reads_bytes_)
#define _In_reads_bytes_(x) _In_bytecount_(x)
#endif

#ifndef _In_
#define _In_
#endif

#ifndef _In_bytecount_
#define _In_bytecount_(x)
#endif

#ifndef _In_count_
#define _In_count_(x)
#endif

#ifndef __in_ecount
#define __in_ecount(x)
#endif

#ifndef _In_opt_
#define _In_opt_
#endif

#ifndef _In_opt_bytecount_
#define _In_opt_bytecount_(x)
#endif

#ifndef _In_opt_count_
#define _In_opt_count_(x)
#endif

#ifndef _In_opt_z_
#define _In_opt_z_
#endif

#ifndef _In_range_
#define _In_range_(x,y)
#endif

#ifndef _In_reads_
#define _In_reads_(x)
#endif

#ifndef _In_reads_bytes_
#define _In_reads_bytes_(x)
#endif

#ifndef _In_reads_opt_
#define _In_reads_opt_(x)
#endif

#ifndef _In_reads_opt_bytes_
#define _In_reads_opt_bytes_(x)
#endif

#ifndef _In_reads_or_z_
#define _In_reads_or_z_
#endif

#ifndef _In_z_
#define _In_z_
#endif

#ifndef _Inout_
#define _Inout_
#endif

#ifndef _Inout_opt_
#define _Inout_opt_
#endif

#ifndef _Inout_z_count_
#define _Inout_z_count_(x)
#endif

#ifndef _Out_
#define _Out_
#endif

#ifndef _Out_opt_
#define _Out_opt_
#endif

#ifndef _Out_writes_
#define _Out_writes_(x)
#endif

#ifndef _Outptr_result_maybenull_
#define _Outptr_result_maybenull_
#endif

#ifndef _Writable_bytes_
#define _Writable_bytes_(x)
#endif

#ifndef _Readable_bytes_
#define _Readable_bytes_(x)
#endif

#ifndef _Success_
#define _Success_(x)
#endif

#ifndef _Pre_notnull_
#define _Pre_notnull_
#endif

#ifdef DETOURS_INTERNAL

#pragma warning(disable:4615) // unknown warning type (suppress with older compilers)

#ifndef _Benign_race_begin_
#define _Benign_race_begin_
#endif

#ifndef _Benign_race_end_
#define _Benign_race_end_
#endif

#ifndef _Field_size_
#define _Field_size_(x)
#endif

#ifndef _Field_range_
#define _Field_range_(x,y)
#endif

#ifndef _Analysis_assume_
#define _Analysis_assume_(x)
#endif

#endif // DETOURS_INTERNAL
#endif // DETOURS_DONT_REMOVE_SAL_20

//////////////////////////////////////////////////////////////////////////////
//
#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} GUID;

#ifdef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        const GUID name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    const GUID name
#endif // INITGUID
#endif // !GUID_DEFINED

#if defined(__cplusplus)
#ifndef _REFGUID_DEFINED
#define _REFGUID_DEFINED
#define REFGUID             const GUID &
#endif // !_REFGUID_DEFINED
#else // !__cplusplus
#ifndef _REFGUID_DEFINED
#define _REFGUID_DEFINED
#define REFGUID             const GUID * const
#endif // !_REFGUID_DEFINED
#endif // !__cplusplus

#ifndef ARRAYSIZE
#define ARRAYSIZE(x)    (sizeof(x)/sizeof(x[0]))
#endif

//
//////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
    #endif // __cplusplus

    /////////////////////////////////////////////////// Instruction Target Macros.
    //
    #define DETOUR_INSTRUCTION_TARGET_NONE          ((PVOID)0)
    #define DETOUR_INSTRUCTION_TARGET_DYNAMIC       ((PVOID)(LONG_PTR)-1)
    #define DETOUR_SECTION_HEADER_SIGNATURE         0x00727444   // "Dtr\0"

    extern const GUID DETOUR_EXE_RESTORE_GUID;
    extern const GUID DETOUR_EXE_HELPER_GUID;

    #define DETOUR_TRAMPOLINE_SIGNATURE             0x21727444  // Dtr!
    typedef struct _DETOUR_TRAMPOLINE DETOUR_TRAMPOLINE, *PDETOUR_TRAMPOLINE;

    #ifndef DETOUR_MAX_SUPPORTED_IMAGE_SECTION_HEADERS
    #define DETOUR_MAX_SUPPORTED_IMAGE_SECTION_HEADERS      32
    #endif // !DETOUR_MAX_SUPPORTED_IMAGE_SECTION_HEADERS

    /////////////////////////////////////////////////////////// Binary Structures.
    //
    #pragma pack(push, 8)
    typedef struct _DETOUR_SECTION_HEADER {
        DWORD cbHeaderSize;
        DWORD nSignature;
        DWORD nDataOffset;
        DWORD cbDataSize;

        DWORD nOriginalImportVirtualAddress;
        DWORD nOriginalImportSize;
        DWORD nOriginalBoundImportVirtualAddress;
        DWORD nOriginalBoundImportSize;

        DWORD nOriginalIatVirtualAddress;
        DWORD nOriginalIatSize;
        DWORD nOriginalSizeOfImage;
        DWORD cbPrePE;

        DWORD nOriginalClrFlags;
        DWORD reserved1;
        DWORD reserved2;
        DWORD reserved3;

        // Followed by cbPrePE bytes of data.
    } DETOUR_SECTION_HEADER, *PDETOUR_SECTION_HEADER;

    typedef struct _DETOUR_SECTION_RECORD {
        DWORD cbBytes;
        DWORD nReserved;
        GUID guid;
    } DETOUR_SECTION_RECORD, *PDETOUR_SECTION_RECORD;

    typedef struct _DETOUR_CLR_HEADER {
        // Header versioning
        ULONG cb;
        USHORT MajorRuntimeVersion;
        USHORT MinorRuntimeVersion;

        // Symbol table and startup information
        IMAGE_DATA_DIRECTORY MetaData;
        ULONG Flags;

        // Followed by the rest of the IMAGE_COR20_HEADER
    } DETOUR_CLR_HEADER, *PDETOUR_CLR_HEADER;

    typedef struct _DETOUR_EXE_RESTORE {
        DWORD cb;
        DWORD cbidh;
        DWORD cbinh;
        DWORD cbclr;

        PBYTE pidh;
        PBYTE pinh;
        PBYTE pclr;

        IMAGE_DOS_HEADER idh;

        union {
            IMAGE_NT_HEADERS inh; // all environments have this
            #ifdef IMAGE_NT_OPTIONAL_HDR32_MAGIC    // some environments do not have this
            IMAGE_NT_HEADERS32 inh32;
            #endif
            #ifdef IMAGE_NT_OPTIONAL_HDR64_MAGIC    // some environments do not have this
            IMAGE_NT_HEADERS64 inh64;
            #endif
            #ifdef IMAGE_NT_OPTIONAL_HDR64_MAGIC    // some environments do not have this
            BYTE raw[sizeof(IMAGE_NT_HEADERS64) + sizeof(IMAGE_SECTION_HEADER) * DETOUR_MAX_SUPPORTED_IMAGE_SECTION_HEADERS];
            #else
            BYTE raw[0x108 + sizeof(IMAGE_SECTION_HEADER) * DETOUR_MAX_SUPPORTED_IMAGE_SECTION_HEADERS];
            #endif
        };

        DETOUR_CLR_HEADER clr;
    } DETOUR_EXE_RESTORE, *PDETOUR_EXE_RESTORE;

    #ifdef IMAGE_NT_OPTIONAL_HDR64_MAGIC
    C_ASSERT(sizeof(IMAGE_NT_HEADERS64) == 0x108);
    #endif

    // The size can change, but assert for clarity due to the muddying #ifdefs.
    #ifdef _WIN64
    C_ASSERT(sizeof(DETOUR_EXE_RESTORE) == 0x688);
    #else
    C_ASSERT (sizeof(DETOUR_EXE_RESTORE)== 0x678);
    #endif

    typedef struct _DETOUR_EXE_HELPER {
        DWORD cb;
        DWORD pid;
        DWORD nDlls;
        CHAR rDlls[4];
    } DETOUR_EXE_HELPER, *PDETOUR_EXE_HELPER;

    #pragma pack(pop)

    #define DETOUR_SECTION_HEADER_DECLARE(cbSectionSize) \
{ \
      sizeof(DETOUR_SECTION_HEADER),\
      DETOUR_SECTION_HEADER_SIGNATURE,\
      sizeof(DETOUR_SECTION_HEADER),\
      (cbSectionSize),\
      \
      0,\
      0,\
      0,\
      0,\
      \
      0,\
      0,\
      0,\
      0,\
}

    ///////////////////////////////////////////////////////////// Binary Typedefs.
    //
    using PF_DETOUR_BINARY_BYWAY_CALLBACK = BOOL(CALLBACK *)(_In_opt_ PVOID pContext, _In_opt_ LPCSTR pszFile, _Outptr_result_maybenull_ LPCSTR* ppszOutFile);

    using PF_DETOUR_BINARY_FILE_CALLBACK = BOOL(CALLBACK *)(_In_opt_ PVOID pContext, _In_ LPCSTR pszOrigFile, _In_ LPCSTR pszFile, _Outptr_result_maybenull_ LPCSTR* ppszOutFile);

    using PF_DETOUR_BINARY_SYMBOL_CALLBACK = BOOL(CALLBACK *)(_In_opt_ PVOID pContext,
                                                              _In_ ULONG nOrigOrdinal,
                                                              _In_ ULONG nOrdinal,
                                                              _Out_ ULONG* pnOutOrdinal,
                                                              _In_opt_ LPCSTR pszOrigSymbol,
                                                              _In_opt_ LPCSTR pszSymbol,
                                                              _Outptr_result_maybenull_ LPCSTR* ppszOutSymbol);

    using PF_DETOUR_BINARY_COMMIT_CALLBACK = BOOL(CALLBACK *)(_In_opt_ PVOID pContext);

    using PF_DETOUR_ENUMERATE_EXPORT_CALLBACK = BOOL(CALLBACK *)(_In_opt_ PVOID pContext, _In_ ULONG nOrdinal, _In_opt_ LPCSTR pszName, _In_opt_ PVOID pCode);

    using PF_DETOUR_IMPORT_FILE_CALLBACK = BOOL(CALLBACK *)(_In_opt_ PVOID pContext, _In_opt_ HMODULE hModule, _In_opt_ LPCSTR pszFile);

    using PF_DETOUR_IMPORT_FUNC_CALLBACK = BOOL(CALLBACK *)(_In_opt_ PVOID pContext, _In_ DWORD nOrdinal, _In_opt_ LPCSTR pszFunc, _In_opt_ PVOID pvFunc);

    // Same as PF_DETOUR_IMPORT_FUNC_CALLBACK but extra indirection on last parameter.
    using PF_DETOUR_IMPORT_FUNC_CALLBACK_EX = BOOL(CALLBACK *)(_In_opt_ PVOID pContext, _In_ DWORD nOrdinal, _In_opt_ LPCSTR pszFunc, _In_opt_ PVOID* ppvFunc);

    using PDETOUR_BINARY = VOID*;
    using PDETOUR_LOADED_BINARY = VOID*;

    //////////////////////////////////////////////////////////// Transaction APIs.
    //
    auto WINAPI DetourTransactionBegin(VOID) -> LONG;
    auto WINAPI DetourTransactionAbort(VOID) -> LONG;
    auto WINAPI DetourTransactionCommit(VOID) -> LONG;
    auto WINAPI DetourTransactionCommitEx(_Out_opt_ PVOID** pppFailedPointer) -> LONG;

    auto WINAPI DetourUpdateThread(_In_ HANDLE hThread) -> LONG;

    auto WINAPI DetourAttach(_Inout_ PVOID* ppPointer, _In_ PVOID pDetour) -> LONG;

    auto WINAPI DetourAttachEx(_Inout_ PVOID* ppPointer, _In_ PVOID pDetour, _Out_opt_ PDETOUR_TRAMPOLINE* ppRealTrampoline, _Out_opt_ PVOID* ppRealTarget, _Out_opt_ PVOID* ppRealDetour) -> LONG;

    auto WINAPI DetourDetach(_Inout_ PVOID* ppPointer, _In_ PVOID pDetour) -> LONG;

    auto WINAPI DetourSetIgnoreTooSmall(_In_ BOOL fIgnore) -> BOOL;
    auto WINAPI DetourSetRetainRegions(_In_ BOOL fRetain) -> BOOL;
    auto WINAPI DetourSetSystemRegionLowerBound(_In_ PVOID pSystemRegionLowerBound) -> PVOID;
    auto WINAPI DetourSetSystemRegionUpperBound(_In_ PVOID pSystemRegionUpperBound) -> PVOID;

    ////////////////////////////////////////////////////////////// Code Functions.
    //
    auto WINAPI DetourFindFunction(_In_ LPCSTR pszModule, _In_ LPCSTR pszFunction) -> PVOID;
    auto WINAPI DetourCodeFromPointer(_In_ PVOID pPointer, _Out_opt_ PVOID* ppGlobals) -> PVOID;
    auto WINAPI DetourCopyInstruction(_In_opt_ PVOID pDst, _Inout_opt_ PVOID* ppDstPool, _In_ PVOID pSrc, _Out_opt_ PVOID* ppTarget, _Out_opt_ LONG* plExtra) -> PVOID;
    auto WINAPI DetourSetCodeModule(_In_ HMODULE hModule, _In_ BOOL fLimitReferencesToModule) -> BOOL;
    auto WINAPI DetourAllocateRegionWithinJumpBounds(_In_ LPCVOID pbTarget, _Out_ PDWORD pcbAllocatedSize) -> PVOID;
    auto WINAPI DetourIsFunctionImported(_In_ PBYTE pbCode, _In_ PBYTE pbAddress) -> BOOL;

    ///////////////////////////////////////////////////// Loaded Binary Functions.
    //
    auto WINAPI DetourGetContainingModule(_In_ PVOID pvAddr) -> HMODULE;
    auto WINAPI DetourEnumerateModules(_In_opt_ HMODULE hModuleLast) -> HMODULE;
    auto WINAPI DetourGetEntryPoint(_In_opt_ HMODULE hModule) -> PVOID;
    auto WINAPI DetourGetModuleSize(_In_opt_ HMODULE hModule) -> ULONG;
    auto WINAPI DetourEnumerateExports(_In_ HMODULE hModule, _In_opt_ PVOID pContext, _In_ PF_DETOUR_ENUMERATE_EXPORT_CALLBACK pfExport) -> BOOL;
    auto WINAPI DetourEnumerateImports(_In_opt_ HMODULE hModule,
                                       _In_opt_ PVOID pContext,
                                       _In_opt_ PF_DETOUR_IMPORT_FILE_CALLBACK pfImportFile,
                                       _In_opt_ PF_DETOUR_IMPORT_FUNC_CALLBACK pfImportFunc) -> BOOL;

    auto WINAPI DetourEnumerateImportsEx(_In_opt_ HMODULE hModule,
                                         _In_opt_ PVOID pContext,
                                         _In_opt_ PF_DETOUR_IMPORT_FILE_CALLBACK pfImportFile,
                                         _In_opt_ PF_DETOUR_IMPORT_FUNC_CALLBACK_EX pfImportFuncEx) -> BOOL;

    _Writable_bytes_(*pcbData)_Readable_bytes_(*pcbData)_Success_(return != NULL)auto WINAPI DetourFindPayload(_In_opt_ HMODULE hModule, _In_ REFGUID rguid, _Out_opt_ DWORD* pcbData) -> PVOID;

    _Writable_bytes_(*pcbData)_Readable_bytes_(*pcbData)_Success_(return != NULL)auto WINAPI DetourFindPayloadEx(_In_ REFGUID rguid, _Out_opt_ DWORD* pcbData) -> PVOID;

    auto WINAPI DetourGetSizeOfPayloads(_In_opt_ HMODULE hModule) -> DWORD;

    auto WINAPI DetourFreePayload(_In_ PVOID pvData) -> BOOL;
    ///////////////////////////////////////////////// Persistent Binary Functions.
    //

    auto WINAPI DetourBinaryOpen(_In_ HANDLE hFile) -> PDETOUR_BINARY;

    _Writable_bytes_(*pcbData)_Readable_bytes_(*pcbData)_Success_(return != NULL)auto WINAPI DetourBinaryEnumeratePayloads(
            _In_ PDETOUR_BINARY pBinary,
            _Out_opt_ GUID* pGuid,
            _Out_ DWORD* pcbData,
            _Inout_ DWORD* pnIterator) -> PVOID;

    _Writable_bytes_(*pcbData)_Readable_bytes_(*pcbData)_Success_(return != NULL)auto WINAPI DetourBinaryFindPayload(_In_ PDETOUR_BINARY pBinary, _In_ REFGUID rguid, _Out_ DWORD* pcbData) -> PVOID;

    auto WINAPI DetourBinarySetPayload(_In_ PDETOUR_BINARY pBinary, _In_ REFGUID rguid, _In_reads_opt_(cbData) PVOID pData, _In_ DWORD cbData) -> PVOID;
    auto WINAPI DetourBinaryDeletePayload(_In_ PDETOUR_BINARY pBinary, _In_ REFGUID rguid) -> BOOL;
    auto WINAPI DetourBinaryPurgePayloads(_In_ PDETOUR_BINARY pBinary) -> BOOL;
    auto WINAPI DetourBinaryResetImports(_In_ PDETOUR_BINARY pBinary) -> BOOL;
    auto WINAPI DetourBinaryEditImports(_In_ PDETOUR_BINARY pBinary,
                                        _In_opt_ PVOID pContext,
                                        _In_opt_ PF_DETOUR_BINARY_BYWAY_CALLBACK pfByway,
                                        _In_opt_ PF_DETOUR_BINARY_FILE_CALLBACK pfFile,
                                        _In_opt_ PF_DETOUR_BINARY_SYMBOL_CALLBACK pfSymbol,
                                        _In_opt_ PF_DETOUR_BINARY_COMMIT_CALLBACK pfCommit) -> BOOL;
    auto WINAPI DetourBinaryWrite(_In_ PDETOUR_BINARY pBinary, _In_ HANDLE hFile) -> BOOL;
    auto WINAPI DetourBinaryClose(_In_ PDETOUR_BINARY pBinary) -> BOOL;

    /////////////////////////////////////////////////// Create Process & Load Dll.
    //
    _Success_(return != NULL)auto WINAPI DetourFindRemotePayload(_In_ HANDLE hProcess, _In_ REFGUID rguid, _Out_opt_ DWORD* pcbData) -> PVOID;

    using PDETOUR_CREATE_PROCESS_ROUTINEA = BOOL(WINAPI *)(_In_opt_ LPCSTR lpApplicationName,
                                                           _Inout_opt_ LPSTR lpCommandLine,
                                                           _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                                           _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                                           _In_ BOOL bInheritHandles,
                                                           _In_ DWORD dwCreationFlags,
                                                           _In_opt_ LPVOID lpEnvironment,
                                                           _In_opt_ LPCSTR lpCurrentDirectory,
                                                           _In_ LPSTARTUPINFOA lpStartupInfo,
                                                           _Out_ LPPROCESS_INFORMATION lpProcessInformation);

    using PDETOUR_CREATE_PROCESS_ROUTINEW = BOOL(WINAPI *)(_In_opt_ LPCWSTR lpApplicationName,
                                                           _Inout_opt_ LPWSTR lpCommandLine,
                                                           _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                                           _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                                           _In_ BOOL bInheritHandles,
                                                           _In_ DWORD dwCreationFlags,
                                                           _In_opt_ LPVOID lpEnvironment,
                                                           _In_opt_ LPCWSTR lpCurrentDirectory,
                                                           _In_ LPSTARTUPINFOW lpStartupInfo,
                                                           _Out_ LPPROCESS_INFORMATION lpProcessInformation);

    auto WINAPI DetourCreateProcessWithDllA(_In_opt_ LPCSTR lpApplicationName,
                                            _Inout_opt_ LPSTR lpCommandLine,
                                            _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                            _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                            _In_ BOOL bInheritHandles,
                                            _In_ DWORD dwCreationFlags,
                                            _In_opt_ LPVOID lpEnvironment,
                                            _In_opt_ LPCSTR lpCurrentDirectory,
                                            _In_ LPSTARTUPINFOA lpStartupInfo,
                                            _Out_ LPPROCESS_INFORMATION lpProcessInformation,
                                            _In_ LPCSTR lpDllName,
                                            _In_opt_ PDETOUR_CREATE_PROCESS_ROUTINEA pfCreateProcessA) -> BOOL;

    auto WINAPI DetourCreateProcessWithDllW(_In_opt_ LPCWSTR lpApplicationName,
                                            _Inout_opt_ LPWSTR lpCommandLine,
                                            _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                            _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                            _In_ BOOL bInheritHandles,
                                            _In_ DWORD dwCreationFlags,
                                            _In_opt_ LPVOID lpEnvironment,
                                            _In_opt_ LPCWSTR lpCurrentDirectory,
                                            _In_ LPSTARTUPINFOW lpStartupInfo,
                                            _Out_ LPPROCESS_INFORMATION lpProcessInformation,
                                            _In_ LPCSTR lpDllName,
                                            _In_opt_ PDETOUR_CREATE_PROCESS_ROUTINEW pfCreateProcessW) -> BOOL;

    #ifdef UNICODE
    #define DetourCreateProcessWithDll      DetourCreateProcessWithDllW
    #define PDETOUR_CREATE_PROCESS_ROUTINE  PDETOUR_CREATE_PROCESS_ROUTINEW
    #else
    #define DetourCreateProcessWithDll      DetourCreateProcessWithDllA
    #define PDETOUR_CREATE_PROCESS_ROUTINE  PDETOUR_CREATE_PROCESS_ROUTINEA
    #endif // !UNICODE

    auto WINAPI DetourCreateProcessWithDllExA(_In_opt_ LPCSTR lpApplicationName,
                                              _Inout_opt_ LPSTR lpCommandLine,
                                              _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                              _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                              _In_ BOOL bInheritHandles,
                                              _In_ DWORD dwCreationFlags,
                                              _In_opt_ LPVOID lpEnvironment,
                                              _In_opt_ LPCSTR lpCurrentDirectory,
                                              _In_ LPSTARTUPINFOA lpStartupInfo,
                                              _Out_ LPPROCESS_INFORMATION lpProcessInformation,
                                              _In_ LPCSTR lpDllName,
                                              _In_opt_ PDETOUR_CREATE_PROCESS_ROUTINEA pfCreateProcessA) -> BOOL;

    auto WINAPI DetourCreateProcessWithDllExW(_In_opt_ LPCWSTR lpApplicationName,
                                              _Inout_opt_ LPWSTR lpCommandLine,
                                              _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                              _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                              _In_ BOOL bInheritHandles,
                                              _In_ DWORD dwCreationFlags,
                                              _In_opt_ LPVOID lpEnvironment,
                                              _In_opt_ LPCWSTR lpCurrentDirectory,
                                              _In_ LPSTARTUPINFOW lpStartupInfo,
                                              _Out_ LPPROCESS_INFORMATION lpProcessInformation,
                                              _In_ LPCSTR lpDllName,
                                              _In_opt_ PDETOUR_CREATE_PROCESS_ROUTINEW pfCreateProcessW) -> BOOL;

    #ifdef UNICODE
    #define DetourCreateProcessWithDllEx    DetourCreateProcessWithDllExW
    #else
    #define DetourCreateProcessWithDllEx    DetourCreateProcessWithDllExA
    #endif // !UNICODE

    auto WINAPI DetourCreateProcessWithDllsA(_In_opt_ LPCSTR lpApplicationName,
                                             _Inout_opt_ LPSTR lpCommandLine,
                                             _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                             _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                             _In_ BOOL bInheritHandles,
                                             _In_ DWORD dwCreationFlags,
                                             _In_opt_ LPVOID lpEnvironment,
                                             _In_opt_ LPCSTR lpCurrentDirectory,
                                             _In_ LPSTARTUPINFOA lpStartupInfo,
                                             _Out_ LPPROCESS_INFORMATION lpProcessInformation,
                                             _In_ DWORD nDlls,
                                             _In_reads_(nDlls) LPCSTR* rlpDlls,
                                             _In_opt_ PDETOUR_CREATE_PROCESS_ROUTINEA pfCreateProcessA) -> BOOL;

    auto WINAPI DetourCreateProcessWithDllsW(_In_opt_ LPCWSTR lpApplicationName,
                                             _Inout_opt_ LPWSTR lpCommandLine,
                                             _In_opt_ LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                             _In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                             _In_ BOOL bInheritHandles,
                                             _In_ DWORD dwCreationFlags,
                                             _In_opt_ LPVOID lpEnvironment,
                                             _In_opt_ LPCWSTR lpCurrentDirectory,
                                             _In_ LPSTARTUPINFOW lpStartupInfo,
                                             _Out_ LPPROCESS_INFORMATION lpProcessInformation,
                                             _In_ DWORD nDlls,
                                             _In_reads_(nDlls) LPCSTR* rlpDlls,
                                             _In_opt_ PDETOUR_CREATE_PROCESS_ROUTINEW pfCreateProcessW) -> BOOL;

    #ifdef UNICODE
    #define DetourCreateProcessWithDlls     DetourCreateProcessWithDllsW
    #else
    #define DetourCreateProcessWithDlls     DetourCreateProcessWithDllsA
    #endif // !UNICODE

    auto WINAPI DetourProcessViaHelperA(_In_ DWORD dwTargetPid, _In_ LPCSTR lpDllName, _In_ PDETOUR_CREATE_PROCESS_ROUTINEA pfCreateProcessA) -> BOOL;

    auto WINAPI DetourProcessViaHelperW(_In_ DWORD dwTargetPid, _In_ LPCSTR lpDllName, _In_ PDETOUR_CREATE_PROCESS_ROUTINEW pfCreateProcessW) -> BOOL;

    #ifdef UNICODE
    #define DetourProcessViaHelper          DetourProcessViaHelperW
    #else
    #define DetourProcessViaHelper          DetourProcessViaHelperA
    #endif // !UNICODE

    auto WINAPI DetourProcessViaHelperDllsA(_In_ DWORD dwTargetPid, _In_ DWORD nDlls, _In_reads_(nDlls) LPCSTR* rlpDlls, _In_ PDETOUR_CREATE_PROCESS_ROUTINEA pfCreateProcessA) -> BOOL;

    auto WINAPI DetourProcessViaHelperDllsW(_In_ DWORD dwTargetPid, _In_ DWORD nDlls, _In_reads_(nDlls) LPCSTR* rlpDlls, _In_ PDETOUR_CREATE_PROCESS_ROUTINEW pfCreateProcessW) -> BOOL;

    #ifdef UNICODE
    #define DetourProcessViaHelperDlls      DetourProcessViaHelperDllsW
    #else
    #define DetourProcessViaHelperDlls      DetourProcessViaHelperDllsA
    #endif // !UNICODE

    auto WINAPI DetourUpdateProcessWithDll(_In_ HANDLE hProcess, _In_reads_(nDlls) LPCSTR* rlpDlls, _In_ DWORD nDlls) -> BOOL;

    auto WINAPI DetourUpdateProcessWithDllEx(_In_ HANDLE hProcess, _In_ HMODULE hImage, _In_ BOOL bIs32Bit, _In_reads_(nDlls) LPCSTR* rlpDlls, _In_ DWORD nDlls) -> BOOL;

    auto WINAPI DetourCopyPayloadToProcess(_In_ HANDLE hProcess, _In_ REFGUID rguid, _In_reads_bytes_(cbData) LPCVOID pvData, _In_ DWORD cbData) -> BOOL;
    _Success_(return != NULL)auto WINAPI DetourCopyPayloadToProcessEx(_In_ HANDLE hProcess, _In_ REFGUID rguid, _In_reads_bytes_(cbData) LPCVOID pvData, _In_ DWORD cbData) -> PVOID;

    auto WINAPI DetourRestoreAfterWith(VOID) -> BOOL;
    auto WINAPI DetourRestoreAfterWithEx(_In_reads_bytes_(cbData) PVOID pvData, _In_ DWORD cbData) -> BOOL;
    auto WINAPI DetourIsHelperProcess(VOID) -> BOOL;
    auto CALLBACK DetourFinishHelperProcess(_In_ HWND, _In_ HINSTANCE, _In_ LPSTR, _In_ INT) -> VOID;

    //
    //////////////////////////////////////////////////////////////////////////////
    #ifdef __cplusplus
}
#endif // __cplusplus

/////////////////////////////////////////////////// Type-safe overloads for C++
//
#if __cplusplus >= 201103L || _MSVC_LANG >= 201103L
#include <type_traits>

template<typename T>
struct DetoursIsFunctionPointer : std::false_type {};

template<typename T>
struct DetoursIsFunctionPointer<T*> : std::is_function<std::remove_pointer_t<T>> {};

template<typename T, std::enable_if_t<DetoursIsFunctionPointer<T>::value, int> = 0>
auto DetourAttach(_Inout_ T* ppPointer, _In_ T pDetour) noexcept -> LONG {
    return DetourAttach(reinterpret_cast<void**>(ppPointer), reinterpret_cast<void*>(pDetour));
}

template<typename T, std::enable_if_t<DetoursIsFunctionPointer<T>::value, int> = 0>
auto DetourAttachEx(_Inout_ T* ppPointer, _In_ T pDetour, _Out_opt_ PDETOUR_TRAMPOLINE* ppRealTrampoline, _Out_opt_ T* ppRealTarget, _Out_opt_ T* ppRealDetour) noexcept -> LONG {
    return DetourAttachEx(reinterpret_cast<void**>(ppPointer), reinterpret_cast<void*>(pDetour), ppRealTrampoline, reinterpret_cast<void**>(ppRealTarget), reinterpret_cast<void**>(ppRealDetour));
}

template<typename T, std::enable_if_t<DetoursIsFunctionPointer<T>::value, int> = 0>
auto DetourDetach(_Inout_ T* ppPointer, _In_ T pDetour) noexcept -> LONG {
    return DetourDetach(reinterpret_cast<void**>(ppPointer), reinterpret_cast<void*>(pDetour));
}

#endif // __cplusplus >= 201103L || _MSVC_LANG >= 201103L
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////// Detours Internal Definitions.
//
#ifdef __cplusplus
#ifdef DETOURS_INTERNAL

#define NOTHROW
// #define NOTHROW (nothrow)

//////////////////////////////////////////////////////////////////////////////
//
#if (_MSC_VER < 1299) && !defined(__GNUC__)
#include <imagehlp.h>
typedef IMAGEHLP_MODULE IMAGEHLP_MODULE64;typedef PIMAGEHLP_MODULE PIMAGEHLP_MODULE64;typedef IMAGEHLP_SYMBOL SYMBOL_INFO;typedef PIMAGEHLP_SYMBOL PSYMBOL_INFO;static inline LONG
InterlockedCompareExchange(_Inout_ LONG * ptr, _In_ LONG nval, _In_ LONG oval) {
    return (LONG)::InterlockedCompareExchange((PVOID*)ptr, (PVOID)nval, (PVOID)oval);
}
#else
#pragma warning(push)
#pragma warning(disable:4091) // empty typedef
#include <dbghelp.h>
#pragma warning(pop)
#endif

#ifdef IMAGEAPI // defined by DBGHELP.H
using PF_ImagehlpApiVersionEx = LPAPI_VERSION(NTAPI *)(_In_ LPAPI_VERSION AppVersion);

using PF_SymInitialize = BOOL(NTAPI *)(_In_ HANDLE hProcess, _In_opt_ LPCSTR UserSearchPath, _In_ BOOL fInvadeProcess);
using PF_SymSetOptions = DWORD(NTAPI *)(_In_ DWORD SymOptions);
using PF_SymGetOptions = DWORD(NTAPI *)(VOID);
using PF_SymLoadModule64 = DWORD64(NTAPI *)(_In_ HANDLE hProcess, _In_opt_ HANDLE hFile, _In_opt_ LPSTR ImageName, _In_opt_ LPSTR ModuleName, _In_ DWORD64 BaseOfDll, _In_ DWORD SizeOfDll);
using PF_SymGetModuleInfo64 = BOOL(NTAPI *)(_In_ HANDLE hProcess, _In_ DWORD64 qwAddr, _Out_ PIMAGEHLP_MODULE64 ModuleInfo);
using PF_SymFromName = BOOL(NTAPI *)(_In_ HANDLE hProcess, _In_ LPSTR Name, _Out_ PSYMBOL_INFO Symbol);

typedef struct _DETOUR_SYM_INFO {
    HANDLE hProcess;
    HMODULE hDbgHelp;
    PF_ImagehlpApiVersionEx pfImagehlpApiVersionEx;
    PF_SymInitialize pfSymInitialize;
    PF_SymSetOptions pfSymSetOptions;
    PF_SymGetOptions pfSymGetOptions;
    PF_SymLoadModule64 pfSymLoadModule64;
    PF_SymGetModuleInfo64 pfSymGetModuleInfo64;
    PF_SymFromName pfSymFromName;
} DETOUR_SYM_INFO, *PDETOUR_SYM_INFO;

auto DetourLoadImageHlp(VOID) -> PDETOUR_SYM_INFO;

#endif // IMAGEAPI

#if defined(_INC_STDIO) && !defined(_CRT_STDIO_ARBITRARY_WIDE_SPECIFIERS)
#error detours.h must be included before stdio.h (or at least define _CRT_STDIO_ARBITRARY_WIDE_SPECIFIERS earlier)
#endif
#define _CRT_STDIO_ARBITRARY_WIDE_SPECIFIERS 1

#ifdef _DEBUG

int Detour_AssertExprWithFunctionName(int reportType, const char* filename, int linenumber, const char* FunctionName, const char* msg);

#define DETOUR_ASSERT_EXPR_WITH_FUNCTION(expr, msg) \
    (void) ((expr) || \
    (1 != Detour_AssertExprWithFunctionName(_CRT_ASSERT, __FILE__, __LINE__,__FUNCTION__, msg)) || \
    (_CrtDbgBreak(), 0))

#define DETOUR_ASSERT(expr) DETOUR_ASSERT_EXPR_WITH_FUNCTION((expr), #expr)

#else// _DEBUG
#define DETOUR_ASSERT(expr)
#endif// _DEBUG

#ifndef DETOUR_TRACE
#if DETOUR_DEBUG
#define DETOUR_TRACE(x) printf x
#define DETOUR_BREAK()  __debugbreak()
#include <stdio.h>
#include <limits.h>
#else
#define DETOUR_TRACE(x)
#define DETOUR_BREAK()
#endif
#endif

#if 1 || defined(DETOURS_IA64)

//
// IA64 instructions are 41 bits, 3 per bundle, plus 5 bit bundle template => 128 bits per bundle.
//

#define DETOUR_IA64_INSTRUCTIONS_PER_BUNDLE (3)

#define DETOUR_IA64_TEMPLATE_OFFSET (0)
#define DETOUR_IA64_TEMPLATE_SIZE   (5)

#define DETOUR_IA64_INSTRUCTION_SIZE (41)
#define DETOUR_IA64_INSTRUCTION0_OFFSET (DETOUR_IA64_TEMPLATE_SIZE)
#define DETOUR_IA64_INSTRUCTION1_OFFSET (DETOUR_IA64_TEMPLATE_SIZE + DETOUR_IA64_INSTRUCTION_SIZE)
#define DETOUR_IA64_INSTRUCTION2_OFFSET (DETOUR_IA64_TEMPLATE_SIZE + DETOUR_IA64_INSTRUCTION_SIZE + DETOUR_IA64_INSTRUCTION_SIZE)

C_ASSERT(DETOUR_IA64_TEMPLATE_SIZE + DETOUR_IA64_INSTRUCTIONS_PER_BUNDLE * DETOUR_IA64_INSTRUCTION_SIZE == 128);

__declspec(align(16)) struct DETOUR_IA64_BUNDLE {
    union {
        BYTE data[16];
        UINT64 wide[2];
    };

    enum {
        A_UNIT = 1u,
        I_UNIT = 2u,
        M_UNIT = 3u,
        B_UNIT = 4u,
        F_UNIT = 5u,
        L_UNIT = 6u,
        X_UNIT = 7u,
    };

    struct DETOUR_IA64_METADATA {
        ULONG nTemplate : 8; // Instruction template.
        ULONG nUnit0    : 4; // Unit for slot 0
        ULONG nUnit1    : 4; // Unit for slot 1
        ULONG nUnit2    : 4; // Unit for slot 2
    };
protected:
    static const DETOUR_IA64_METADATA s_rceCopyTable[33];

    auto RelocateBundle(_Inout_ DETOUR_IA64_BUNDLE* pDst, _Inout_opt_ DETOUR_IA64_BUNDLE* pBundleExtra) const -> UINT;

    auto RelocateInstruction(_Inout_ DETOUR_IA64_BUNDLE* pDst, _In_ BYTE slot, _Inout_opt_ DETOUR_IA64_BUNDLE* pBundleExtra) const -> bool;

    // 120 112 104 96 88 80 72 64 56 48 40 32 24 16  8  0
    //  f.  e.  d. c. b. a. 9. 8. 7. 6. 5. 4. 3. 2. 1. 0.

    //                                      00
    // f.e. d.c. b.a. 9.8. 7.6. 5.4. 3.2. 1.0.
    // 0000 0000 0000 0000 0000 0000 0000 001f : Template [4..0]
    // 0000 0000 0000 0000 0000 03ff ffff ffe0 : Zero [ 41..  5]
    // 0000 0000 0000 0000 0000 3c00 0000 0000 : Zero [ 45.. 42]
    // 0000 0000 0007 ffff ffff c000 0000 0000 : One  [ 82.. 46]
    // 0000 0000 0078 0000 0000 0000 0000 0000 : One  [ 86.. 83]
    // 0fff ffff ff80 0000 0000 0000 0000 0000 : Two  [123.. 87]
    // f000 0000 0000 0000 0000 0000 0000 0000 : Two  [127..124]
    auto GetTemplate() const -> BYTE;
    // Get 4 bit opcodes.
    auto GetInst0() const -> BYTE;
    auto GetInst1() const -> BYTE;
    auto GetInst2() const -> BYTE;
    auto GetUnit(BYTE slot) const -> BYTE;
    auto GetUnit0() const -> BYTE;
    auto GetUnit1() const -> BYTE;
    auto GetUnit2() const -> BYTE;
    // Get 37 bit data.
    auto GetData0() const -> UINT64;
    auto GetData1() const -> UINT64;
    auto GetData2() const -> UINT64;

    // Get/set the full 41 bit instructions.
    auto GetInstruction(BYTE slot) const -> UINT64;
    auto GetInstruction0() const -> UINT64;
    auto GetInstruction1() const -> UINT64;
    auto GetInstruction2() const -> UINT64;
    auto SetInstruction(BYTE slot, UINT64 instruction) -> void;
    auto SetInstruction0(UINT64 instruction) -> void;
    auto SetInstruction1(UINT64 instruction) -> void;
    auto SetInstruction2(UINT64 instruction) -> void;

    // Get/set bitfields.
    static auto GetBits(UINT64 Value, UINT64 Offset, UINT64 Count) -> UINT64;
    static auto SetBits(UINT64 Value, UINT64 Offset, UINT64 Count, UINT64 Field) -> UINT64;

    // Get specific read-only fields.
    static auto GetOpcode(UINT64 instruction) -> UINT64; // 4bit opcode
    static auto GetX(UINT64 instruction) -> UINT64; // 1bit opcode extension
    static auto GetX3(UINT64 instruction) -> UINT64; // 3bit opcode extension
    static auto GetX6(UINT64 instruction) -> UINT64; // 6bit opcode extension

    // Get/set specific fields.
    static auto GetImm7a(UINT64 instruction) -> UINT64;
    static auto SetImm7a(UINT64 instruction, UINT64 imm7a) -> UINT64;
    static auto GetImm13c(UINT64 instruction) -> UINT64;
    static auto SetImm13c(UINT64 instruction, UINT64 imm13c) -> UINT64;
    static auto GetSignBit(UINT64 instruction) -> UINT64;
    static auto SetSignBit(UINT64 instruction, UINT64 signBit) -> UINT64;
    static auto GetImm20a(UINT64 instruction) -> UINT64;
    static auto SetImm20a(UINT64 instruction, UINT64 imm20a) -> UINT64;
    static auto GetImm20b(UINT64 instruction) -> UINT64;
    static auto SetImm20b(UINT64 instruction, UINT64 imm20b) -> UINT64;

    static auto SignExtend(UINT64 Value, UINT64 Offset) -> UINT64;

    auto IsMovlGp() const -> BOOL;

    auto SetInst(BYTE Slot, BYTE nInst) -> VOID;
    auto SetInst0(BYTE nInst) -> VOID;
    auto SetInst1(BYTE nInst) -> VOID;
    auto SetInst2(BYTE nInst) -> VOID;
    auto SetData(BYTE Slot, UINT64 nData) -> VOID;
    auto SetData0(UINT64 nData) -> VOID;
    auto SetData1(UINT64 nData) -> VOID;
    auto SetData2(UINT64 nData) -> VOID;
    auto SetNop(BYTE Slot) -> BOOL;
    auto SetNop0() -> BOOL;
    auto SetNop1() -> BOOL;
    auto SetNop2() -> BOOL;
public:
    auto IsBrl() const -> BOOL;
    auto SetBrl() -> VOID;
    auto SetBrl(UINT64 target) -> VOID;
    auto GetBrlTarget() const -> UINT64;
    auto SetBrlTarget(UINT64 target) -> VOID;
    auto SetBrlImm(UINT64 imm) -> VOID;
    auto GetBrlImm() const -> UINT64;

    auto GetMovlGp() const -> UINT64;
    auto SetMovlGp(UINT64 gp) -> VOID;

    auto SetStop() -> VOID;

    auto Copy(_Out_ DETOUR_IA64_BUNDLE* pDst, _Inout_opt_ DETOUR_IA64_BUNDLE* pBundleExtra = nullptr) const -> UINT;
};
#endif // DETOURS_IA64

#ifdef DETOURS_ARM

#define DETOURS_PFUNC_TO_PBYTE(p)  ((PBYTE)(((ULONG_PTR)(p)) & ~(ULONG_PTR)1))
#define DETOURS_PBYTE_TO_PFUNC(p)  ((PBYTE)(((ULONG_PTR)(p)) | (ULONG_PTR)1))

#endif // DETOURS_ARM

//////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
    #endif // __cplusplus

    #define DETOUR_OFFLINE_LIBRARY(x)                                       \
PVOID WINAPI DetourCopyInstruction##x(_In_opt_ PVOID pDst,              \
                                      _Inout_opt_ PVOID *ppDstPool,     \
                                      _In_ PVOID pSrc,                  \
                                      _Out_opt_ PVOID *ppTarget,        \
                                      _Out_opt_ LONG *plExtra);         \
                                                                        \
BOOL WINAPI DetourSetCodeModule##x(_In_ HMODULE hModule,                \
                                   _In_ BOOL fLimitReferencesToModule);
    DETOUR_OFFLINE_LIBRARY(X86)
    DETOUR_OFFLINE_LIBRARY(X64)
    DETOUR_OFFLINE_LIBRARY(ARM)
    DETOUR_OFFLINE_LIBRARY(ARM64)
    DETOUR_OFFLINE_LIBRARY(IA64)

    #undef DETOUR_OFFLINE_LIBRARY

    //////////////////////////////////////////////////////////////////////////////
    //
    // Helpers for manipulating page protection.
    //

    _Success_(return != FALSE)auto WINAPI DetourVirtualProtectSameExecuteEx(_In_ HANDLE hProcess, _In_ PVOID pAddress, _In_ SIZE_T nSize, _In_ DWORD dwNewProtect, _Out_ PDWORD pdwOldProtect) -> BOOL;

    _Success_(return != FALSE)auto WINAPI DetourVirtualProtectSameExecute(_In_ PVOID pAddress, _In_ SIZE_T nSize, _In_ DWORD dwNewProtect, _Out_ PDWORD pdwOldProtect) -> BOOL;

    // Detours must depend only on kernel32.lib, so we cannot use IsEqualGUID
    auto WINAPI DetourAreSameGuid(_In_ REFGUID left, _In_ REFGUID right) -> BOOL;
    #ifdef __cplusplus
}
#endif // __cplusplus

//////////////////////////////////////////////////////////////////////////////

#define MM_ALLOCATION_GRANULARITY 0x10000

//////////////////////////////////////////////////////////////////////////////

#endif // DETOURS_INTERNAL
#endif // __cplusplus

#endif // _DETOURS_H_
//
////////////////////////////////////////////////////////////////  End of File.
