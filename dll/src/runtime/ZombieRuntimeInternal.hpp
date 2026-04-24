#pragma once

#include "../../UnityResolve.hpp"

namespace board_runtime::internal {
	bool InstallZombieHooks(UnityResolve::Assembly* assembly);
	void UninstallZombieHooks();
	void ResetZombieHooksState();
}
