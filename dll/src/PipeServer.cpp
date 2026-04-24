#include "../pch.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../Console.hpp"

#include "BoardRuntime.hpp"
#include "PipeServer.hpp"

namespace ipc {
	namespace {
		constexpr std::size_t kCommandDataCapacity = 256;
		constexpr std::size_t kResponseDataCapacity = 65535;

		enum class RpcValueType : uint8_t {
			Null = 0,
			Bool = 1,
			Int32 = 2,
			Float64 = 3,
			String = 4
		};

		struct CommandPacket {
			uint32_t command;
			uint32_t dataSize;
			char data[kCommandDataCapacity];
		};

		struct ResponsePacket {
			uint32_t success;
			uint32_t dataSize;
			char data[kResponseDataCapacity];
		};

		struct RpcValue {
			RpcValueType type = RpcValueType::Null;
			bool boolValue = false;
			int32_t intValue = 0;
			double floatValue = 0.0;
			std::string stringValue;

			static RpcValue FromBool(bool value) {
				RpcValue result;
				result.type = RpcValueType::Bool;
				result.boolValue = value;
				return result;
			}

			static RpcValue FromInt32(int32_t value) {
				RpcValue result;
				result.type = RpcValueType::Int32;
				result.intValue = value;
				return result;
			}

			static RpcValue FromString(std::string value) {
				RpcValue result;
				result.type = RpcValueType::String;
				result.stringValue = std::move(value);
				return result;
			}
		};

		using RpcHandler = std::function<RpcValue(const std::vector<RpcValue>&)>;
		struct RpcMethodEntry {
			std::string_view name;
			RpcHandler handler;
		};

		std::mutex g_pipeMutex;
		HANDLE g_pipeHandle = INVALID_HANDLE_VALUE;
		HANDLE g_pipeThread = nullptr;
		HANDLE g_stopEvent = nullptr;

		void TracePipeMessage(const std::string& text) {
			const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
			if (wideLength <= 0) {
				OutputDebugStringA(text.c_str());
				return;
			}

			std::wstring wideMessage(static_cast<size_t>(wideLength), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wideMessage.data(), wideLength);
			OutputDebugStringW(wideMessage.c_str());
		}

		void TracePipeInfo(const std::string& text) {
			const std::string message = std::format("[管道][信息] {}\n", text);
			TracePipeMessage(message);
		}

		void TracePipeError(const std::string& text) {
			const std::string message = std::format("[管道][错误] {}\n", text);
			TracePipeMessage(message);
		}

		bool IsStopRequested() {
			return g_stopEvent != nullptr && WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0;
		}

		void ClosePipeHandleLocked() {
			if (g_pipeHandle == INVALID_HANDLE_VALUE) {
				return;
			}

			CancelIoEx(g_pipeHandle, nullptr);
			DisconnectNamedPipe(g_pipeHandle);
			CloseHandle(g_pipeHandle);
			g_pipeHandle = INVALID_HANDLE_VALUE;
		}

		bool WaitForOverlappedResultOrStop(HANDLE pipeHandle, OVERLAPPED& overlapped, DWORD& transferredBytes) {
			HANDLE waitHandles[] = { g_stopEvent, overlapped.hEvent };
			const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

			if (waitResult == WAIT_OBJECT_0) {
				CancelIoEx(pipeHandle, &overlapped);
				return false;
			}

			if (waitResult != WAIT_OBJECT_0 + 1) {
				CancelIoEx(pipeHandle, &overlapped);
				return false;
			}

			return GetOverlappedResult(pipeHandle, &overlapped, &transferredBytes, FALSE) != FALSE;
		}

		bool WaitForPipeConnection(HANDLE pipeHandle) {
			OVERLAPPED overlapped{};
			overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (overlapped.hEvent == nullptr) {
				TracePipeError("创建管道连接重叠事件失败");
				return false;
			}

			bool connected = false;
			DWORD ignored = 0;
			const BOOL connectResult = ConnectNamedPipe(pipeHandle, &overlapped);
			if (connectResult != FALSE) {
				connected = true;
			}
			else {
				const DWORD error = GetLastError();
				if (error == ERROR_IO_PENDING) {
					connected = WaitForOverlappedResultOrStop(pipeHandle, overlapped, ignored);
				}
				else if (error == ERROR_PIPE_CONNECTED) {
					connected = true;
				}
				else if (!IsStopRequested()) {
					TracePipeError(std::format("ConnectNamedPipe 失败: {}", error));
				}
			}

			CloseHandle(overlapped.hEvent);
			return connected;
		}

		bool ReadCommand(HANDLE pipeHandle, CommandPacket& cmd, DWORD& bytesRead) {
			OVERLAPPED overlapped{};
			overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (overlapped.hEvent == nullptr) {
				TracePipeError("创建管道读取重叠事件失败");
				return false;
			}

			bool readSuccess = false;
			bytesRead = 0;

			const BOOL readResult = ReadFile(pipeHandle, &cmd, sizeof(cmd), &bytesRead, &overlapped);
			if (readResult != FALSE) {
				readSuccess = bytesRead > 0;
			}
			else {
				const DWORD error = GetLastError();
				if (error == ERROR_IO_PENDING) {
					readSuccess = WaitForOverlappedResultOrStop(pipeHandle, overlapped, bytesRead) && bytesRead > 0;
				}
				else if (!IsStopRequested() && error != ERROR_BROKEN_PIPE) {
					TracePipeError(std::format("ReadFile 失败: {}", error));
				}
			}

			CloseHandle(overlapped.hEvent);
			return readSuccess;
		}

		bool WriteResponse(HANDLE pipeHandle, const ResponsePacket& resp) {
			OVERLAPPED overlapped{};
			overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (overlapped.hEvent == nullptr) {
				TracePipeError("创建管道写入重叠事件失败");
				return false;
			}

			bool writeSuccess = false;
			DWORD bytesWritten = 0;

			const BOOL writeResult = WriteFile(pipeHandle, &resp, sizeof(resp), &bytesWritten, &overlapped);
			if (writeResult != FALSE) {
				writeSuccess = true;
			}
			else {
				const DWORD error = GetLastError();
				if (error == ERROR_IO_PENDING) {
					writeSuccess = WaitForOverlappedResultOrStop(pipeHandle, overlapped, bytesWritten);
				}
				else if (!IsStopRequested() && error != ERROR_BROKEN_PIPE) {
					TracePipeError(std::format("WriteFile 失败: {}", error));
				}
			}

			CloseHandle(overlapped.hEvent);
			return writeSuccess;
		}

		template <typename T>
		bool ReadPrimitive(const uint8_t* source, size_t sourceSize, size_t& offset, T& value) {
			if (offset + sizeof(T) > sourceSize) {
				return false;
			}

			std::memcpy(&value, source + offset, sizeof(T));
			offset += sizeof(T);
			return true;
		}

		template <typename T>
		void AppendPrimitive(std::vector<uint8_t>& payload, const T& value) {
			const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
			payload.insert(payload.end(), bytes, bytes + sizeof(T));
		}

		std::string TrimCopy(std::string_view text) {
			size_t start = 0;
			size_t end = text.size();

			while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) {
				++start;
			}

			while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
				--end;
			}

			return std::string(text.substr(start, end - start));
		}

		std::string NormalizeFunctionKey(std::string_view functionName) {
			std::string normalized;
			normalized.reserve(functionName.size());

			for (char ch : functionName) {
				const unsigned char raw = static_cast<unsigned char>(ch);
				if (std::isalnum(raw)) {
					normalized.push_back(static_cast<char>(std::tolower(raw)));
				}
			}

			return normalized;
		}

		bool StartsWith(std::string_view text, std::string_view prefix) {
			return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
		}

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

		bool ToBool(const RpcValue& value) {
			switch (value.type) {
			case RpcValueType::Bool:
				return value.boolValue;
			case RpcValueType::Int32:
				return value.intValue != 0;
			case RpcValueType::Float64:
				return value.floatValue != 0.0;
			case RpcValueType::String: {
				std::string text = TrimCopy(value.stringValue);
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
					return static_cast<char>(std::tolower(c));
					});

				if (text == "1" || text == "true" || text == "yes" || text == "on") {
					return true;
				}
				if (text == "0" || text == "false" || text == "no" || text == "off") {
					return false;
				}
				throw std::runtime_error(std::format("无法将字符串转换为布尔值: {}", value.stringValue));
			}
			default:
				throw std::runtime_error("不支持的布尔参数类型");
			}
		}

		int32_t ToInt32(const RpcValue& value) {
			switch (value.type) {
			case RpcValueType::Int32:
				return value.intValue;
			case RpcValueType::Bool:
				return value.boolValue ? 1 : 0;
			case RpcValueType::Float64:
				if (std::trunc(value.floatValue) != value.floatValue) {
					throw std::runtime_error(std::format("浮点参数必须是整数: {}", value.floatValue));
				}
				if (value.floatValue < static_cast<double>((std::numeric_limits<int32_t>::min)()) ||
					value.floatValue > static_cast<double>((std::numeric_limits<int32_t>::max)())) {
					throw std::runtime_error(std::format("参数超出 int32 范围: {}", value.floatValue));
				}
				return static_cast<int32_t>(value.floatValue);
			case RpcValueType::String: {
				const std::string text = TrimCopy(value.stringValue);
				if (text.empty()) {
					throw std::runtime_error("字符串参数不能为空");
				}

				char* end = nullptr;
				const long parsed = std::strtol(text.c_str(), &end, 10);
				if (end == text.c_str() || *end != '\0') {
					throw std::runtime_error(std::format("字符串不是有效整数: {}", value.stringValue));
				}
				if (parsed < (std::numeric_limits<int32_t>::min)() || parsed > (std::numeric_limits<int32_t>::max)()) {
					throw std::runtime_error(std::format("参数超出 int32 范围: {}", value.stringValue));
				}

				return static_cast<int32_t>(parsed);
			}
			default:
				throw std::runtime_error("不支持的 int32 参数类型");
			}
		}

		std::string SerializeZombieCoordinates() {
			const auto coordinates = board_runtime::GetZombieCoordinates();
			std::string serialized;
			serialized.reserve(coordinates.size() * 96 + 2);
			serialized.push_back('[');
			bool first = true;

			for (const auto& coordinate : coordinates) {
				const std::string escapedName = EscapeJsonString(coordinate.name);
				const std::string item = std::format(
					"{{\"X\":{:.2f},\"Y\":{:.2f},\"Z\":{:.2f},\"Row\":{},\"Column\":{},\"Name\":\"{}\"}}",
					coordinate.x,
					coordinate.y,
					coordinate.z,
					coordinate.row,
					coordinate.column,
					escapedName
				);

				if (!first) {
					serialized.push_back(',');
				}
				serialized.append(item);
				first = false;
			}
			serialized.push_back(']');

			return serialized;
		}

		std::string SerializeZombieTypeNameMap() {
			const auto coordinates = board_runtime::GetZombieCoordinates();
			std::unordered_map<int, std::string> typeNameMap;
			typeNameMap.reserve(coordinates.size());
			for (const auto& coordinate : coordinates) {
				if (coordinate.name.empty()) {
					continue;
				}
				typeNameMap.try_emplace(coordinate.zombieType, coordinate.name);
			}

			std::vector<int> sortedTypes;
			sortedTypes.reserve(typeNameMap.size());
			for (const auto& [zombieType, _] : typeNameMap) {
				sortedTypes.push_back(zombieType);
			}
			std::sort(sortedTypes.begin(), sortedTypes.end());

			std::string serialized;
			serialized.reserve(typeNameMap.size() * 24 + 2);
			serialized.push_back('{');
			bool first = true;
			for (const auto zombieType : sortedTypes) {
				const auto it = typeNameMap.find(zombieType);
				if (it == typeNameMap.end()) {
					continue;
				}

				const std::string escapedName = EscapeJsonString(it->second);
				const std::string item = std::format("\"{}\":\"{}\"", zombieType, escapedName);
				if (!first) {
					serialized.push_back(',');
				}
				serialized.append(item);
				first = false;
			}
			serialized.push_back('}');

			return serialized;
		}

		const std::vector<RpcMethodEntry>& GetRpcMethodEntries() {
			static const std::vector<RpcMethodEntry> methods = {
				{
					"SetFreeCD",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (args.size() != 1) {
							throw std::runtime_error("SetFreeCD 需要 1 个参数");
						}

						const bool enabled = ToBool(args[0]);
						board_runtime::SetFreeCD(enabled);
						return RpcValue::FromBool(enabled);
					}
				},
				{
					"SetRandomCard",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (args.size() != 1) {
							throw std::runtime_error("SetRandomCard 需要 1 个参数");
						}

						const bool enabled = ToBool(args[0]);
						board_runtime::SetRandomCard(enabled);
						return RpcValue::FromBool(enabled);
					}
				},
				{
					"SetRightPutPot",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (args.size() != 1) {
							throw std::runtime_error("SetRightPutPot 需要 1 个参数");
						}

						const bool enabled = ToBool(args[0]);
						board_runtime::SetRightPutPot(enabled);
						return RpcValue::FromBool(enabled);
					}
				},
				{
					"GetFreeCD",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (!args.empty()) {
							throw std::runtime_error("GetFreeCD 需要 0 个参数");
						}

						return RpcValue::FromBool(board_runtime::GetFreeCD());
				}
				},
				{
					"StartNextRound",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (!args.empty()) {
							throw std::runtime_error("StartNextRound 需要 0 个参数");
						}

						return RpcValue::FromBool(board_runtime::StartNextRound());
					}
				},
				{
					"GetBoardFields",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (!args.empty()) {
							throw std::runtime_error("GetBoardFields 需要 0 个参数");
						}

						return RpcValue::FromString(board_runtime::GetBoardFieldsJson());
					}
				},
				{
					"GetZombiePositions",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (!args.empty()) {
							throw std::runtime_error("GetZombiePositions 需要 0 个参数");
						}

						return RpcValue::FromString(SerializeZombieCoordinates());
					}
				},
				{
					"SetSun",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (args.size() != 1) {
							throw std::runtime_error("SetSun 需要 1 个参数");
						}

						const int32_t sunValue = ToInt32(args[0]);
						board_runtime::SetSun(sunValue);
						return RpcValue::FromInt32(sunValue);
					}
				},
				{
					"CreateFireLine",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (args.size() != 1) {
							throw std::runtime_error("CreateFireLine 需要 1 个参数");
						}

						const int32_t fireRow = ToInt32(args[0]);
						board_runtime::CreateFireLine(fireRow);
						return RpcValue::FromInt32(fireRow);
					}
				},
				{
					"SetPit",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (args.size() != 2) {
							throw std::runtime_error("SetPit 需要 2 个参数");
						}

						const int32_t theColumn = ToInt32(args[0]);
						const int32_t theRow = ToInt32(args[1]);
						board_runtime::SetPit(theColumn, theRow);
						return RpcValue::FromInt32(0);
					}
				},

				{
					"GetSun",
					[](const std::vector<RpcValue>& args) -> RpcValue {
						if (!args.empty()) {
							throw std::runtime_error("GetSun 需要 0 个参数");
						}

						return RpcValue::FromInt32(board_runtime::GetSun());
					}
				}
			};
			return methods;
		}

		const std::unordered_map<std::string, RpcHandler>& GetRpcHandlers() {
			static const std::unordered_map<std::string, RpcHandler> handlers = [] {
				std::unordered_map<std::string, RpcHandler> map;
				for (const auto& method : GetRpcMethodEntries()) {
					map.emplace(std::string(method.name), method.handler);
					map.emplace(NormalizeFunctionKey(method.name), method.handler);
				}
				return map;
			}();
			return handlers;
		}

		void SetErrorResponse(ResponsePacket& resp, const std::string& message) {
			resp.success = 0;

			const size_t maxWritable = sizeof(resp.data) - 1;
			const size_t copyLength = (std::min)(message.size(), maxWritable);
			std::memset(resp.data, 0, sizeof(resp.data));
			std::memcpy(resp.data, message.data(), copyLength);
			resp.dataSize = static_cast<uint32_t>(copyLength + 1);
		}

		bool DecodeRpcValue(const uint8_t* source, size_t sourceSize, size_t& offset, RpcValue& value, std::string& error) {
			uint8_t typeRaw = 0;
			if (!ReadPrimitive(source, sourceSize, offset, typeRaw)) {
				error = "读取参数类型失败";
				return false;
			}

			switch (static_cast<RpcValueType>(typeRaw)) {
			case RpcValueType::Null:
				value = RpcValue{};
				return true;
			case RpcValueType::Bool: {
				uint8_t rawBool = 0;
				if (!ReadPrimitive(source, sourceSize, offset, rawBool)) {
					error = "读取布尔参数失败";
					return false;
				}

				value = RpcValue::FromBool(rawBool != 0);
				return true;
			}
			case RpcValueType::Int32: {
				int32_t intValue = 0;
				if (!ReadPrimitive(source, sourceSize, offset, intValue)) {
					error = "读取 int32 参数失败";
					return false;
				}

				value = RpcValue::FromInt32(intValue);
				return true;
			}
			case RpcValueType::Float64: {
				double floatValue = 0.0;
				if (!ReadPrimitive(source, sourceSize, offset, floatValue)) {
					error = "读取 float64 参数失败";
					return false;
				}

				value = RpcValue{};
				value.type = RpcValueType::Float64;
				value.floatValue = floatValue;
				return true;
			}
			case RpcValueType::String: {
				uint16_t length = 0;
				if (!ReadPrimitive(source, sourceSize, offset, length)) {
					error = "读取字符串长度失败";
					return false;
				}
				if (offset + length > sourceSize) {
					error = "字符串参数长度无效";
					return false;
				}

				value = RpcValue::FromString(std::string(reinterpret_cast<const char*>(source + offset), length));
				offset += length;
				return true;
			}
			default:
				error = std::format("未知参数类型: {}", typeRaw);
				return false;
			}
		}

		bool DecodeInvokeRequest(const CommandPacket& cmd, std::string& functionName, std::vector<RpcValue>& args, std::string& error) {
			const size_t payloadSize = cmd.dataSize;
			if (payloadSize == 0 || payloadSize > sizeof(cmd.data)) {
				error = "调用载荷大小无效";
				return false;
			}

			const uint8_t* payload = reinterpret_cast<const uint8_t*>(cmd.data);
			size_t offset = 0;

			uint8_t functionLength = 0;
			if (!ReadPrimitive(payload, payloadSize, offset, functionLength)) {
				error = "读取函数名长度失败";
				return false;
			}

			if (functionLength == 0 || offset + functionLength > payloadSize) {
				error = "函数名长度无效";
				return false;
			}

			functionName.assign(reinterpret_cast<const char*>(payload + offset), functionLength);
			offset += functionLength;

			uint8_t argCount = 0;
			if (!ReadPrimitive(payload, payloadSize, offset, argCount)) {
				error = "读取参数数量失败";
				return false;
			}

			args.clear();
			args.reserve(argCount);
			for (uint8_t i = 0; i < argCount; ++i) {
				RpcValue value;
				if (!DecodeRpcValue(payload, payloadSize, offset, value, error)) {
					error = std::format("解析参数 {} 失败: {}", static_cast<uint32_t>(i), error);
					return false;
				}
				args.push_back(std::move(value));
			}

			if (offset != payloadSize) {
				error = "调用载荷包含多余字节";
				return false;
			}

			return true;
		}

		bool EncodeRpcValue(const RpcValue& value, ResponsePacket& resp, std::string& error) {
			std::vector<uint8_t> payload;
			payload.reserve(sizeof(resp.data));
			payload.push_back(static_cast<uint8_t>(value.type));

			switch (value.type) {
			case RpcValueType::Null:
				break;
			case RpcValueType::Bool:
				payload.push_back(value.boolValue ? 1 : 0);
				break;
			case RpcValueType::Int32:
				AppendPrimitive(payload, value.intValue);
				break;
			case RpcValueType::Float64:
				AppendPrimitive(payload, value.floatValue);
				break;
			case RpcValueType::String: {
				if (value.stringValue.size() > (std::numeric_limits<uint16_t>::max)()) {
					error = "字符串返回值过长";
					return false;
				}

				const uint16_t textLength = static_cast<uint16_t>(value.stringValue.size());
				AppendPrimitive(payload, textLength);
				payload.insert(payload.end(), value.stringValue.begin(), value.stringValue.end());
				break;
			}
			default:
				error = "不支持的返回值类型";
				return false;
			}

			if (payload.size() > sizeof(resp.data)) {
				error = std::format("返回载荷过大: {} > {}", payload.size(), sizeof(resp.data));
				return false;
			}

			resp.success = 1;
			resp.dataSize = static_cast<uint32_t>(payload.size());
			std::memset(resp.data, 0, sizeof(resp.data));
			std::memcpy(resp.data, payload.data(), payload.size());
			return true;
		}

		void ProcessInvokeCommand(const CommandPacket* cmd, ResponsePacket* resp) {
			std::string functionName;
			std::vector<RpcValue> args;
			std::string decodeError;

			if (!DecodeInvokeRequest(*cmd, functionName, args, decodeError)) {
				SetErrorResponse(*resp, std::format("调用请求解码失败: {}", decodeError));
				return;
			}

			std::string exactName = TrimCopy(functionName);
			std::string normalized = NormalizeFunctionKey(exactName);
			std::string stripped = normalized;
			if (StartsWith(stripped, "boardruntime")) {
				stripped = stripped.substr(std::string_view("boardruntime").size());
			}
			if (StartsWith(stripped, "cmd")) {
				stripped = stripped.substr(3);
			}

			const auto& handlers = GetRpcHandlers();
			auto handler = handlers.find(exactName);
			if (handler == handlers.end()) {
				handler = handlers.find(normalized);
			}
			if (handler == handlers.end()) {
				handler = handlers.find(stripped);
			}
			if (handler == handlers.end()) {
				SetErrorResponse(*resp, std::format("未知函数: {}", functionName));
				return;
			}

			try {
				RpcValue result = handler->second(args);
				std::string encodeError;
				if (!EncodeRpcValue(result, *resp, encodeError)) {
					SetErrorResponse(*resp, std::format("返回值编码失败: {}", encodeError));
					return;
				}

				TracePipeInfo(std::format("调用成功: {}", functionName));
			}
			catch (const std::exception& e) {
				SetErrorResponse(*resp, std::format("{} 调用失败: {}", functionName, e.what()));
			}
		}

		void ProcessCommand(const CommandPacket* cmd, ResponsePacket* resp) {
			if (cmd->command != 0) {
				SetErrorResponse(*resp, std::format("不支持的命令 ID: {}。Invoke 请使用命令 0。", cmd->command));
				return;
			}

			ProcessInvokeCommand(cmd, resp);
		}

		DWORD WINAPI PipeServerThread(LPVOID) {
			while (!IsStopRequested()) {
				HANDLE pipeHandle = CreateNamedPipeW(
					L"\\\\.\\pipe\\PVZModPipe",
					PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
					PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
					PIPE_UNLIMITED_INSTANCES,
					sizeof(ResponsePacket),
					sizeof(CommandPacket),
					0,
					nullptr
				);

				{
					std::lock_guard<std::mutex> lock(g_pipeMutex);
					g_pipeHandle = pipeHandle;
				}

				if (pipeHandle == INVALID_HANDLE_VALUE) {
					if (!IsStopRequested()) {
						TracePipeError("创建命名管道失败");
					}

					if (g_stopEvent != nullptr && WaitForSingleObject(g_stopEvent, 5000) == WAIT_OBJECT_0) {
						break;
					}
					continue;
				}

				TracePipeInfo("命名管道服务已创建，等待连接...");

				if (WaitForPipeConnection(pipeHandle)) {
					TracePipeInfo("客户端已连接");

					CommandPacket cmd{};
					DWORD bytesRead = 0;
					while (!IsStopRequested() && ReadCommand(pipeHandle, cmd, bytesRead)) {
						ResponsePacket resp{};
						ProcessCommand(&cmd, &resp);

						if (!WriteResponse(pipeHandle, resp)) {
							break;
						}
					}
				}

				{
					std::lock_guard<std::mutex> lock(g_pipeMutex);
					ClosePipeHandleLocked();
				}
			}

			return 0;
		}
	}

	bool StartPipeServer(void* stopEvent) {
		if (g_pipeThread != nullptr) {
			return true;
		}

		g_stopEvent = static_cast<HANDLE>(stopEvent);
		g_pipeThread = CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
		if (g_pipeThread == nullptr) {
			TracePipeError("创建管道服务线程失败");
			g_stopEvent = nullptr;
			return false;
		}

		return true;
	}

	void StopPipeServer(unsigned long waitTimeoutMs) {
		{
			std::lock_guard<std::mutex> lock(g_pipeMutex);
			ClosePipeHandleLocked();
		}

		if (g_pipeThread != nullptr) {
			WaitForSingleObject(g_pipeThread, waitTimeoutMs);
			CloseHandle(g_pipeThread);
			g_pipeThread = nullptr;
		}

		g_stopEvent = nullptr;
	}
}
