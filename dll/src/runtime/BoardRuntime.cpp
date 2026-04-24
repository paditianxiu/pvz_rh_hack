#include "../../pch.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <vector>

#include "../../Console.hpp"
#include "../../UnityResolve.hpp"
#include <detours/detours.h>

#include "../BoardRuntime.hpp"
#include "common/RuntimeHookCommon.hpp"
#include "ZombieRuntimeInternal.hpp"

namespace board_runtime {
	namespace {
		using MethodHook_t = void (*)(void* _this);

		void* g_boardInstance = nullptr;
		std::mutex g_boardMutex;
		bool g_hooksInstalled = false;
		std::atomic_bool g_freeCDAutoRefreshEnabled = false;
		constexpr float kFreeCDCooldownValue = 99999.0f;

		MethodHook_t g_originalBoardAwake = nullptr;
		MethodHook_t g_originalBoardStart = nullptr;
		MethodHook_t g_originalBoardOnDestroy = nullptr;
		MethodHook_t g_originalMouseLeftClickWithSomeThing = nullptr;

		bool SetAllCardUICooldown(float cooldown) {
			try {
				const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
				if (!assembly) {
					LOG_ERROR("未找到程序集！");
					return false;
				}

				const auto cardUIClass = assembly->Get("CardUI");
				if (!cardUIClass) {
					LOG_ERROR("未找到 CardUI 类！");
					return false;
				}

				constexpr unsigned int kCardUICDOffset = 0x44;
				unsigned int cdOffset = kCardUICDOffset;
				if (const auto cdField = cardUIClass->Get<UnityResolve::Field>("CD")) {
					if (cdField->offset > 0) {
						cdOffset = static_cast<unsigned int>(cdField->offset);
					}
				}
				else {
					LOG_WARNING("未找到 CardUI.CD 字段，回退使用偏移 0x44");
				}

				const auto cardUIInstances = cardUIClass->FindObjectsByType<void*>();
				if (cardUIInstances.empty()) {
					LOG_WARNING("未找到 CardUI 实例！");
					return false;
				}

				size_t updatedCount = 0;
				for (auto cardUIInstance : cardUIInstances) {
					if (!cardUIInstance) {
						continue;
					}

					cardUIClass->SetValue<float>(cardUIInstance, cdOffset, cooldown);
					++updatedCount;
				}

				return updatedCount > 0;
			}
			catch (const std::exception& e) {
				LOG_ERROR(std::format("设置 CardUI.CD 异常: {}", e.what()).c_str());
				return false;
			}
		}

		bool SetBoardBoolField(const char* fieldName, bool enabled) {
			try {
				void* boardInstance = board_runtime::GetBoardInstance();
				if (!boardInstance) {
					LOG_ERROR("未找到 Board 实例！");
					return false;
				}

				const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
				if (!assembly) {
					LOG_ERROR("未找到程序集！");
					return false;
				}

				const auto boardClass = assembly->Get("Board");
				if (!boardClass) {
					LOG_ERROR("未找到 Board 类！");
					return false;
				}

				const auto field = boardClass->Get<UnityResolve::Field>(fieldName);
				if (!field) {
					LOG_ERROR(std::format("未找到 {} 字段！", fieldName).c_str());
					return false;
				}

				boardClass->SetValue(boardInstance, field->name, enabled);
				LOG_INFO(std::format("{} 已设置为: {}，实例: 0x{:X}", fieldName, enabled, reinterpret_cast<uintptr_t>(boardInstance)).c_str());
				return true;
			}
			catch (const std::exception& e) {
				LOG_ERROR(std::format("{} 异常: {}", fieldName, e.what()).c_str());
				return false;
			}
		}

		void HookedBoardAwake(void* _this) {
			{
				std::lock_guard<std::mutex> lock(g_boardMutex);
				g_boardInstance = _this;
			}

			LOG_INFO(std::format("[钩子] 已调用 Board::Awake，this: 0x{:X}", reinterpret_cast<uintptr_t>(_this)).c_str());
			if (g_originalBoardAwake) {
				g_originalBoardAwake(_this);
			}
		}

		void HookedBoardStart(void* _this) {
			{
				std::lock_guard<std::mutex> lock(g_boardMutex);
				g_boardInstance = _this;
			}

			LOG_INFO(std::format("[钩子] 已调用 Board::Start，this: 0x{:X}", reinterpret_cast<uintptr_t>(_this)).c_str());
			if (g_originalBoardStart) {
				g_originalBoardStart(_this);
			}
		}

		void HookedBoardOnDestroy(void* _this) {
			LOG_INFO(std::format("[钩子] 已调用 Board::OnDestroy，this: 0x{:X}", reinterpret_cast<uintptr_t>(_this)).c_str());
			{
				std::lock_guard<std::mutex> lock(g_boardMutex);
				if (g_boardInstance == _this) {
					g_boardInstance = nullptr;
				}
			}

			if (g_originalBoardOnDestroy) {
				g_originalBoardOnDestroy(_this);
			}
		}

		void HookedMouseLeftClickWithSomeThing(void* _this) {
			if (g_originalMouseLeftClickWithSomeThing) {
				g_originalMouseLeftClickWithSomeThing(_this);
			}

			if (g_freeCDAutoRefreshEnabled.load(std::memory_order_acquire)) {
				SetAllCardUICooldown(kFreeCDCooldownValue);
			}
		}

		void ResetBoardRuntimeState() {
			{
				std::lock_guard<std::mutex> lock(g_boardMutex);
				g_boardInstance = nullptr;
			}

			g_originalBoardAwake = nullptr;
			g_originalBoardStart = nullptr;
			g_originalBoardOnDestroy = nullptr;
			g_originalMouseLeftClickWithSomeThing = nullptr;
			g_hooksInstalled = false;
			g_freeCDAutoRefreshEnabled.store(false, std::memory_order_release);
		}
	}

	bool InstallBoardHooks() {
		if (g_hooksInstalled) {
			LOG_WARNING("钩子已安装");
			return true;
		}

		try {
			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				LOG_ERROR("获取 Assembly-CSharp.dll 失败");
				return false;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				LOG_ERROR("获取 Board 类失败");
				return false;
			}
			const auto mouseClass = assembly->Get("Mouse");

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			bool hookSuccess = true;
			hookSuccess = common::AttachMethodHook(boardClass, "Board", "Awake", g_originalBoardAwake, HookedBoardAwake, true) && hookSuccess;
			hookSuccess = common::AttachMethodHook(boardClass, "Board", "Start", g_originalBoardStart, HookedBoardStart, true) && hookSuccess;
			hookSuccess = common::AttachMethodHook(boardClass, "Board", "OnDestroy", g_originalBoardOnDestroy, HookedBoardOnDestroy, false) && hookSuccess;
			hookSuccess = common::AttachMethodHook(mouseClass, "Mouse", "LeftClickWithSomeThing", g_originalMouseLeftClickWithSomeThing, HookedMouseLeftClickWithSomeThing, false) && hookSuccess;

			const auto result = DetourTransactionCommit();
			if (result != NO_ERROR) {
				LOG_ERROR(std::format("提交 Board Detours 事务失败: {}", result).c_str());
				return false;
			}

			const bool zombieHookInstalled = internal::InstallZombieHooks(assembly);
			if (!zombieHookInstalled) {
				LOG_WARNING("Zombie 钩子未安装或部分缺失，继续运行...");
			}

			g_hooksInstalled = true;
			if (!hookSuccess) {
				LOG_WARNING("部分 Board 钩子安装失败，继续运行...");
			}

			LOG_INFO("Board 钩子安装成功");
			return true;
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("InstallBoardHooks 异常: {}", e.what()).c_str());
			return false;
		}
	}

	void UninstallBoardHooks() {
		if (!g_hooksInstalled) {
			internal::ResetZombieHooksState();
			ResetBoardRuntimeState();
			return;
		}

		internal::UninstallZombieHooks();

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());

		if (g_originalBoardAwake) {
			DetourDetach(&(PVOID&)g_originalBoardAwake, HookedBoardAwake);
		}
		if (g_originalBoardStart) {
			DetourDetach(&(PVOID&)g_originalBoardStart, HookedBoardStart);
		}
		if (g_originalBoardOnDestroy) {
			DetourDetach(&(PVOID&)g_originalBoardOnDestroy, HookedBoardOnDestroy);
		}
		if (g_originalMouseLeftClickWithSomeThing) {
			DetourDetach(&(PVOID&)g_originalMouseLeftClickWithSomeThing, HookedMouseLeftClickWithSomeThing);
		}

		const auto result = DetourTransactionCommit();
		if (result != NO_ERROR) {
			LOG_WARNING(std::format("卸载 Board 钩子时提交事务失败: {}", result).c_str());
		}

		ResetBoardRuntimeState();
		LOG_INFO("Board 钩子已卸载");
	}

	void* GetBoardInstance() {
		std::lock_guard<std::mutex> lock(g_boardMutex);
		if (g_boardInstance) {
			return g_boardInstance;
		}

		LOG_WARNING("通过所有方式都未获取到 Board 实例");
		return nullptr;
	}

	void SetFreeCD(bool enabled) {
		g_freeCDAutoRefreshEnabled.store(enabled, std::memory_order_release);
		SetBoardBoolField("freeCD", enabled);

		if (enabled) {
			SetAllCardUICooldown(kFreeCDCooldownValue);
		}
	}

	void SetRandomCard(bool enabled) {
		SetBoardBoolField("randomCard", enabled);
	}

	void SetRightPutPot(bool enabled) {
		try {
			do {
				void* boardInstance = GetBoardInstance();
				if (!boardInstance) {
					LOG_ERROR("未找到 Board 实例！");
					break;
				}

				const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
				if (!assembly) {
					LOG_ERROR("未找到程序集！");
					break;
				}

				const auto boardClass = assembly->Get("Board");
				if (!boardClass) {
					LOG_ERROR("未找到 Board 类！");
					break;
				}

				auto boardTagField = boardClass->Get<UnityResolve::Field>("BoardTag");
				if (!boardTagField) {
					boardTagField = boardClass->Get<UnityResolve::Field>("boardTag");
				}
				if (!boardTagField) {
					LOG_ERROR("未找到 BoardTag 字段！");
					break;
				}

				UnityResolve::Class* boardTagClass = assembly->Get("BoardTag");
				if (!boardTagClass) {
					for (const auto klass : assembly->classes) {
						if (klass && klass->Get<UnityResolve::Field>("pvpScaryPot")) {
							boardTagClass = klass;
							break;
						}
					}
				}
				if (!boardTagClass) {
					LOG_ERROR("未找到 BoardTag 类！");
					break;
				}

				const auto pvpScaryPotField = boardTagClass->Get<UnityResolve::Field>("pvpScaryPot");
				const auto isScaryPotField = boardTagClass->Get<UnityResolve::Field>("isScaryPot");
				if (!pvpScaryPotField || !isScaryPotField) {
					LOG_ERROR("未找到 BoardTag.pvpScaryPot 或 BoardTag.isScaryPot 字段！");
					break;
				}

				auto* const boardTagInstance = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(boardInstance) + boardTagField->offset);
				boardTagClass->SetValue(boardTagInstance, pvpScaryPotField->name, enabled);
				boardTagClass->SetValue(boardTagInstance, isScaryPotField->name, enabled);
				LOG_INFO(std::format("BoardTag 字段已设置为: {}，实例: 0x{:X}", enabled, reinterpret_cast<uintptr_t>(boardInstance)).c_str());
			} while (false);
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("设置 BoardTag 字段异常: {}", e.what()).c_str());
		}

		SetBoardBoolField("rightPutPot", enabled);
	}

	bool GetFreeCD() {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				LOG_ERROR("GetFreeCD 未找到 Board 实例！");
				return false;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				return false;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				return false;
			}

			const auto freeCD = boardClass->Get<UnityResolve::Field>("freeCD");
			if (freeCD) {
				return boardClass->GetValue<int>(boardInstance, freeCD->name) == 1;
			}
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("GetFreeCD 异常: {}", e.what()).c_str());
		}

		return false;
	}

	int GetBoardWave() {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				return -1;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				return -1;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				return -1;
			}

			const auto waveField = boardClass->Get<UnityResolve::Field>("theWave");
			if (waveField) {
				return boardClass->GetValue<int>(boardInstance, waveField->name);
			}
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("GetBoardWave 异常: {}", e.what()).c_str());
		}

		return -1;
	}

	int GetSun() {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				return -1;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				return -1;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				return -1;
			}

			const auto sunField = boardClass->Get<UnityResolve::Field>("theSun");
			if (sunField) {
				return boardClass->GetValue<int>(boardInstance, sunField->name);
			}
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("GetSun 异常: {}", e.what()).c_str());
		}

		return -1;
	}

	void SetSun(int sunCount) {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				return;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				return;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				return;
			}

			const auto sunField = boardClass->Get<UnityResolve::Field>("theSun");
			if (!sunField) {
				return;
			}

			boardClass->SetValue(boardInstance, sunField->name, sunCount);
			LOG_INFO(std::format("阳光已设置为: {}", sunCount).c_str());
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("SetSun 异常: {}", e.what()).c_str());
		}
	}
}
