#include "../pch.h"

#include <cstdint>
#include <format>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "../Console.hpp"
#include "../UnityResolve.hpp"
#include <detours/detours.h>

#include "BoardRuntime.hpp"

namespace board_runtime {
	namespace {
		using MethodHook_t = void (*)(void* _this);
		using GetTransform_t = void* (*)(void* _this);
		using GetPosition_t = UnityResolve::UnityType::Vector3 (*)(void* _this);

		void* g_boardInstance = nullptr;
		std::mutex g_boardMutex;
		std::unordered_set<void*> g_zombieInstances;
		std::mutex g_zombieMutex;
		bool g_hooksInstalled = false;

		MethodHook_t g_originalBoardAwake = nullptr;
		MethodHook_t g_originalBoardStart = nullptr;
		MethodHook_t g_originalBoardOnDestroy = nullptr;
		MethodHook_t g_originalZombieFixedUpdate = nullptr;
		MethodHook_t g_originalZombieOnDestroy = nullptr;
		GetTransform_t g_getTransform = nullptr;
		GetPosition_t g_getPosition = nullptr;
		UnityResolve::Method* g_getEntityRowMethod = nullptr;
		UnityResolve::Method* g_getEntityColumnMethod = nullptr;

		void CacheBoardInstance(void* instance) {
			std::lock_guard<std::mutex> lock(g_boardMutex);
			g_boardInstance = instance;
		}

		void ClearBoardInstance(void* instance) {
			std::lock_guard<std::mutex> lock(g_boardMutex);
			if (g_boardInstance == instance) {
				g_boardInstance = nullptr;
			}
		}

		void AddZombieInstance(void* instance) {
			if (!instance) {
				return;
			}

			std::lock_guard<std::mutex> lock(g_zombieMutex);
			g_zombieInstances.insert(instance);
		}

		void RemoveZombieInstance(void* instance) {
			if (!instance) {
				return;
			}

			std::lock_guard<std::mutex> lock(g_zombieMutex);
			g_zombieInstances.erase(instance);
		}

		void HookedBoardAwake(void* _this) {
			CacheBoardInstance(_this);
			LOG_INFO(std::format("[钩子] 已调用 Board::Awake，this: 0x{:X}", reinterpret_cast<uintptr_t>(_this)).c_str());

			if (g_originalBoardAwake) {
				g_originalBoardAwake(_this);
			}
		}

		void HookedBoardStart(void* _this) {
			CacheBoardInstance(_this);
			LOG_INFO(std::format("[钩子] 已调用 Board::Start，this: 0x{:X}", reinterpret_cast<uintptr_t>(_this)).c_str());

			if (g_originalBoardStart) {
				g_originalBoardStart(_this);
			}
		}

		void HookedBoardOnDestroy(void* _this) {
			LOG_INFO(std::format("[钩子] 已调用 Board::OnDestroy，this: 0x{:X}", reinterpret_cast<uintptr_t>(_this)).c_str());
			ClearBoardInstance(_this);

			if (g_originalBoardOnDestroy) {
				g_originalBoardOnDestroy(_this);
			}
		}

		void HookedZombieFixedUpdate(void* _this) {
			AddZombieInstance(_this);

			if (g_originalZombieFixedUpdate) {
				g_originalZombieFixedUpdate(_this);
			}
		}

		void HookedZombieOnDestroy(void* _this) {
			RemoveZombieInstance(_this);

			if (g_originalZombieOnDestroy) {
				g_originalZombieOnDestroy(_this);
			}
		}

		bool ResolveZombiePositionMethods() {
			if (!g_getTransform || !g_getPosition) {
				const auto coreModule = UnityResolve::Get("UnityEngine.CoreModule.dll");
				if (!coreModule) {
					return false;
				}

				const auto componentClass = coreModule->Get("Component", "UnityEngine");
				const auto transformClass = coreModule->Get("Transform", "UnityEngine");
				if (!componentClass || !transformClass) {
					return false;
				}

				const auto getTransformMethod = componentClass->Get<UnityResolve::Method>("get_transform");
				const auto getPositionMethod = transformClass->Get<UnityResolve::Method>("get_position");
				if (!getTransformMethod || !getTransformMethod->function || !getPositionMethod || !getPositionMethod->function) {
					return false;
				}

				g_getTransform = reinterpret_cast<GetTransform_t>(getTransformMethod->function);
				g_getPosition = reinterpret_cast<GetPosition_t>(getPositionMethod->function);
			}

			if (!g_getEntityRowMethod || !g_getEntityColumnMethod) {
				const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
				const auto playerClass = assembly ? assembly->Get("Player") : nullptr;
				g_getEntityRowMethod = playerClass->Get<UnityResolve::Method>("get_Row");
				g_getEntityColumnMethod = playerClass->Get<UnityResolve::Method>("get_Column");
			}

			return true;
		}

		void ResetRuntimeState() {
			{
				std::lock_guard<std::mutex> lock(g_boardMutex);
				g_boardInstance = nullptr;
			}
			{
				std::lock_guard<std::mutex> lock(g_zombieMutex);
				g_zombieInstances.clear();
			}

			g_originalBoardAwake = nullptr;
			g_originalBoardStart = nullptr;
			g_originalBoardOnDestroy = nullptr;
			g_originalZombieFixedUpdate = nullptr;
			g_originalZombieOnDestroy = nullptr;
			g_getTransform = nullptr;
			g_getPosition = nullptr;
			g_getEntityRowMethod = nullptr;
			g_getEntityColumnMethod = nullptr;
			g_hooksInstalled = false;
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
			const auto zombieClass = assembly->Get("Zombie");

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			auto attachHook = [&](UnityResolve::Class* klass, const char* className, const char* methodName, MethodHook_t& original, MethodHook_t hook, bool required) {
				if (!klass) {
					if (required) {
						LOG_ERROR(std::format("未找到 {} 类", className).c_str());
						return false;
					}

					LOG_WARNING(std::format("未找到 {} 类，跳过挂钩", className).c_str());
					return true;
				}

				const auto method = klass->Get<UnityResolve::Method>(methodName);
				if (!method || !method->function) {
					if (required) {
						LOG_ERROR(std::format("未找到 {}::{} 方法或函数指针为空", className, methodName).c_str());
						return false;
					}

					LOG_WARNING(std::format("未找到 {}::{} 方法或函数指针为空", className, methodName).c_str());
					return true;
				}

				LOG_INFO(std::format("找到 {}::{}，地址: 0x{:X}", className, methodName, reinterpret_cast<uintptr_t>(method->function)).c_str());

				original = reinterpret_cast<MethodHook_t>(method->function);
				const auto result = DetourAttach(&(PVOID&)original, hook);
				if (result != NO_ERROR) {
					if (required) {
						LOG_ERROR(std::format("为 {}::{} 挂钩失败: {}", className, methodName, result).c_str());
						return false;
					}

					LOG_WARNING(std::format("为 {}::{} 挂钩失败: {}", className, methodName, result).c_str());
					return true;
				}

				LOG_INFO(std::format("{}::{} 挂钩成功", className, methodName).c_str());
				return true;
				};

			bool hookSuccess = true;
			hookSuccess = attachHook(boardClass, "Board", "Awake", g_originalBoardAwake, HookedBoardAwake, true) && hookSuccess;
			hookSuccess = attachHook(boardClass, "Board", "Start", g_originalBoardStart, HookedBoardStart, true) && hookSuccess;
			hookSuccess = attachHook(boardClass, "Board", "OnDestroy", g_originalBoardOnDestroy, HookedBoardOnDestroy, false) && hookSuccess;
			hookSuccess = attachHook(zombieClass, "Zombie", "FixedUpdate", g_originalZombieFixedUpdate, HookedZombieFixedUpdate, false) && hookSuccess;
			hookSuccess = attachHook(zombieClass, "Zombie", "OnDestroy", g_originalZombieOnDestroy, HookedZombieOnDestroy, false) && hookSuccess;

			const auto result = DetourTransactionCommit();
			if (result != NO_ERROR) {
				LOG_ERROR(std::format("提交 Detours 事务失败: {}", result).c_str());
				return false;
			}
			if (!ResolveZombiePositionMethods()) {
				LOG_WARNING("解析僵尸坐标方法失败，坐标功能可能不可用");
			}

			g_hooksInstalled = true;
			if (!hookSuccess) {
				LOG_WARNING("部分钩子安装失败，继续运行...");
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
			ResetRuntimeState();
			return;
		}

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
		if (g_originalZombieFixedUpdate) {
			DetourDetach(&(PVOID&)g_originalZombieFixedUpdate, HookedZombieFixedUpdate);
		}
		if (g_originalZombieOnDestroy) {
			DetourDetach(&(PVOID&)g_originalZombieOnDestroy, HookedZombieOnDestroy);
		}

		DetourTransactionCommit();
		ResetRuntimeState();

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

	std::vector<ZombieCoordinate> GetZombieCoordinates() {
		std::vector<void*> snapshot;
		{
			std::lock_guard<std::mutex> lock(g_zombieMutex);
			snapshot.reserve(g_zombieInstances.size());
			for (const auto zombie : g_zombieInstances) {
				snapshot.push_back(zombie);
			}
		}

		if (snapshot.empty()) {
			return {};
		}

		if (!ResolveZombiePositionMethods()) {
			return {};
		}

		std::vector<ZombieCoordinate> coordinates;
		coordinates.reserve(snapshot.size());
		for (const auto zombie : snapshot) {
			if (!zombie) {
				continue;
			}

			const auto transform = g_getTransform(zombie);
			if (!transform) {
				continue;
			}

			const auto position = g_getPosition(transform);
			auto mainCamera = UnityResolve::UnityType::Camera::GetMain();
			if (!mainCamera) {
				continue;
			}
			const auto screenPos = mainCamera->WorldToScreenPoint(position);
			int row = -1;
			int column = -1;
			if (g_getEntityRowMethod) {
				try {
					row = g_getEntityRowMethod->Invoke<int>(zombie);
				}
				catch (...) {}
			}
			if (g_getEntityColumnMethod) {
				try {
					column = g_getEntityColumnMethod->Invoke<int>(zombie);
				}
				catch (...) {}
			}

			coordinates.push_back({
				reinterpret_cast<std::uintptr_t>(zombie),
				screenPos.x,
				screenPos.y,
				screenPos.z,
				row,
				column
				});
		}

		return coordinates;
	}

	void SetFreeCD(bool enabled) {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				LOG_ERROR("未找到 Board 实例！");
				return;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				LOG_ERROR("未找到程序集！");
				return;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				LOG_ERROR("未找到 Board 类！");
				return;
			}

			const auto freeCD = boardClass->Get<UnityResolve::Field>("freeCD");
			if (!freeCD) {
				LOG_ERROR("未找到 freeCD 字段！");
				return;
			}
			boardClass->SetValue(boardInstance, freeCD->name, enabled ? 1 : 0);
			LOG_INFO(std::format("freeCD 已设置为: {}，实例: 0x{:X}", enabled, reinterpret_cast<uintptr_t>(boardInstance)).c_str());
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("freeCD 异常: {}", e.what()).c_str());
		}
	}


	void SetRandomCard(bool enabled) {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				LOG_ERROR("未找到 Board 实例！");
				return;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				LOG_ERROR("未找到程序集！");
				return;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				LOG_ERROR("未找到 Board 类！");
				return;
			}

			const auto randomCard = boardClass->Get<UnityResolve::Field>("randomCard");
			if (!randomCard) {
				LOG_ERROR("未找到 randomCard 字段！");
				return;
			}
			boardClass->SetValue(boardInstance, randomCard->name, enabled ? 1 : 0);
			LOG_INFO(std::format("randomCard 已设置为: {}，实例: 0x{:X}", enabled, reinterpret_cast<uintptr_t>(boardInstance)).c_str());
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("randomCard 异常: {}", e.what()).c_str());
		}
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
