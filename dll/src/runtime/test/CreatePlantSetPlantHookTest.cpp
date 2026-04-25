#include "../../../pch.h"

#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "../../../Console.hpp"
#include "../../../UnityResolve.hpp"
#include <detours/detours.h>

#include "CreatePlantSetPlantHookTest.hpp"

namespace board_runtime {
	namespace {
		using SetPlantHook_t = void* (UNITY_CALLING_CONVENTION*)(
			void* _this,
			int32_t newColumn,
			int32_t newRow,
			int32_t theSeedType,
			void* targetPlant,
			UnityResolve::UnityType::Vector2 puffV,
			bool isFreeSet,
			bool withEffect,
			void* hitPlant,
			void* methodInfo
		);

		bool g_createPlantSetPlantHookInstalled = false;
		SetPlantHook_t g_originalCreatePlantSetPlant = nullptr;

		void* HookedCreatePlantSetPlant(
			void* _this,
			int32_t newColumn,
			int32_t newRow,
			int32_t theSeedType,
			void* targetPlant,
			UnityResolve::UnityType::Vector2 puffV,
			bool isFreeSet,
			bool withEffect,
			void* hitPlant,
			void* methodInfo
		) {
			LOG_INFO(std::format(
				"[TestHook][CreatePlant::SetPlant] this=0x{:X}, newColumn={}, newRow={}, theSeedType={}, targetPlant=0x{:X}, puffV=({:.3f},{:.3f}), isFreeSet={}, withEffect={}, hitPlant=0x{:X}, methodInfo=0x{:X}",
				reinterpret_cast<std::uintptr_t>(_this),
				newColumn,
				newRow,
				theSeedType,
				reinterpret_cast<std::uintptr_t>(targetPlant),
				puffV.x,
				puffV.y,
				isFreeSet,
				withEffect,
				reinterpret_cast<std::uintptr_t>(hitPlant),
				reinterpret_cast<std::uintptr_t>(methodInfo)
			).c_str());

			if (!g_originalCreatePlantSetPlant) {
				LOG_WARNING("[TestHook][CreatePlant::SetPlant] 原函数指针为空，跳过原调用");
				return nullptr;
			}

	
			const auto result = g_originalCreatePlantSetPlant(
				_this,
				newColumn,
				newRow,
				theSeedType,
				targetPlant,
				puffV,
				isFreeSet,
				withEffect,
				hitPlant,
				methodInfo
			);

			LOG_INFO(std::format(
				"[TestHook][CreatePlant::SetPlant] return=0x{:X}",
				reinterpret_cast<std::uintptr_t>(result)
			).c_str());

			return result;
		}

		void ResetCreatePlantSetPlantHookState() {
			g_originalCreatePlantSetPlant = nullptr;
			g_createPlantSetPlantHookInstalled = false;
		}
	}

	namespace internal {
		bool CreatePlantSetPlantHookTest::Install(UnityResolve::Assembly* assembly) {
			if (g_createPlantSetPlantHookInstalled) {
				return true;
			}

			try {
				const auto resolvedAssembly = assembly ? assembly : UnityResolve::Get("Assembly-CSharp.dll");
				if (!resolvedAssembly) {
					LOG_ERROR("CreatePlant::SetPlant 测试钩子安装失败：未找到程序集");
					return false;
				}

				const auto createPlantClass = resolvedAssembly->Get("CreatePlant");
				if (!createPlantClass) {
					LOG_WARNING("未找到 CreatePlant 类，跳过 SetPlant 测试钩子");
					return false;
				}

				const std::vector<std::string> expectedArgs = {
					"System.Int32",
					"System.Int32",
					"PlantType",
					"Plant",
					"UnityEngine.Vector2",
					"System.Boolean",
					"System.Boolean",
					"Plant"
				};

				auto method = createPlantClass->Get<UnityResolve::Method>("SetPlant", expectedArgs);
				if (!method) {
					method = createPlantClass->Get<UnityResolve::Method>("SetPlant");
				}
				if (!method || !method->function) {
					LOG_WARNING("未找到 CreatePlant::SetPlant 方法或函数指针为空，跳过测试钩子");
					return false;
				}

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());

				g_originalCreatePlantSetPlant = reinterpret_cast<SetPlantHook_t>(method->function);
				const auto attachResult = DetourAttach(&(PVOID&)g_originalCreatePlantSetPlant, HookedCreatePlantSetPlant);
				const bool hookSuccess = (attachResult == NO_ERROR);
				if (!hookSuccess) {
					LOG_WARNING(std::format("CreatePlant::SetPlant 测试钩子挂载失败: {}", attachResult).c_str());
				}

				const auto result = DetourTransactionCommit();
				if (result != NO_ERROR) {
					LOG_ERROR(std::format("提交 CreatePlant::SetPlant 测试钩子事务失败: {}", result).c_str());
					return false;
				}

				g_createPlantSetPlantHookInstalled = hookSuccess;
				if (!hookSuccess) {
					LOG_WARNING("CreatePlant::SetPlant 测试钩子未安装，继续运行...");
					return false;
				}

				LOG_INFO("CreatePlant::SetPlant 测试钩子安装完成");
				return true;
			}
			catch (const std::exception& e) {
				LOG_ERROR(std::format("Install CreatePlant::SetPlant 测试钩子异常: {}", e.what()).c_str());
				return false;
			}
		}

		void CreatePlantSetPlantHookTest::Uninstall() {
			if (!g_createPlantSetPlantHookInstalled) {
				ResetCreatePlantSetPlantHookState();
				return;
			}

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			if (g_originalCreatePlantSetPlant) {
				DetourDetach(&(PVOID&)g_originalCreatePlantSetPlant, HookedCreatePlantSetPlant);
			}

			const auto result = DetourTransactionCommit();
			if (result != NO_ERROR) {
				LOG_WARNING(std::format("卸载 CreatePlant::SetPlant 测试钩子时提交事务失败: {}", result).c_str());
			}

			ResetCreatePlantSetPlantHookState();
			LOG_INFO("CreatePlant::SetPlant 测试钩子已卸载");
		}

		void CreatePlantSetPlantHookTest::Reset() {
			ResetCreatePlantSetPlantHookState();
		}
	}
}
