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
	void CreateFireLine(int theFireRow);
	void SetPit(int theColumn, int theRo);
	bool StartNextRound();
	std::string GetPlantList();
	std::string GetBoardFieldsJson();
	void SetSun(int sunCount);
	void CreatePlant(int newColumn, int newRow, int theSeedType);
}
