#include "../../pch.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <format>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include "../../Console.hpp"
#include "../../UnityResolve.hpp"
#include <detours/detours.h>

#include "../BoardRuntime.hpp"
#include "common/RuntimeHookCommon.hpp"
#include "test/CreatePlantSetPlantHookTest.hpp"
#include "ZombieRuntimeInternal.hpp"

namespace board_runtime {
	namespace {
		using MethodHook_t = void (*)(void* _this);
		using Update_t = void(UNITY_CALLING_CONVENTION*)(void* _this);

		std::mutex g_boardMutex;
		std::mutex g_mainThreadMutex;
		std::queue<std::function<void()>> g_mainThreadTasks;
		bool g_hooksInstalled = false;
		std::atomic_bool g_freeCDAutoRefreshEnabled = false;
		std::atomic_bool g_mainThreadDispatcherInstalled = false;
		constexpr float kFreeCDCooldownValue = 99999.0f;

		MethodHook_t g_originalMouseLeftClickWithSomeThing = nullptr;
		Update_t g_originalBoardUpdate = nullptr;

		std::string EscapeJsonString(std::string_view text) {
			std::string escaped;
			escaped.reserve(text.size() + 8);
			for (const unsigned char ch : text) {
				switch (ch) {
				case '\"':
					escaped.append("\\\"");
					break;
				case '\\':
					escaped.append("\\\\");
					break;
				case '\b':
					escaped.append("\\b");
					break;
				case '\f':
					escaped.append("\\f");
					break;
				case '\n':
					escaped.append("\\n");
					break;
				case '\r':
					escaped.append("\\r");
					break;
				case '\t':
					escaped.append("\\t");
					break;
				default:
					if (ch < 0x20) {
						escaped.append(std::format("\\u{:04X}", static_cast<uint32_t>(ch)));
					}
					else {
						escaped.push_back(static_cast<char>(ch));
					}
					break;
				}
			}
			return escaped;
		}

		template <typename T>
		bool TryReadFieldValue(const UnityResolve::Field* field, void* boardInstance, T& outValue) {
			if (!field) {
				return false;
			}

			try {
				if (field->static_field) {
					field->GetStaticValue(&outValue);
					return true;
				}

				if (!boardInstance || field->offset < 0) {
					return false;
				}

				outValue = *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(boardInstance) + static_cast<std::uintptr_t>(field->offset));
				return true;
			}
			catch (...) {
				return false;
			}
		}

		std::string SerializeBoardFieldValueAsJson(const UnityResolve::Field* field, void* boardInstance) {
			if (!field || !field->type) {
				return "null";
			}

			const std::string& typeName = field->type->name;

			if (typeName == "System.Boolean") {
				bool value = false;
				if (TryReadFieldValue(field, boardInstance, value)) {
					return value ? "true" : "false";
				}
				return "null";
			}

			if (typeName == "System.Int32") {
				int32_t value = 0;
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::to_string(value);
				}
				return "null";
			}

			if (typeName == "System.UInt32") {
				uint32_t value = 0;
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::to_string(value);
				}
				return "null";
			}

			if (typeName == "System.Int64") {
				int64_t value = 0;
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::to_string(value);
				}
				return "null";
			}

			if (typeName == "System.UInt64") {
				uint64_t value = 0;
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::to_string(value);
				}
				return "null";
			}

			if (typeName == "System.Single") {
				float value = 0.0f;
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::format("{:.6f}", value);
				}
				return "null";
			}

			if (typeName == "System.Double") {
				double value = 0.0;
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::format("{:.6f}", value);
				}
				return "null";
			}

			if (typeName == "System.String") {
				UnityResolve::UnityType::String* value = nullptr;
				if (TryReadFieldValue(field, boardInstance, value) && value) {
					return std::format("\"{}\"", EscapeJsonString(value->ToString()));
				}
				return "null";
			}

			if (typeName == "UnityEngine.Vector2") {
				UnityResolve::UnityType::Vector2 value{};
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::format("{{\"x\":{:.6f},\"y\":{:.6f}}}", value.x, value.y);
				}
				return "null";
			}

			if (typeName == "UnityEngine.Vector3") {
				UnityResolve::UnityType::Vector3 value{};
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::format("{{\"x\":{:.6f},\"y\":{:.6f},\"z\":{:.6f}}}", value.x, value.y, value.z);
				}
				return "null";
			}

			if (typeName == "UnityEngine.Vector4") {
				UnityResolve::UnityType::Vector4 value{};
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::format("{{\"x\":{:.6f},\"y\":{:.6f},\"z\":{:.6f},\"w\":{:.6f}}}", value.x, value.y, value.z, value.w);
				}
				return "null";
			}

			if (typeName == "UnityEngine.Quaternion") {
				UnityResolve::UnityType::Quaternion value{};
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::format("{{\"x\":{:.6f},\"y\":{:.6f},\"z\":{:.6f},\"w\":{:.6f}}}", value.x, value.y, value.z, value.w);
				}
				return "null";
			}

			if (typeName == "UnityEngine.Color") {
				UnityResolve::UnityType::Color value(0.0f, 0.0f, 0.0f, 0.0f);
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::format("{{\"r\":{:.6f},\"g\":{:.6f},\"b\":{:.6f},\"a\":{:.6f}}}", value.r, value.g, value.b, value.a);
				}
				return "null";
			}

			if (typeName == "UnityEngine.Rect") {
				UnityResolve::UnityType::Rect value{};
				if (TryReadFieldValue(field, boardInstance, value)) {
					return std::format("{{\"x\":{:.6f},\"y\":{:.6f},\"width\":{:.6f},\"height\":{:.6f}}}", value.fX, value.fY, value.fWidth, value.fHeight);
				}
				return "null";
			}

			std::uintptr_t rawPointer = 0;
			if (TryReadFieldValue(field, boardInstance, rawPointer)) {
				return std::format("\"0x{:X}\"", rawPointer);
			}

			return "null";
		}

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

		void RunOnMainThread(std::function<void()> fn) {
			std::lock_guard<std::mutex> lock(g_mainThreadMutex);
			g_mainThreadTasks.push(std::move(fn));
		}

		void UNITY_CALLING_CONVENTION HookedBoardUpdate(void* _this) {
			if (g_originalBoardUpdate) {
				g_originalBoardUpdate(_this);
			}

			std::queue<std::function<void()>> tasks;
			{
				std::lock_guard<std::mutex> lock(g_mainThreadMutex);
				std::swap(tasks, g_mainThreadTasks);
			}

			while (!tasks.empty()) {
				try {
					tasks.front()();
				}
				catch (const std::exception& e) {
					LOG_ERROR(std::format("MainThread Task Exception: {}", e.what()).c_str());
				}
				catch (...) {
					LOG_ERROR("MainThread Task Unknown Exception");
				}
				tasks.pop();
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
				std::lock_guard<std::mutex> lock(g_mainThreadMutex);
				std::queue<std::function<void()>> emptyTasks;
				std::swap(g_mainThreadTasks, emptyTasks);
			}

			g_originalBoardUpdate = nullptr;
			g_originalMouseLeftClickWithSomeThing = nullptr;
			g_hooksInstalled = false;
			g_freeCDAutoRefreshEnabled.store(false, std::memory_order_release);
			g_mainThreadDispatcherInstalled.store(false, std::memory_order_release);
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
			const bool mainThreadDispatcherHooked = common::AttachMethodHook(
				boardClass,
				"Board",
				"Update",
				g_originalBoardUpdate,
				HookedBoardUpdate,
				true
			);
			hookSuccess = mainThreadDispatcherHooked && hookSuccess;
			hookSuccess = common::AttachMethodHook(mouseClass, "Mouse", "LeftClickWithSomeThing", g_originalMouseLeftClickWithSomeThing, HookedMouseLeftClickWithSomeThing, false) && hookSuccess;

			const auto result = DetourTransactionCommit();
			if (result != NO_ERROR) {
				LOG_ERROR(std::format("提交 Board Detours 事务失败: {}", result).c_str());
				return false;
			}
			g_mainThreadDispatcherInstalled.store(mainThreadDispatcherHooked, std::memory_order_release);
			if (!mainThreadDispatcherHooked) {
				LOG_WARNING("MainThread Dispatcher 安装失败，CreatePlant 将回退到当前线程执行");
			}

			const bool zombieHookInstalled = internal::InstallZombieHooks(assembly);
			if (!zombieHookInstalled) {
				LOG_WARNING("Zombie 钩子未安装或部分缺失，继续运行...");
			}
			/*const bool createPlantSetPlantHookInstalled = internal::CreatePlantSetPlantHookTest::Install(assembly);
			if (!createPlantSetPlantHookInstalled) {
				LOG_WARNING("CreatePlant::SetPlant 测试钩子未安装或部分缺失，继续运行...");
			}*/

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
			//internal::CreatePlantSetPlantHookTest::Reset();
			internal::ResetZombieHooksState();
			ResetBoardRuntimeState();
			return;
		}

		//internal::CreatePlantSetPlantHookTest::Uninstall();
		internal::UninstallZombieHooks();

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());

		if (g_originalBoardUpdate) {
			DetourDetach(&(PVOID&)g_originalBoardUpdate, HookedBoardUpdate);
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

		const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
		if (!assembly) {
			LOG_WARNING("未找到 Assembly-CSharp.dll 程序集");
			return nullptr;
		}

		const auto boardClass = assembly->Get("Board");
		if (!boardClass) {
			LOG_WARNING("未找到 Board 类");
			return nullptr;
		}

		const auto instances = boardClass->FindObjectsByType<void*>();
		if (!instances.empty() && instances[0]) {
			return instances[0];
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
	void CreateFireLine(int theFireRow) {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				LOG_ERROR("CreateFireLine 未找到 Board 实例！");
				return;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				LOG_ERROR("CreateFireLine 未找到程序集！");
				return;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				LOG_ERROR("CreateFireLine 未找到 Board 类！");
				return;
			}

			const auto createFireLineMethod = boardClass->Get<UnityResolve::Method>("CreateFireLine");
			if (!createFireLineMethod) {
				LOG_ERROR("CreateFireLine 未找到 Board::CreateFireLine 方法！");
				return;
			}

			auto task = [=]() {
				createFireLineMethod->Invoke<void>(
					boardInstance,
					theFireRow
				);
			};

			if (g_mainThreadDispatcherInstalled.load(std::memory_order_acquire)) {
				RunOnMainThread(std::move(task));
				return;
			}

			LOG_WARNING("MainThread Dispatcher 未安装");
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("CreateFireLine 异常: {}", e.what()).c_str());
		}
	}


	bool StartNextRound() {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				LOG_ERROR("StartNextRound 未找到 Board 实例！");
				return false;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				LOG_ERROR("StartNextRound 未找到程序集！");
				return false;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				LOG_ERROR("StartNextRound 未找到 Board 类！");
				return false;
			}

			const auto methoed = boardClass->Get<UnityResolve::Method>("StartNextRound");
			if (!methoed) {
				LOG_ERROR("StartNextRound 未找到 Board::StartNextRound 方法！");
				return false;
			}

			methoed->Invoke<void>(
				boardInstance
			);
			return true;
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("StartNextRound 异常: {}", e.what()).c_str());
			return false;
		}
	}

	void SetPit(int theColumn, int theRow) {
		try {
			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				LOG_ERROR("SetPit 未找到 Board 实例！");
				return;
			}

			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				LOG_ERROR("SetPit 未找到程序集！");
				return;
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				LOG_ERROR("SetPit 未找到 Board 类！");
				return;
			}

			const auto methoed = boardClass->Get<UnityResolve::Method>("SetPit");
			if (!methoed) {
				LOG_ERROR("SetPit 未找到 Board::SetPit 方法！");
				return;
			}

			methoed->Invoke<void>(
				boardInstance,
				theColumn,
				theRow
			);
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("SetPit 异常: {}", e.what()).c_str());
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

	std::string GetBoardFieldsJson() {
		try {
			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				LOG_ERROR("GetBoardFieldsJson 未找到程序集！");
				return "{\"Class\":\"Board\",\"Error\":\"assembly_not_found\",\"Fields\":[]}";
			}

			const auto boardClass = assembly->Get("Board");
			if (!boardClass) {
				LOG_ERROR("GetBoardFieldsJson 未找到 Board 类！");
				return "{\"Class\":\"Board\",\"Error\":\"board_class_not_found\",\"Fields\":[]}";
			}

			void* boardInstance = GetBoardInstance();
			if (!boardInstance) {
				LOG_WARNING("GetBoardFieldsJson 未找到 Board 实例，仅导出静态字段值");
			}

			std::string json;
			json.reserve(boardClass->fields.size() * 112 + 96);
			json.push_back('{');
			json.append(std::format("\"Class\":\"{}\",", EscapeJsonString(boardClass->name)));
			if (boardInstance) {
				json.append(std::format("\"Instance\":\"0x{:X}\",", reinterpret_cast<std::uintptr_t>(boardInstance)));
			}
			else {
				json.append("\"Instance\":null,");
			}
			json.append("\"Fields\":[");

			bool firstField = true;
			for (const auto field : boardClass->fields) {
				if (!field) {
					continue;
				}

				if (!firstField) {
					json.push_back(',');
				}

				const std::string typeName = field->type ? field->type->name : "Unknown";
				const std::string valueJson = SerializeBoardFieldValueAsJson(field, boardInstance);
				json.append(std::format(
					"{{\"Name\":\"{}\",\"Type\":\"{}\",\"Static\":{},\"Offset\":{},\"Value\":{}}}",
					EscapeJsonString(field->name),
					EscapeJsonString(typeName),
					field->static_field ? "true" : "false",
					field->offset,
					valueJson
				));
				firstField = false;
			}

			json.append("]}");
			return json;
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("GetBoardFieldsJson 异常: {}", e.what()).c_str());
			return std::format("{{\"Class\":\"Board\",\"Error\":\"{}\",\"Fields\":[]}}", EscapeJsonString(e.what()));
		}
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

	void CreatePlant(int newColumn, int newRow, int theSeedType) {
		auto createPlantTask = [=]() {
			try {
				const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
				if (!assembly) {
					LOG_ERROR("CreatePlant: Assembly not found");
					return;
				}

				const auto klass = assembly->Get("CreatePlant");
				if (!klass) {
					LOG_ERROR("CreatePlant: Class not found");
					return;
				}

				const auto objects = klass->FindObjectsByType<void*>();
				if (objects.empty() || !objects[0]) {
					LOG_ERROR("CreatePlant: Instance not found");
					return;
				}

				static const std::vector<std::string> setPlantArgs = {
					"System.Int32",
					"System.Int32",
					"PlantType",
					"Plant",
					"UnityEngine.Vector2",
					"System.Boolean",
					"System.Boolean",
					"Plant"
				};
				auto method = klass->Get<UnityResolve::Method>("SetPlant", setPlantArgs);
				if (!method) {
					method = klass->Get<UnityResolve::Method>("SetPlant");
				}
				if (!method) {
					LOG_ERROR("CreatePlant: Method not found");
					return;
				}

				UnityResolve::UnityType::Vector2 puffV{};
				puffV.x = 0.0f;
				puffV.y = 0.0f;

				LOG_INFO(std::format("CreatePlant MainThread thread={}", GetCurrentThreadId()).c_str());
				method->Invoke<void*>(
					objects[0],
					newColumn,
					newRow,
					theSeedType,
					nullptr,
					puffV,
					false,
					true,
					nullptr
				);
			}
			catch (const std::exception& e) {
				LOG_ERROR(std::format("CreatePlant Exception: {}", e.what()).c_str());
			}
			catch (...) {
				LOG_ERROR("CreatePlant Unknown Exception");
			}
		};

		if (g_mainThreadDispatcherInstalled.load(std::memory_order_acquire)) {
			RunOnMainThread(std::move(createPlantTask));
			return;
		}

		LOG_WARNING("MainThread Dispatcher 未安装");
	}

	std::string GetPlantList() {
		try {
			const auto assembly = UnityResolve::Get("Assembly-CSharp.dll");
			if (!assembly) {
				LOG_ERROR("GetPlantList 未找到程序集！");
				return "{\"Class\":\"PlantType\",\"Error\":\"assembly_not_found\",\"Items\":[]}";
			}

			const auto enumClass = assembly->Get("PlantType", "*");
			if (!enumClass) {
				LOG_ERROR("GetPlantList 未找到 PlantType 枚举类！");
				return "{\"Class\":\"PlantType\",\"Error\":\"enum_class_not_found\",\"Items\":[]}";
			}

			const auto enumValues = board_runtime::common::GetEnumValues<int>(enumClass);
			std::vector<std::pair<std::string, int>> items;
			items.reserve(enumValues.size());
			for (const auto& [name, value] : enumValues) {
				items.emplace_back(name, value);
			}

			std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
				if (left.second != right.second) {
					return left.second < right.second;
				}
				return left.first < right.first;
			});

			std::string json;
			json.reserve(items.size() * 48 + 32);
			json.append("{\"Class\":\"PlantType\",\"Items\":[");

			bool first = true;
			for (const auto& [name, value] : items) {
				if (!first) {
					json.push_back(',');
				}
				json.append(std::format(
					"{{\"name\":\"{}\",\"value\":{}}}",
					EscapeJsonString(name),
					value
				));
				first = false;
			}

			json.append("]}");
			return json;
		}
		catch (const std::exception& e) {
			LOG_ERROR(std::format("GetPlantList 异常: {}", e.what()).c_str());
			return std::format(
				"{{\"Class\":\"PlantType\",\"Error\":\"{}\",\"Items\":[]}}",
				EscapeJsonString(e.what())
			);
		}
	}
}



