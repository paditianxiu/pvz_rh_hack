//go:build windows

// ipc_client.go
package main

import (
	"errors"
	"fmt"
	"golang.org/x/sys/windows"
	"unsafe"
)

type CommandPacket struct {
	Command  uint32
	DataSize uint32
	Data     [256]byte
}

type ResponsePacket struct {
	Success  uint32
	DataSize uint32
	Data     [65535]byte
}

type IPCClient struct {
	handle windows.Handle
}

const (
	pipeName            = `\\.\pipe\PVZModPipe`
	pipeWaitTimeoutMs   = 1000
	maxIPCDataSize      = 256
	maxResponseDataSize = 65535
	pipeReadModeMessage = windows.PIPE_READMODE_MESSAGE
)

var (
	ipcKernel32DLL    = windows.NewLazySystemDLL("kernel32.dll")
	waitNamedPipeProc = ipcKernel32DLL.NewProc("WaitNamedPipeW")
)

func NewIPCClient() (*IPCClient, error) {
	path, err := windows.UTF16PtrFromString(pipeName)
	if err != nil {
		return nil, fmt.Errorf("命名管道路径编码失败: %w", err)
	}

	if err := waitNamedPipe(path, pipeWaitTimeoutMs); err != nil {
		return nil, fmt.Errorf("pipe not available: %w", err)
	}

	handle, err := windows.CreateFile(
		path,
		windows.GENERIC_READ|windows.GENERIC_WRITE,
		0,
		nil,
		windows.OPEN_EXISTING,
		windows.FILE_ATTRIBUTE_NORMAL,
		0,
	)
	if err != nil {
		return nil, fmt.Errorf("failed to open pipe: %w", err)
	}

	// 设置为消息模式
	mode := uint32(pipeReadModeMessage)
	err = windows.SetNamedPipeHandleState(handle, &mode, nil, nil)
	if err != nil {
		windows.CloseHandle(handle)
		return nil, fmt.Errorf("failed to set pipe mode: %w", err)
	}

	return &IPCClient{handle: handle}, nil
}

func (c *IPCClient) Close() {
	if c.handle != 0 {
		_ = windows.CloseHandle(c.handle)
		c.handle = 0
	}
}

func (c *IPCClient) SendCommand(command uint32, data []byte) ([]byte, error) {
	if len(data) > maxIPCDataSize {
		return nil, fmt.Errorf("命令数据过大: %d > %d", len(data), maxIPCDataSize)
	}

	packet := CommandPacket{
		Command:  command,
		DataSize: uint32(len(data)),
	}
	copy(packet.Data[:], data)

	var bytesWritten uint32
	packetBytes := unsafe.Slice((*byte)(unsafe.Pointer(&packet)), int(unsafe.Sizeof(packet)))
	err := windows.WriteFile(c.handle, packetBytes, &bytesWritten, nil)
	if err != nil {
		return nil, fmt.Errorf("write failed: %w", err)
	}

	var resp ResponsePacket
	var bytesRead uint32
	respBytes := unsafe.Slice((*byte)(unsafe.Pointer(&resp)), int(unsafe.Sizeof(resp)))
	err = windows.ReadFile(c.handle, respBytes, &bytesRead, nil)
	if err != nil {
		return nil, fmt.Errorf("read failed: %w", err)
	}

	if resp.DataSize > maxResponseDataSize || resp.DataSize > uint32(len(resp.Data)) {
		return nil, fmt.Errorf("响应数据非法: %d", resp.DataSize)
	}

	if resp.Success == 0 {
		return nil, fmt.Errorf("command failed: %s", string(resp.Data[:resp.DataSize]))
	}

	return resp.Data[:resp.DataSize], nil
}

func waitNamedPipe(path *uint16, timeoutMs uint32) error {
	result, _, err := waitNamedPipeProc.Call(uintptr(unsafe.Pointer(path)), uintptr(timeoutMs))
	if result != 0 {
		return nil
	}

	if err == nil || errors.Is(err, windows.ERROR_SUCCESS) {
		return errors.New("WaitNamedPipeW 调用失败")
	}

	return err
}

func isRecoverablePipeError(err error) bool {
	return errors.Is(err, windows.ERROR_BROKEN_PIPE) ||
		errors.Is(err, windows.ERROR_NO_DATA) ||
		errors.Is(err, windows.ERROR_PIPE_NOT_CONNECTED) ||
		errors.Is(err, windows.ERROR_SEM_TIMEOUT) ||
		errors.Is(err, windows.ERROR_PIPE_BUSY) ||
		errors.Is(err, windows.ERROR_FILE_NOT_FOUND)
}
