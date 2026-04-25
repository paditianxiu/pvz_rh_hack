#pragma once

#include "../../../UnityResolve.hpp"

namespace board_runtime::internal {
	class CreatePlantSetPlantHookTest final {
	public:
		static bool Install(UnityResolve::Assembly* assembly);
		static void Uninstall();
		static void Reset();
	};
}
