// 2026-03-28 04:53:36

#pragma once
#include <TlHelp32.h>
#include <Windows.h>
#include <bitset>
#include <functional>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <winternl.h>
#include <detours/detours.h>

namespace stdpp::hook {
    class Inline {
    public:
        template<typename Func>
        static auto create(Func func, Func callback) -> bool {
            std::lock_guard lock(mutex);

            if (hooks.contains(func)) {
                remove_internal(func);
            }

            Func original = func;
            LONG error = DetourTransactionBegin();
            if (error != NO_ERROR) {
                return false;
            }

            error = DetourUpdateThread(GetCurrentThread());
            if (error != NO_ERROR) {
                DetourTransactionAbort();
                return false;
            }

            error = DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(callback));
            if (error != NO_ERROR) {
                DetourTransactionAbort();
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                DetourTransactionAbort();
                return false;
            }

            hooks[func] = {reinterpret_cast<void*>(callback), reinterpret_cast<void*>(original)};
            return true;
        }

        static auto remove(const void* func) -> bool {
            std::lock_guard lock(mutex);
            return remove_internal(func);
        }

        template<typename R, typename... Args>
        static auto call(R (*func)(Args...), Args... args) -> R {
            std::lock_guard lock(mutex);

            auto it = hooks.find(func);
            if (it == hooks.end()) {
                return func(args...);
            }

            auto original = reinterpret_cast<R(*)(Args...)>(it->second.second);
            return original(args...);
        }

        static auto shutdown() -> void {
            std::lock_guard lock(mutex);
            for (const auto& func : hooks | std::views::keys) {
                remove_internal(func);
            }
            hooks.clear();
        }
    private:
        inline static std::unordered_map<const void*, std::pair<void*, void*>> hooks;
        inline static std::mutex mutex;

        static auto remove_internal(const void* func) -> bool {
            const auto it = hooks.find(func);
            if (it == hooks.end()) {
                return false;
            }

            LONG error = DetourTransactionBegin();
            if (error != NO_ERROR) {
                return false;
            }

            error = DetourUpdateThread(GetCurrentThread());
            if (error != NO_ERROR) {
                DetourTransactionAbort();
                return false;
            }

            error = DetourDetach(&it->second.second, it->second.first);
            if (error != NO_ERROR) {
                DetourTransactionAbort();
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                DetourTransactionAbort();
                return false;
            }

            hooks.erase(it);
            return true;
        }
    };

    class IAT {
    public:
        template<typename Func>
        static auto create(const HMODULE module, const std::string& import_module, const std::string& func_name, Func callback) -> bool {
            auto target = find_iat_entry(module, import_module, func_name);
            if (!target) {
                return false;
            }

            DWORD old_protect;
            if (!VirtualProtect(target, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
                return false;
            }

            Func original = reinterpret_cast<Func>(*target);
            *target = reinterpret_cast<void*>(callback);

            VirtualProtect(target, sizeof(void*), old_protect, &old_protect);

            hooks[target] = {static_cast<void*>(callback), static_cast<void*>(original)};
            return true;
        }

        static auto remove(const HMODULE module, const std::string& import_module, const std::string& func_name) -> bool {
            const auto target = find_iat_entry(module, import_module, func_name);
            if (!target || !hooks.contains(target)) {
                return false;
            }

            DWORD old_protect;
            VirtualProtect(target, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect);
            *target = hooks[target].second;
            VirtualProtect(target, sizeof(void*), old_protect, &old_protect);

            hooks.erase(target);
            return true;
        }

        template<typename R, typename... Args>
        static auto call(const HMODULE module, const std::string& import_module, const std::string& func_name, Args... args) -> R {
            const auto target = find_iat_entry(module, import_module, func_name);
            if (!target || !hooks.contains(target)) {
                return R();
            }
            return reinterpret_cast<R(*)(Args...)>(hooks[target].second)(args...);
        }
    private:
        inline static std::unordered_map<void*, std::pair<void*, void*>> hooks;

        static auto find_iat_entry(const HMODULE module, const std::string& import_module, const std::string& func_name) -> void** {
            if (!module) {
                return nullptr;
            }
            const auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
            const auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(module) + dos->e_lfanew);
            const auto [VirtualAddress, Size] = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (!VirtualAddress) {
                return nullptr;
            }
            for (auto import_desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(reinterpret_cast<BYTE*>(module) + VirtualAddress); import_desc->Name; ++import_desc) {
                const auto mod_name = reinterpret_cast<const char*>(reinterpret_cast<BYTE*>(module) + import_desc->Name);
                if (_stricmp(mod_name, import_module.c_str()) != 0) {
                    continue;
                }
                auto thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(module) + import_desc->OriginalFirstThunk);
                auto iat = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(module) + import_desc->FirstThunk);
                if (!thunk) {
                    thunk = iat;
                }
                for (; thunk->u1.AddressOfData; ++thunk, ++iat) {
                    if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                        continue;
                    }
                    const auto import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<BYTE*>(module) + thunk->u1.AddressOfData);
                    if (strcmp(import->Name, func_name.c_str()) == 0) {
                        return reinterpret_cast<void**>(&iat->u1.Function);
                    }
                }
            }
            return nullptr;
        }
    };

    class VTable {
    public:
        template<typename Func>
        static auto create(void* instance, const size_t index, Func callback) -> bool {
            if (!instance) {
                return false;
            }

            const auto vtable = *static_cast<void***>(instance);
            void** entry = &vtable[index];
            DWORD old_protect;
            if (!VirtualProtect(entry, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect)) {
                return false;
            }

            Func original = reinterpret_cast<Func>(*entry);
            *entry = reinterpret_cast<void*>(callback);
            VirtualProtect(entry, sizeof(void*), old_protect, &old_protect);

            hooks[entry] = {static_cast<void*>(callback), static_cast<void*>(original)};
            return true;
        }

        static auto remove(void* instance, const size_t index) -> bool {
            if (!instance) {
                return false;
            }

            const auto vtable = *static_cast<void***>(instance);
            void** entry = &vtable[index];
            if (!hooks.contains(entry)) {
                return false;
            }

            DWORD old_protect;
            VirtualProtect(entry, sizeof(void*), PAGE_EXECUTE_READWRITE, &old_protect);
            *entry = hooks[entry].second;
            VirtualProtect(entry, sizeof(void*), old_protect, &old_protect);

            hooks.erase(entry);
            return true;
        }

        template<typename R, typename... Args>
        static auto call(void* instance, const size_t index, Args... args) -> R {
            const auto vtable = *static_cast<void***>(instance);
            void** entry = &vtable[index];
            if (!hooks.contains(entry)) {
                return R();
            }
            return reinterpret_cast<R(*)(void*, Args...)>(hooks[entry].second)(instance, args...);
        }
    private:
        inline static std::unordered_map<void*, std::pair<void*, void*>> hooks;
    };

    class HardBreakPoint {
    public:
        struct BreakPoint {
            int id;
            void* original;
            void* replacement;
        };

        struct DR7 {
            uint32_t raw;

            auto set(const int id, void* address) -> void {
                raw |= 1 << (id * 2);
                raw |= 0b00 << (16 + id * 4);
                raw |= 0b00 << (18 + id * 4);
            }

            auto clear(const int id) -> void {
                raw &= ~(1 << (id * 2));
                raw &= ~(0b11 << (16 + id * 4));
                raw &= ~(0b11 << (18 + id * 4));
            }
        };

        template<typename R, typename... Args>
        static auto create(R (*target)(Args...), R (*replacement)(Args...)) -> bool {
            std::call_once(once_flag, initialize);
            std::unique_lock lock(mutex_);

            for (int i = 0; i < 4; ++i) {
                if (!bp_status_[i]) {
                    bp_status_[i] = true;

                    BreakPoint bp{i, reinterpret_cast<void*>(target), reinterpret_cast<void*>(replacement)};
                    breakpoints_[target] = bp;
                    reverse_map_[replacement] = target;
                    dr7_.set(i, reinterpret_cast<void*>(target));

                    apply_to_all_threads([&](const HANDLE thread) {
                        apply_break_point_to_thread(thread, bp);
                    });

                    return true;
                }
            }
            return false;
        }

        template<typename R, typename... Args>
        static auto remove(R (*target)(Args...)) -> bool {
            std::unique_lock lock(mutex_);

            auto it = breakpoints_.find(target);
            if (it == breakpoints_.end()) {
                return false;
            }

            const BreakPoint& bp = it->second;
            bp_status_[bp.id] = false;
            dr7_.clear(bp.id);
            reverse_map_.erase(bp.replacement);
            breakpoints_.erase(it);

            apply_to_all_threads([&](const HANDLE thread) {
                remove_break_point_from_thread(thread, bp);
            });

            return true;
        }

        template<typename R, typename... Args>
        static auto call(R (*replacement)(Args...), Args... args) -> R {
            std::shared_lock lock(mutex_);

            auto orig = reverse_map_[replacement];
            const auto& bp = breakpoints_[orig];

            HANDLE h_thread = GetCurrentThread();
            remove_break_point_from_thread(h_thread, bp);

            if constexpr (std::is_void_v<R>) {
                reinterpret_cast<R(*)(Args...)>(bp.original)(args...);
            } else {
                R ret = reinterpret_cast<R(*)(Args...)>(bp.original)(args...);
                apply_break_point_to_thread(h_thread, bp);
                return ret;
            }

            apply_break_point_to_thread(h_thread, bp);
            return R();
        }

        inline static DR7 dr7_;

        static auto set_dr() -> bool {
            CONTEXT ctx{};
            GetThreadContext(GetCurrentThread(), &ctx);
            ctx.Dr7 = dr7_.raw;
            return SetThreadContext(GetCurrentThread(), &ctx);
        }
    private:
        inline static std::once_flag once_flag;
        inline static std::bitset<4> bp_status_;
        inline static std::shared_mutex mutex_;
        inline static std::unordered_map<void*, BreakPoint> breakpoints_;
        inline static std::unordered_map<void*, void*> reverse_map_;

        static auto initialize() -> void {
            AddVectoredExceptionHandler(1, vectored_exception_handler);
        }

        class ThreadHandle {
        public:
            explicit ThreadHandle(const DWORD tid) {
                h_thread_ = OpenThread(THREAD_ALL_ACCESS, FALSE, tid);
            }

            ~ThreadHandle() {
                if (h_thread_) {
                    CloseHandle(h_thread_);
                }
            }

            auto valid() const -> bool {
                return h_thread_ != nullptr;
            }

            auto get() const -> HANDLE {
                return h_thread_;
            }

            auto suspend_resume(const std::function<void()>& action) const -> void {
                if (!valid()) {
                    return;
                }
                SuspendThread(h_thread_);
                action();
                ResumeThread(h_thread_);
            }
        private:
            HANDLE h_thread_;
        };

        static auto apply_to_all_threads(const std::function<void(HANDLE)>& _func) -> void {
            const DWORD self = GetCurrentThreadId();
            for (const DWORD tid : get_all_thread_ids()) {
                if (tid == self) {
                    continue;
                }
                ThreadHandle th(tid);
                if (th.valid()) {
                    th.suspend_resume([&] {
                        _func(th.get());
                    });
                }
            }
        }

        static auto get_all_thread_ids() -> std::vector<DWORD> {
            std::vector<DWORD> ids;
            const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE) {
                return ids;
            }

            THREADENTRY32 te{};
            te.dwSize = sizeof(te);
            if (Thread32First(snapshot, &te)) {
                do {
                    if (te.th32OwnerProcessID == GetCurrentProcessId()) {
                        ids.push_back(te.th32ThreadID);
                    }
                } while (Thread32Next(snapshot, &te));
            }
            CloseHandle(snapshot);
            return ids;
        }

        static auto apply_break_point_to_thread(const HANDLE thread, const BreakPoint& bp) -> void {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (!GetThreadContext(thread, &ctx)) {
                return;
            }

            switch (bp.id) {
                case 0:
                    ctx.Dr0 = reinterpret_cast<DWORD_PTR>(bp.original);
                    break;
                case 1:
                    ctx.Dr1 = reinterpret_cast<DWORD_PTR>(bp.original);
                    break;
                case 2:
                    ctx.Dr2 = reinterpret_cast<DWORD_PTR>(bp.original);
                    break;
                case 3:
                    ctx.Dr3 = reinterpret_cast<DWORD_PTR>(bp.original);
                    break;
                default: ;
            }
            ctx.Dr7 = dr7_.raw;
            SetThreadContext(thread, &ctx);
        }

        static auto remove_break_point_from_thread(const HANDLE _thread, const BreakPoint& _bp) -> void {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (!GetThreadContext(_thread, &ctx)) {
                return;
            }

            switch (_bp.id) {
                case 0:
                    ctx.Dr0 = 0;
                    break;
                case 1:
                    ctx.Dr1 = 0;
                    break;
                case 2:
                    ctx.Dr2 = 0;
                    break;
                case 3:
                    ctx.Dr3 = 0;
                    break;
                default: ;
            }
            ctx.Dr7 = dr7_.raw;
            SetThreadContext(_thread, &ctx);
        }

        static auto WINAPI vectored_exception_handler(const PEXCEPTION_POINTERS ep) -> LONG {
            if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
                std::shared_lock lock(mutex_);
                void* addr = ep->ExceptionRecord->ExceptionAddress;
                auto bp = breakpoints_[addr];
                ep->ContextRecord->Rip = reinterpret_cast<DWORD64>(bp.replacement);
                ep->ContextRecord->Dr7 = dr7_.raw;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }
    };

    inline thread_local auto _ = HardBreakPoint::set_dr();
}
