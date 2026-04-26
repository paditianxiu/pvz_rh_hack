#include "../pch.h"

#include <atomic>
#include <cstdint>
#include <format>

#include "../Console.hpp"
#include "../UnityResolve.hpp"

#include "BoardRuntime.hpp"
#include "Bootstrap.hpp"
#include "PipeServer.hpp"

namespace bootstrap {
namespace {
HANDLE g_stopEvent = nullptr;
HANDLE g_debugThread = nullptr;
HANDLE g_initThread = nullptr;
HMODULE g_moduleHandle = nullptr;
std::atomic_bool g_consoleStarted = false;
std::atomic_bool g_fastUnloadStarted = false;

bool IsStopRequested() {
    return g_stopEvent != nullptr && WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0;
}

void SignalStopEvent() {
    if (g_stopEvent != nullptr) {
        SetEvent(g_stopEvent);
    }
}

void InitializeFeatures() {
    if (IsStopRequested()) {
        return;
    }

    if (board_runtime::InstallBoardHooks()) {
        LOG_INFO("Board 钩子安装成功");
    }
    else {
        LOG_WARNING("安装 Board 钩子失败，将使用回退方式");
	}

	const auto instance = board_runtime::GetBoardInstance();
	if (instance) {
		LOG_INFO(std::format("初始 Board 实例: 0x{:X}", reinterpret_cast<uintptr_t>(instance)).c_str());
	}
	else {
		LOG_WARNING("初始化阶段不可用 Board 实例");
	}

	if (IsStopRequested()) {
		return;
	}

	board_runtime::SetFreeCD(false);

    LOG_INFO("功能初始化完成");
}

DWORD WINAPI BootstrapThread(LPVOID) {
    console::StartConsole(L"调试控制台", false);
    g_consoleStarted = true;

    if (IsStopRequested()) {
        return 0;
    }

    UnityResolve::Init(GetModuleHandleA("GameAssembly.dll"), UnityResolve::Mode::Il2Cpp);
    LOG_INFO("注入端初始化成功！");

    InitializeFeatures();
    if (IsStopRequested()) {
        return 0;
    }

    if (ipc::StartPipeServer(g_stopEvent)) {
        LOG_INFO("IPC 服务已启动");
    }

    return 0;
}

void WaitAndCloseThread(HANDLE& threadHandle, DWORD timeoutMs) {
    if (threadHandle == nullptr) {
        return;
    }

    WaitForSingleObject(threadHandle, timeoutMs);
    CloseHandle(threadHandle);
    threadHandle = nullptr;
}

DWORD WINAPI FastUnloadThread(LPVOID) {
    Stop(false);

    const HMODULE moduleHandle = g_moduleHandle;
    if (moduleHandle != nullptr) {
        FreeLibraryAndExitThread(moduleHandle, 0);
    }

    g_fastUnloadStarted = false;
    return 0;
}
}

void Start() {
    if (g_stopEvent != nullptr) {
        return;
    }

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (g_stopEvent == nullptr) {
        return;
    }

    g_initThread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
    if (g_initThread == nullptr) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

void Stop(bool processTerminating) {
    if (g_stopEvent == nullptr) {
        if (g_consoleStarted.exchange(false)) {
            console::EndConsole();
        }
        return;
    }

    SignalStopEvent();

    ipc::StopPipeServer(processTerminating ? 100 : 6000);
    WaitAndCloseThread(g_debugThread, processTerminating ? 100 : 6000);
    WaitAndCloseThread(g_initThread, processTerminating ? 100 : 6000);

    board_runtime::SetFreeCD(false);
    board_runtime::UninstallBoardHooks();

    if (!processTerminating) {
        LOG_INFO("DLL 清理完成");
    }

    if (g_consoleStarted.exchange(false)) {
        console::EndConsole();
    }

    CloseHandle(g_stopEvent);
    g_stopEvent = nullptr;
}

void SetModuleHandle(void* moduleHandle) {
    g_moduleHandle = static_cast<HMODULE>(moduleHandle);
}

void NotifyProcessDetach(bool processTerminating) {
    if (processTerminating) {
        SignalStopEvent();
        return;
    }

    if (g_fastUnloadStarted.load()) {
        // RequestFastUnload path already performed full cleanup on its worker thread.
        SignalStopEvent();
        return;
    }

    // Backward-compatibility path: old external FreeLibrary() unload.
    // Keep this bounded to reduce loader-lock stall while avoiding use-after-unload crashes.
    Stop(true);
}

bool RequestFastUnload() {
    if (g_fastUnloadStarted.exchange(true)) {
        return false;
    }

    if (g_moduleHandle == nullptr) {
        g_fastUnloadStarted = false;
        return false;
    }

    HANDLE unloadThread = CreateThread(nullptr, 0, FastUnloadThread, nullptr, 0, nullptr);
    if (unloadThread == nullptr) {
        g_fastUnloadStarted = false;
        return false;
    }

    CloseHandle(unloadThread);
    return true;
}
}
