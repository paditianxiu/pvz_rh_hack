#pragma once

namespace ipc {
bool StartPipeServer(void* stopEvent);
void StopPipeServer(unsigned long waitTimeoutMs = 6000);
}
