#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <vector>

#include "../../../Console.hpp"
#include "../../../UnityResolve.hpp"
#include <detours/detours.h>



namespace board_runtime::common {
	template <typename MethodHookT>
	bool AttachMethodHook(UnityResolve::Class* klass,
		const char* className,
		const char* methodName,
		MethodHookT& original,
		MethodHookT hook,
		bool required) {
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
		original = reinterpret_cast<MethodHookT>(method->function);
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
	}

	template<typename T>
	std::unordered_map<std::string, T> GetEnumValues(UnityResolve::Class* enumClass) {
		std::unordered_map<std::string, T> result;

		if (!enumClass) return result;

		for (const auto& field : enumClass->fields) {
			if (field->static_field) {
				T value = 0;
				field->GetStaticValue(&value);
				result[field->name] = value;
			}
		}

		return result;
	}
}
