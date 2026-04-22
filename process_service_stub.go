//go:build !windows

package main

import "errors"

func listProcesses() ([]ProcessInfo, error) {
	return nil, errors.New("当前平台不支持进程管理和 DLL 注入")
}

func findProcessesByName(processName string) ([]ProcessInfo, error) {
	return nil, errors.New("当前平台不支持进程管理和 DLL 注入")
}

func getProcessPID(processName string) (uint32, error) {
	return 0, errors.New("当前平台不支持进程管理和 DLL 注入")
}

func terminateProcess(pid uint32) error {
	return errors.New("当前平台不支持进程管理和 DLL 注入")
}

func injectDLL(pid uint32, dllPath string) error {
	return errors.New("当前平台不支持 DLL 注入")
}

func unloadDLL(pid uint32, dllPath string) error {
	return errors.New("当前平台不支持 DLL 卸载")
}

func isDLLInjected(pid uint32, dllPath string) (bool, error) {
	return false, errors.New("当前平台不支持 DLL 检测")
}

func getMainWindowBounds(processName string) (*WindowBounds, error) {
	return nil, errors.New("当前平台不支持读取窗口边界")
}

func setOverlayVisible(processName string, visible bool) error {
	return errors.New("当前平台不支持叠加窗口")
}

func syncOverlayWindow(processName string) error {
	return errors.New("当前平台不支持叠加窗口")
}
