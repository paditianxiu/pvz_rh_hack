#pragma once
#include <Windows.h>
#include <cstdio>
#include <print>
#include <string>
#include <ostream>
#define LOG_DEBUG(text) console::OutConsole(console::Debug,text,__FILE__,__LINE__)
#define LOG_INFO(text) console::OutConsole(console::Info,text,__FILE__,__LINE__)
#define LOG_WARNING(text) console::OutConsole(console::Warning,text,__FILE__,__LINE__)
#define LOG_ERROR(text) console::OutConsole(console::Error,text,__FILE__,__LINE__)

namespace console {
	enum OutType : short int {
		Info,
		Debug,
		Warning,
		Error
	};

	enum Color : short int {
		Black,
		Blue,
		Green,
		LightGreen,
		Red,
		Purple,
		Yellow,
		White,
		Grey,
		LightBlue,
		ThinGreen,
		LightLightGreen,
		LightRed,
		Lavender,
		CanaryYellow,
		BrightWhite
	};

	static auto OutConsole(const OutType type, const std::string& text, const std::string& file, int line) -> void {
		if (GetConsoleWindow() == nullptr) return;
		const auto hWnd_ = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hWnd_ == nullptr || hWnd_ == INVALID_HANDLE_VALUE) return;
		switch (type) {
			case Info: SetConsoleTextAttribute(hWnd_, BACKGROUND_INTENSITY | Green * 16);
				std::print(" ");
				SetConsoleTextAttribute(hWnd_, BACKGROUND_INTENSITY | Black);
				SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | White);
				std::print("[");
				SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | Green);
				std::print("信息 ");
				break;
			case Debug: SetConsoleTextAttribute(hWnd_, BACKGROUND_INTENSITY | Blue * 16);
				std::print(" ");
				SetConsoleTextAttribute(hWnd_, BACKGROUND_INTENSITY | Black);
				SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | White);
				std::print("[");
				SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | Blue);
				std::print("调试");
				break;
			case Warning: SetConsoleTextAttribute(hWnd_, BACKGROUND_INTENSITY | Yellow * 16);
				std::print(" ");
				SetConsoleTextAttribute(hWnd_, BACKGROUND_INTENSITY | Black);
				SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | White);
				std::print("[");
				SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | Yellow);
				std::print("警告 ");
				break;
			case Error: SetConsoleTextAttribute(hWnd_, BACKGROUND_INTENSITY | Red * 16);
				std::print(" ");
				SetConsoleTextAttribute(hWnd_, BACKGROUND_INTENSITY | Black);
				SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | White);
				std::print("[");
				SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | Red);
				std::print("错误");
				break;
		}
		SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | White);
		std::print("]>[");
		SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | Purple);
		std::print("{}:{}", file.substr(file.find_last_of('\\') + 1), line);
		SetConsoleTextAttribute(hWnd_, FOREGROUND_INTENSITY | White);
		std::print("] :{}\n", text);
		std::fflush(stdout);
	}

	static auto StartConsole(const wchar_t* title, const bool close) -> HWND {
		HWND hWnd_ = nullptr;
		if (GetConsoleWindow() != nullptr) return GetConsoleWindow();
		AllocConsole();
		SetConsoleTitleW(title);
		SetConsoleOutputCP(CP_UTF8);
		SetConsoleCP(CP_UTF8);
		while (nullptr == hWnd_) hWnd_ = GetConsoleWindow();
		const auto menu_ = GetSystemMenu(hWnd_, FALSE);
		if (!close) DeleteMenu(menu_, SC_CLOSE, MF_BYCOMMAND);
		SetWindowLong(hWnd_, GWL_STYLE, GetWindowLong(hWnd_, GWL_STYLE) & ~WS_MAXIMIZEBOX);
		SetWindowLong(hWnd_, GWL_STYLE, GetWindowLong(hWnd_, GWL_STYLE) & ~WS_THICKFRAME);

		FILE* redirectedStdout = nullptr;
		FILE* redirectedStderr = nullptr;
		FILE* redirectedStdin = nullptr;
		freopen_s(&redirectedStdout, "CONOUT$", "w", stdout);
		freopen_s(&redirectedStderr, "CONOUT$", "w", stderr);
		freopen_s(&redirectedStdin, "CONIN$", "r", stdin);

		setvbuf(stdout, nullptr, _IONBF, 0);
		setvbuf(stderr, nullptr, _IONBF, 0);

		const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
		if (inputHandle != nullptr && inputHandle != INVALID_HANDLE_VALUE) {
			DWORD inputMode = 0;
			if (GetConsoleMode(inputHandle, &inputMode) != FALSE) {
				inputMode |= ENABLE_EXTENDED_FLAGS;
				inputMode |= ENABLE_INSERT_MODE;
				inputMode &= ~ENABLE_QUICK_EDIT_MODE;
				SetConsoleMode(inputHandle, inputMode);
			}
		}
		return hWnd_;
	}

	static auto EndConsole() -> void {
		if (GetConsoleWindow() == nullptr) return;
		fclose(stdout);
		fclose(stderr);
		fclose(stdin);
		FreeConsole();
	}
}
