//go:build windows

package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"syscall"
	"unsafe"

	"github.com/wailsapp/wails/v3/pkg/application"
	"golang.org/x/sys/windows"
)

const (
	processAccessInject = windows.PROCESS_CREATE_THREAD |
		windows.PROCESS_QUERY_INFORMATION |
		windows.PROCESS_VM_OPERATION |
		windows.PROCESS_VM_WRITE |
		windows.PROCESS_VM_READ

	processAccessUnload = windows.PROCESS_CREATE_THREAD |
		windows.PROCESS_QUERY_INFORMATION |
		windows.PROCESS_VM_OPERATION |
		windows.PROCESS_VM_READ

	processAccessTerminate = windows.PROCESS_TERMINATE | windows.SYNCHRONIZE

	memCommit  = 0x1000
	memReserve = 0x2000
	memRelease = 0x8000

	gwOwner = 4

	remoteThreadWaitMilliseconds = 5000

	overlayWindowName = "zombie-overlay"
	defaultWindowDPI  = 96
)

var (
	user32DLL               = windows.NewLazySystemDLL("user32.dll")
	enumWindowsProc         = user32DLL.NewProc("EnumWindows")
	getWindowThreadPIDProc  = user32DLL.NewProc("GetWindowThreadProcessId")
	isWindowVisibleProc     = user32DLL.NewProc("IsWindowVisible")
	getWindowProc           = user32DLL.NewProc("GetWindow")
	getClientRectProc       = user32DLL.NewProc("GetClientRect")
	clientToScreenProc      = user32DLL.NewProc("ClientToScreen")
	getDpiForWindowProc     = user32DLL.NewProc("GetDpiForWindow")
	getForegroundWindowProc = user32DLL.NewProc("GetForegroundWindow")
	kernel32DLL             = windows.NewLazySystemDLL("kernel32.dll")
	createRemoteThreadProc  = kernel32DLL.NewProc("CreateRemoteThread")
	virtualAllocExProc      = kernel32DLL.NewProc("VirtualAllocEx")
	virtualFreeExProc       = kernel32DLL.NewProc("VirtualFreeEx")
	getExitCodeThreadProc   = kernel32DLL.NewProc("GetExitCodeThread")
	loadLibraryWProc        = kernel32DLL.NewProc("LoadLibraryW")
	freeLibraryProc         = kernel32DLL.NewProc("FreeLibrary")
	findMainWindowCallback  = syscall.NewCallback(enumMainWindowForPIDProc)
)

func listProcesses() ([]ProcessInfo, error) {
	snapshot, err := windows.CreateToolhelp32Snapshot(windows.TH32CS_SNAPPROCESS, 0)
	if err != nil {
		return nil, fmt.Errorf("创建进程快照失败: %w", err)
	}
	defer windows.CloseHandle(snapshot)

	var entry windows.ProcessEntry32
	entry.Size = uint32(unsafe.Sizeof(entry))

	if err := windows.Process32First(snapshot, &entry); err != nil {
		return nil, fmt.Errorf("读取首个进程失败: %w", err)
	}

	processes := make([]ProcessInfo, 0, 128)
	for {
		processes = append(processes, processEntryToInfo(entry))

		err = windows.Process32Next(snapshot, &entry)
		if err != nil {
			if errors.Is(err, windows.ERROR_NO_MORE_FILES) {
				break
			}
			return nil, fmt.Errorf("遍历进程失败: %w", err)
		}
	}

	sort.Slice(processes, func(i, j int) bool {
		left := strings.ToLower(processes[i].Name)
		right := strings.ToLower(processes[j].Name)
		if left == right {
			return processes[i].PID < processes[j].PID
		}
		return left < right
	})

	return processes, nil
}

func findProcessesByName(processName string) ([]ProcessInfo, error) {
	processName = strings.TrimSpace(processName)
	if processName == "" {
		return nil, errors.New("进程名不能为空")
	}

	processes, err := listProcesses()
	if err != nil {
		return nil, err
	}

	matches := make([]ProcessInfo, 0, 4)
	for _, process := range processes {
		if processNameMatches(processName, process.Name) {
			matches = append(matches, process)
		}
	}

	return matches, nil
}

func getProcessPID(processName string) (uint32, error) {
	matches, err := findProcessesByName(processName)
	if err != nil {
		return 0, err
	}

	switch len(matches) {
	case 0:
		return 0, fmt.Errorf("未找到进程: %s", processName)
	case 1:
		return matches[0].PID, nil
	default:
		return 0, fmt.Errorf("找到多个同名进程，请改用明确 PID: %s", formatProcessMatches(matches))
	}
}

func terminateProcess(pid uint32) error {
	if err := validatePID(pid); err != nil {
		return err
	}

	processHandle, err := windows.OpenProcess(processAccessTerminate, false, pid)
	if err != nil {
		return fmt.Errorf("打开进程失败(pid=%d): %w", pid, err)
	}
	defer windows.CloseHandle(processHandle)

	if err := windows.TerminateProcess(processHandle, 0); err != nil {
		return fmt.Errorf("结束进程失败(pid=%d): %w", pid, err)
	}

	return waitForHandle(processHandle, remoteThreadWaitMilliseconds, fmt.Sprintf("等待进程退出超时(pid=%d)", pid))
}

func injectDLL(pid uint32, dllPath string) error {
	if err := validatePID(pid); err != nil {
		return err
	}

	resolvedDLLPath, err := resolveDLLPath(dllPath)
	if err != nil {
		return err
	}

	processHandle, err := windows.OpenProcess(processAccessInject, false, pid)
	if err != nil {
		return fmt.Errorf("打开目标进程失败(pid=%d): %w", pid, err)
	}
	defer windows.CloseHandle(processHandle)

	utf16DLLPath, err := windows.UTF16FromString(resolvedDLLPath)
	if err != nil {
		return fmt.Errorf("DLL 路径编码失败: %w", err)
	}

	bufferSize := uintptr(len(utf16DLLPath)) * unsafe.Sizeof(utf16DLLPath[0])
	remoteMemory, err := virtualAllocEx(processHandle, 0, bufferSize, memCommit|memReserve, windows.PAGE_READWRITE)
	if err != nil {
		return fmt.Errorf("申请远程内存失败: %w", err)
	}
	defer virtualFreeEx(processHandle, remoteMemory, 0, memRelease)

	if err := writeRemoteUTF16(processHandle, remoteMemory, utf16DLLPath); err != nil {
		return err
	}

	threadHandle, err := createRemoteThread(processHandle, loadLibraryWProc.Addr(), remoteMemory)
	if err != nil {
		return fmt.Errorf("创建远程线程失败: %w", err)
	}
	defer windows.CloseHandle(threadHandle)

	if err := waitForHandle(threadHandle, remoteThreadWaitMilliseconds, "等待 DLL 注入完成超时"); err != nil {
		return err
	}

	exitCode, err := getExitCodeThread(threadHandle)
	if err != nil {
		return err
	}
	if exitCode == 0 {
		return errors.New("远程 LoadLibraryW 执行失败")
	}

	return nil
}

func unloadDLL(pid uint32, dllPath string) error {
	if err := validatePID(pid); err != nil {
		return err
	}

	resolvedDLLPath, err := resolveDLLPath(dllPath)
	if err != nil {
		return err
	}

	moduleHandle, found, err := findRemoteModule(pid, resolvedDLLPath)
	if err != nil {
		return err
	}
	if !found {
		return fmt.Errorf("未在进程 %d 中找到 DLL: %s", pid, resolvedDLLPath)
	}

	processHandle, err := windows.OpenProcess(processAccessUnload, false, pid)
	if err != nil {
		return fmt.Errorf("打开目标进程失败(pid=%d): %w", pid, err)
	}
	defer windows.CloseHandle(processHandle)

	threadHandle, err := createRemoteThread(processHandle, freeLibraryProc.Addr(), uintptr(moduleHandle))
	if err != nil {
		return fmt.Errorf("创建远程卸载线程失败: %w", err)
	}
	defer windows.CloseHandle(threadHandle)

	return nil
}

func isDLLInjected(pid uint32, dllPath string) (bool, error) {
	if err := validatePID(pid); err != nil {
		return false, err
	}

	resolvedDLLPath, err := resolveDLLPath(dllPath)
	if err != nil {
		return false, err
	}

	_, found, err := findRemoteModule(pid, resolvedDLLPath)
	if err != nil {
		return false, err
	}

	return found, nil
}

func getMainWindowBounds(processName string) (*WindowBounds, error) {
	pid, err := getProcessPID(processName)
	if err != nil {
		return nil, err
	}

	windowHandle, err := findMainWindowByPID(pid)
	if err != nil {
		return nil, err
	}

	return getWindowBounds(windowHandle)
}

func setOverlayVisible(processName string, visible bool) error {
	overlayWindow, exists := application.Get().Window.GetByName(overlayWindowName)
	if !exists {
		return fmt.Errorf("未找到叠加窗口: %s", overlayWindowName)
	}

	if !visible {
		overlayWindow.Hide()
		return nil
	}

	if err := syncOverlayWindow(processName); err != nil {
		return err
	}

	overlayWindow.SetAlwaysOnTop(true)
	overlayWindow.SetIgnoreMouseEvents(true)
	overlayWindow.Show()
	return nil
}

func syncOverlayWindow(processName string) error {
	overlayWindow, exists := application.Get().Window.GetByName(overlayWindowName)
	if !exists {
		return fmt.Errorf("未找到叠加窗口: %s", overlayWindowName)
	}

	pid, err := getProcessPID(processName)
	if err != nil {
		return err
	}

	windowHandle, err := findMainWindowByPID(pid)
	if err != nil {
		return err
	}

	bounds, err := getWindowBounds(windowHandle)
	if err != nil {
		return err
	}

	dpi := getWindowDPI(windowHandle)
	logicalBounds := convertPhysicalBoundsToLogical(bounds, dpi)

	overlayWindow.SetPosition(logicalBounds.X, logicalBounds.Y)
	overlayWindow.SetSize(logicalBounds.Width, logicalBounds.Height)
	overlayWindow.SetAlwaysOnTop(true)
	overlayWindow.SetIgnoreMouseEvents(true)
	return nil
}

func processEntryToInfo(entry windows.ProcessEntry32) ProcessInfo {
	return ProcessInfo{
		PID:         entry.ProcessID,
		ParentPID:   entry.ParentProcessID,
		Name:        windows.UTF16ToString(entry.ExeFile[:]),
		ThreadCount: entry.Threads,
	}
}

func processNameMatches(query, actual string) bool {
	query = strings.ToLower(filepath.Base(strings.TrimSpace(query)))
	actual = strings.ToLower(filepath.Base(strings.TrimSpace(actual)))

	if query == actual {
		return true
	}

	if filepath.Ext(query) == "" {
		return query+".exe" == actual
	}

	return false
}

func formatProcessMatches(processes []ProcessInfo) string {
	parts := make([]string, 0, len(processes))
	for _, process := range processes {
		parts = append(parts, fmt.Sprintf("%s(pid=%d)", process.Name, process.PID))
	}
	return strings.Join(parts, ", ")
}

func validatePID(pid uint32) error {
	if pid == 0 {
		return errors.New("PID 必须大于 0")
	}
	return nil
}

func resolveExistingPath(path string) (string, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return "", errors.New("路径不能为空")
	}

	absolutePath, err := filepath.Abs(path)
	if err != nil {
		return "", fmt.Errorf("解析绝对路径失败: %w", err)
	}

	realPath, err := filepath.EvalSymlinks(absolutePath)
	if err == nil {
		absolutePath = realPath
	}

	if _, err := os.Stat(absolutePath); err != nil {
		return "", fmt.Errorf("目标文件不存在: %w", err)
	}

	return filepath.Clean(absolutePath), nil
}

func normalizePathForMatch(path string) string {
	path = strings.TrimSpace(path)
	if path == "" {
		return ""
	}

	if absolutePath, err := filepath.Abs(path); err == nil {
		path = absolutePath
	}

	if realPath, err := filepath.EvalSymlinks(path); err == nil {
		path = realPath
	}

	return strings.ToLower(filepath.Clean(path))
}

func findRemoteModuleHandle(pid uint32, dllPath string) (windows.Handle, error) {
	moduleHandle, found, err := findRemoteModule(pid, dllPath)
	if err != nil {
		return 0, err
	}
	if !found {
		return 0, fmt.Errorf("未在进程 %d 中找到 DLL: %s", pid, dllPath)
	}
	return moduleHandle, nil
}

func findRemoteModule(pid uint32, dllPath string) (windows.Handle, bool, error) {
	targetPath := normalizePathForMatch(dllPath)
	if targetPath == "" {
		return 0, false, errors.New("DLL 路径不能为空")
	}

	queryBaseName := strings.ToLower(filepath.Base(strings.TrimSpace(dllPath)))
	matchByBaseName := filepath.Dir(strings.TrimSpace(dllPath)) == "."

	snapshot, err := windows.CreateToolhelp32Snapshot(windows.TH32CS_SNAPMODULE|windows.TH32CS_SNAPMODULE32, pid)
	if err != nil {
		return 0, false, fmt.Errorf("创建模块快照失败(pid=%d): %w", pid, err)
	}
	defer windows.CloseHandle(snapshot)

	var entry windows.ModuleEntry32
	entry.Size = uint32(windows.SizeofModuleEntry32)

	if err := windows.Module32First(snapshot, &entry); err != nil {
		return 0, false, fmt.Errorf("读取首个模块失败(pid=%d): %w", pid, err)
	}

	for {
		modulePath := windows.UTF16ToString(entry.ExePath[:])
		moduleName := windows.UTF16ToString(entry.Module[:])

		if normalizePathForMatch(modulePath) == targetPath {
			return entry.ModuleHandle, true, nil
		}

		if matchByBaseName && strings.EqualFold(moduleName, queryBaseName) {
			return entry.ModuleHandle, true, nil
		}

		err = windows.Module32Next(snapshot, &entry)
		if err != nil {
			if errors.Is(err, windows.ERROR_NO_MORE_FILES) {
				break
			}
			return 0, false, fmt.Errorf("遍历模块失败(pid=%d): %w", pid, err)
		}
	}

	return 0, false, nil
}

func writeRemoteUTF16(processHandle windows.Handle, remoteAddress uintptr, data []uint16) error {
	if len(data) == 0 {
		return errors.New("写入远程进程的数据不能为空")
	}

	expectedSize := uintptr(len(data)) * unsafe.Sizeof(data[0])
	var written uintptr
	if err := windows.WriteProcessMemory(processHandle, remoteAddress, (*byte)(unsafe.Pointer(&data[0])), expectedSize, &written); err != nil {
		return fmt.Errorf("写入远程进程内存失败: %w", err)
	}
	if written != expectedSize {
		return fmt.Errorf("写入远程进程内存不完整: %d/%d", written, expectedSize)
	}

	return nil
}

func waitForHandle(handle windows.Handle, timeoutMilliseconds uint32, timeoutMessage string) error {
	waitResult, err := windows.WaitForSingleObject(handle, timeoutMilliseconds)
	if err != nil {
		return fmt.Errorf("等待句柄失败: %w", err)
	}

	switch waitResult {
	case windows.WAIT_OBJECT_0:
		return nil
	case uint32(windows.WAIT_TIMEOUT):
		return errors.New(timeoutMessage)
	default:
		return fmt.Errorf("等待句柄返回异常状态: %d", waitResult)
	}
}

func virtualAllocEx(processHandle windows.Handle, address uintptr, size uintptr, allocationType uint32, protect uint32) (uintptr, error) {
	result, _, err := virtualAllocExProc.Call(
		uintptr(processHandle),
		address,
		size,
		uintptr(allocationType),
		uintptr(protect),
	)
	if result == 0 {
		return 0, fmt.Errorf("VirtualAllocEx 调用失败: %w", normalizeCallError(err, "VirtualAllocEx 调用失败"))
	}
	return result, nil
}

func virtualFreeEx(processHandle windows.Handle, address uintptr, size uintptr, freeType uint32) error {
	result, _, err := virtualFreeExProc.Call(
		uintptr(processHandle),
		address,
		size,
		uintptr(freeType),
	)
	if result == 0 {
		return fmt.Errorf("VirtualFreeEx 调用失败: %w", normalizeCallError(err, "VirtualFreeEx 调用失败"))
	}
	return nil
}

func createRemoteThread(processHandle windows.Handle, startAddress uintptr, parameter uintptr) (windows.Handle, error) {
	result, _, err := createRemoteThreadProc.Call(
		uintptr(processHandle),
		0,
		0,
		startAddress,
		parameter,
		0,
		0,
	)
	if result == 0 {
		return 0, normalizeCallError(err, "CreateRemoteThread 调用失败")
	}
	return windows.Handle(result), nil
}

func getExitCodeThread(threadHandle windows.Handle) (uint32, error) {
	var exitCode uint32
	result, _, err := getExitCodeThreadProc.Call(uintptr(threadHandle), uintptr(unsafe.Pointer(&exitCode)))
	if result == 0 {
		return 0, fmt.Errorf("GetExitCodeThread 调用失败: %w", normalizeCallError(err, "GetExitCodeThread 调用失败"))
	}
	return exitCode, nil
}

func normalizeCallError(callErr error, fallback string) error {
	if callErr == nil || errors.Is(callErr, windows.ERROR_SUCCESS) {
		return errors.New(fallback)
	}
	return callErr
}

func getWindowDPI(windowHandle windows.Handle) uint32 {
	if windowHandle == 0 {
		return defaultWindowDPI
	}

	result, _, err := getDpiForWindowProc.Call(uintptr(windowHandle))
	if result == 0 {
		if errors.Is(err, windows.ERROR_PROC_NOT_FOUND) {
			return defaultWindowDPI
		}
		return defaultWindowDPI
	}

	return uint32(result)
}

func physicalToLogicalPixels(value int, dpi uint32) int {
	if dpi == 0 {
		return value
	}

	numerator := int64(value) * defaultWindowDPI
	denominator := int64(dpi)

	if numerator >= 0 {
		return int((numerator + denominator/2) / denominator)
	}

	return int((numerator - denominator/2) / denominator)
}

func convertPhysicalBoundsToLogical(bounds *WindowBounds, dpi uint32) *WindowBounds {
	if bounds == nil {
		return nil
	}

	converted := &WindowBounds{
		X:      physicalToLogicalPixels(bounds.X, dpi),
		Y:      physicalToLogicalPixels(bounds.Y, dpi),
		Width:  physicalToLogicalPixels(bounds.Width, dpi),
		Height: physicalToLogicalPixels(bounds.Height, dpi),
	}

	if converted.Width < 1 {
		converted.Width = 1
	}
	if converted.Height < 1 {
		converted.Height = 1
	}

	return converted
}

type rawWindowRect struct {
	Left   int32
	Top    int32
	Right  int32
	Bottom int32
}

type rawPoint struct {
	X int32
	Y int32
}

type mainWindowEnumContext struct {
	pid        uint32
	mainWindow windows.Handle
	maxArea    int64
}

func enumMainWindowForPIDProc(hwnd uintptr, lParam uintptr) uintptr {
	context := (*mainWindowEnumContext)(unsafe.Pointer(lParam))
	if context == nil {
		return 1
	}

	candidate := windows.Handle(hwnd)
	if !isTopLevelVisibleWindowForPID(candidate, context.pid) {
		return 1
	}

	width, height, ok := getClientSize(candidate)
	if !ok {
		return 1
	}

	area := int64(width) * int64(height)
	if area > context.maxArea {
		context.maxArea = area
		context.mainWindow = candidate
	}

	return 1
}

func findMainWindowByPID(pid uint32) (windows.Handle, error) {
	if err := validatePID(pid); err != nil {
		return 0, err
	}

	foregroundWindow, ok := findForegroundWindowByPID(pid)
	if ok {
		return foregroundWindow, nil
	}

	context := &mainWindowEnumContext{pid: pid}
	enumWindowsProc.Call(findMainWindowCallback, uintptr(unsafe.Pointer(context)))
	runtime.KeepAlive(context)

	if context.mainWindow == 0 {
		return 0, fmt.Errorf("未找到进程主窗口(pid=%d)", pid)
	}

	return context.mainWindow, nil
}

func getWindowBounds(windowHandle windows.Handle) (*WindowBounds, error) {
	if windowHandle == 0 {
		return nil, errors.New("窗口句柄无效")
	}

	var clientRect rawWindowRect
	result, _, err := getClientRectProc.Call(uintptr(windowHandle), uintptr(unsafe.Pointer(&clientRect)))
	if result == 0 {
		return nil, fmt.Errorf("读取窗口客户区失败: %w", normalizeCallError(err, "GetClientRect 调用失败"))
	}

	width := int(clientRect.Right - clientRect.Left)
	height := int(clientRect.Bottom - clientRect.Top)
	if width <= 0 || height <= 0 {
		return nil, fmt.Errorf("读取到无效窗口尺寸: width=%d height=%d", width, height)
	}

	topLeft := rawPoint{
		X: clientRect.Left,
		Y: clientRect.Top,
	}
	result, _, err = clientToScreenProc.Call(uintptr(windowHandle), uintptr(unsafe.Pointer(&topLeft)))
	if result == 0 {
		return nil, fmt.Errorf("换算客户区屏幕坐标失败: %w", normalizeCallError(err, "ClientToScreen 调用失败"))
	}

	return &WindowBounds{
		X:      int(topLeft.X),
		Y:      int(topLeft.Y),
		Width:  width,
		Height: height,
	}, nil
}

func findForegroundWindowByPID(pid uint32) (windows.Handle, bool) {
	result, _, _ := getForegroundWindowProc.Call()
	if result == 0 {
		return 0, false
	}

	windowHandle := windows.Handle(result)
	if !isTopLevelVisibleWindowForPID(windowHandle, pid) {
		return 0, false
	}

	return windowHandle, true
}

func isTopLevelVisibleWindowForPID(windowHandle windows.Handle, pid uint32) bool {
	if windowHandle == 0 {
		return false
	}

	var windowPID uint32
	getWindowThreadPIDProc.Call(uintptr(windowHandle), uintptr(unsafe.Pointer(&windowPID)))
	if windowPID != pid {
		return false
	}

	visible, _, _ := isWindowVisibleProc.Call(uintptr(windowHandle))
	if visible == 0 {
		return false
	}

	owner, _, _ := getWindowProc.Call(uintptr(windowHandle), gwOwner)
	if owner != 0 {
		return false
	}

	return true
}

func getClientSize(windowHandle windows.Handle) (int, int, bool) {
	var clientRect rawWindowRect
	result, _, _ := getClientRectProc.Call(uintptr(windowHandle), uintptr(unsafe.Pointer(&clientRect)))
	if result == 0 {
		return 0, 0, false
	}

	width := int(clientRect.Right - clientRect.Left)
	height := int(clientRect.Bottom - clientRect.Top)
	if width <= 0 || height <= 0 {
		return 0, 0, false
	}

	return width, height, true
}
