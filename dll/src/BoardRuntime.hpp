#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace board_runtime {
	struct ZombieCoordinate {
		std::uintptr_t instance;
		float x;
		float y;
		float z;
		int row;
		int column;
		int zombieType;
		std::string name;
	};

	bool InstallBoardHooks();
	void UninstallBoardHooks();

	void* GetBoardInstance();
	std::vector<ZombieCoordinate> GetZombieCoordinates();

	void SetRandomCard(bool enabled);
	void SetFreeCD(bool enabled);
	void SetRightPutPot(bool enabled);
	bool GetFreeCD();
	int GetBoardWave();
	int GetSun();
	void SetSun(int sunCount);
}
