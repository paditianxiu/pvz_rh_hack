#pragma once

namespace bootstrap {
void Start();
void Stop(bool processTerminating);
void SetModuleHandle(void* moduleHandle);
void NotifyProcessDetach(bool processTerminating);
bool RequestFastUnload();
}
