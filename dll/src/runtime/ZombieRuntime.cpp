#include "../../pch.h"

#include <cstdint>
#include <format>
#include <mutex>
#include <unordered_set>
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
		using GetTransform_t = void* (*)(void* _this);
		using GetPosition_t = UnityResolve::UnityType::Vector3 (*)(void* _this);

		std::unordered_set<void*> g_zombieInstances;
		std::mutex g_zombieMutex;
		bool g_zombieHooksInstalled = false;

		MethodHook_t g_originalZombieFixedUpdate = nullptr;
		MethodHook_t g_originalZombieOnDestroy = nullptr;
		GetTransform_t g_getTransform = nullptr;
		GetPosition_t g_getPosition = nullptr;
		UnityResolve::Method* g_getEntityRowMethod = nullptr;
		UnityResolve::Method* g_getEntityColumnMethod = nullptr;

		void HookedZombieFixedUpdate(void* _this) {
			if (_this) {
				std::lock_guard<std::mutex> lock(g_zombieMutex);
				g_zombieInstances.insert(_this);
			}

			if (g_originalZombieFixedUpdate) {
				g_originalZombieFixedUpdate(_this);
			}
		}

		void HookedZombieOnDestroy(void* _this) {
			if (_this) {
				std::lock_guard<std::mutex> lock(g_zombieMutex);
				g_zombieInstances.erase(_this);
			}

			if (g_originalZombieOnDestroy) {
				g_originalZombieOnDestroy(_this);
			}
		}

		bool ResolveZombiePositionMethods(UnityResolve::Assembly* assembly = nullptr) {
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
				const auto resolvedAssembly = assembly ? assembly : UnityResolve::Get("Assembly-CSharp.dll");
				const auto playerClass = resolvedAssembly ? resolvedAssembly->Get("Player") : nullptr;
				if (playerClass) {
					g_getEntityRowMethod = playerClass->Get<UnityResolve::Method>("get_Row");
					g_getEntityColumnMethod = playerClass->Get<UnityResolve::Method>("get_Column");
				}
			}

			return true;
		}

		void ResetZombieRuntimeState() {
			{
				std::lock_guard<std::mutex> lock(g_zombieMutex);
				g_zombieInstances.clear();
			}

			g_originalZombieFixedUpdate = nullptr;
			g_originalZombieOnDestroy = nullptr;
			g_getTransform = nullptr;
			g_getPosition = nullptr;
			g_getEntityRowMethod = nullptr;
			g_getEntityColumnMethod = nullptr;
			g_zombieHooksInstalled = false;
		}
	}

	namespace internal {
		bool InstallZombieHooks(UnityResolve::Assembly* assembly) {
			if (g_zombieHooksInstalled) {
				return true;
			}

			try {
				const auto resolvedAssembly = assembly ? assembly : UnityResolve::Get("Assembly-CSharp.dll");
				if (!resolvedAssembly) {
					LOG_ERROR("获取 Assembly-CSharp.dll 失败");
					return false;
				}

				const auto zombieClass = resolvedAssembly->Get("Zombie");
				if (!zombieClass) {
					LOG_WARNING("未找到 Zombie 类，跳过 Zombie 钩子");
					return false;
				}

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());

				bool hookSuccess = true;
				hookSuccess = common::AttachMethodHook(zombieClass, "Zombie", "FixedUpdate", g_originalZombieFixedUpdate, HookedZombieFixedUpdate, false) && hookSuccess;
				hookSuccess = common::AttachMethodHook(zombieClass, "Zombie", "OnDestroy", g_originalZombieOnDestroy, HookedZombieOnDestroy, false) && hookSuccess;

				const auto result = DetourTransactionCommit();
				if (result != NO_ERROR) {
					LOG_ERROR(std::format("提交 Zombie Detours 事务失败: {}", result).c_str());
					return false;
				}

				if (!ResolveZombiePositionMethods(resolvedAssembly)) {
					LOG_WARNING("解析僵尸坐标方法失败，坐标功能可能不可用");
				}

				g_zombieHooksInstalled = true;
				if (!hookSuccess) {
					LOG_WARNING("部分 Zombie 钩子安装失败，继续运行...");
				}

				LOG_INFO("Zombie 钩子安装完成");
				return true;
			}
			catch (const std::exception& e) {
				LOG_ERROR(std::format("InstallZombieHooks 异常: {}", e.what()).c_str());
				return false;
			}
		}

		void UninstallZombieHooks() {
			if (!g_zombieHooksInstalled) {
				ResetZombieRuntimeState();
				return;
			}

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			if (g_originalZombieFixedUpdate) {
				DetourDetach(&(PVOID&)g_originalZombieFixedUpdate, HookedZombieFixedUpdate);
			}
			if (g_originalZombieOnDestroy) {
				DetourDetach(&(PVOID&)g_originalZombieOnDestroy, HookedZombieOnDestroy);
			}

			const auto result = DetourTransactionCommit();
			if (result != NO_ERROR) {
				LOG_WARNING(std::format("卸载 Zombie 钩子时提交事务失败: {}", result).c_str());
			}

			ResetZombieRuntimeState();
			LOG_INFO("Zombie 钩子已卸载");
		}

		void ResetZombieHooksState() {
			ResetZombieRuntimeState();
		}
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

			std::string name;
			try {
				auto* const component = reinterpret_cast<UnityResolve::UnityType::Component*>(zombie);
				if (component) {
					auto* const gameObject = component->GetGameObject();
					if (gameObject && gameObject->GetName()) {
						name = gameObject->GetName()->ToString();
					}
				}
			}
			catch (...) {}

			ZombieCoordinate coordinate{};
			coordinate.instance = reinterpret_cast<std::uintptr_t>(zombie);
			coordinate.x = screenPos.x;
			coordinate.y = screenPos.y;
			coordinate.z = screenPos.z;
			coordinate.row = row;
			coordinate.column = column;
			coordinate.name = name;
			coordinates.push_back(coordinate);
		}

		return coordinates;
	}
}
