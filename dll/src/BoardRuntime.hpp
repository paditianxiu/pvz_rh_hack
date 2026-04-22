#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace board_runtime {
	struct ZombieCoordinate {
		std::uintptr_t instance;
		float x;
		float y;
		float z;
		int row;
		int column;
	};

	bool InstallBoardHooks();
	void UninstallBoardHooks();

	void* GetBoardInstance();
	std::vector<ZombieCoordinate> GetZombieCoordinates();

	void SetRandomCard(bool enabled);
	void SetFreeCD(bool enabled);
	bool GetFreeCD();
	int GetBoardWave();
	int GetSun();
	void SetSun(int sunCount);
}
