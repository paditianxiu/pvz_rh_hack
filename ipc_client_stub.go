//go:build !windows

package main

import "errors"

type IPCClient struct{}

func NewIPCClient() (*IPCClient, error) {
	return nil, errors.New("当前平台不支持 Windows 命名管道 IPC")
}

func (c *IPCClient) Close() {}

func (c *IPCClient) SendCommand(command uint32, data []byte) ([]byte, error) {
	return nil, errors.New("当前平台不支持 Windows 命名管道 IPC")
}

func (c *IPCClient) SetFreeCD(enabled bool) error {
	return errors.New("当前平台不支持 Windows 命名管道 IPC")
}

func (c *IPCClient) GetFreeCD() (bool, error) {
	return false, errors.New("当前平台不支持 Windows 命名管道 IPC")
}
