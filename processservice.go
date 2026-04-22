package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math"
	"strconv"
	"strings"
	"sync"
	"time"
)

type ProcessService struct{}

type ProcessInfo struct {
	PID         uint32 `json:"pid"`
	ParentPID   uint32 `json:"parentPid"`
	Name        string `json:"name"`
	ThreadCount uint32 `json:"threadCount"`
}

type WindowBounds struct {
	X      int `json:"x"`
	Y      int `json:"y"`
	Width  int `json:"width"`
	Height int `json:"height"`
}

var (
	ipcClient    *IPCClient
	ipcClientMux sync.Mutex
	ipcCallMux   sync.Mutex
)

const (
	ipcConnectRetryCount = 5
	ipcConnectRetryDelay = 200 * time.Millisecond

	cmdInvoke uint32 = 0

	rpcValueNull    byte = 0
	rpcValueBool    byte = 1
	rpcValueInt32   byte = 2
	rpcValueFloat64 byte = 3
	rpcValueString  byte = 4
)

func getIPCClient() (*IPCClient, error) {
	ipcClientMux.Lock()
	defer ipcClientMux.Unlock()

	if ipcClient == nil {
		client, err := NewIPCClient()
		if err != nil {
			return nil, err
		}
		ipcClient = client
	}
	return ipcClient, nil
}

func getIPCClientWithRetry() (*IPCClient, error) {
	var lastErr error
	for attempt := 0; attempt < ipcConnectRetryCount; attempt++ {
		client, err := getIPCClient()
		if err == nil {
			return client, nil
		}

		lastErr = err
		if attempt < ipcConnectRetryCount-1 {
			time.Sleep(ipcConnectRetryDelay)
		}
	}

	return nil, lastErr
}

func resetIPCClient() {
	ipcClientMux.Lock()
	defer ipcClientMux.Unlock()

	if ipcClient != nil {
		ipcClient.Close()
		ipcClient = nil
	}
}

func trimNullTerminator(data []byte) string {
	return strings.TrimRight(string(data), "\x00")
}

func appendInvokeValue(payload []byte, value any) ([]byte, error) {
	switch v := value.(type) {
	case nil:
		return append(payload, rpcValueNull), nil
	case bool:
		if v {
			return append(payload, rpcValueBool, 1), nil
		}
		return append(payload, rpcValueBool, 0), nil
	case int, int8, int16, int32, int64, uint, uint8, uint16, uint32, uint64, float32, float64, json.Number:
		intValue, intErr := parseValue(v)
		if intErr == nil {
			next := append(payload, rpcValueInt32)
			buf := make([]byte, 4)
			binary.LittleEndian.PutUint32(buf, uint32(intValue))
			return append(next, buf...), nil
		}

		floatValue, floatErr := strconv.ParseFloat(fmt.Sprintf("%v", v), 64)
		if floatErr != nil {
			return nil, fmt.Errorf("数值参数编码失败: %w", intErr)
		}

		next := append(payload, rpcValueFloat64)
		buf := make([]byte, 8)
		binary.LittleEndian.PutUint64(buf, math.Float64bits(floatValue))
		return append(next, buf...), nil
	case string:
		raw := []byte(v)
		if len(raw) > math.MaxUint16 {
			return nil, fmt.Errorf("字符串参数过长: %d", len(raw))
		}

		next := append(payload, rpcValueString)
		lenBuf := make([]byte, 2)
		binary.LittleEndian.PutUint16(lenBuf, uint16(len(raw)))
		next = append(next, lenBuf...)
		return append(next, raw...), nil
	default:
		raw, err := json.Marshal(v)
		if err != nil {
			return nil, fmt.Errorf("不支持的参数类型: %T", v)
		}
		if len(raw) > math.MaxUint16 {
			return nil, fmt.Errorf("JSON 参数过长: %d", len(raw))
		}

		next := append(payload, rpcValueString)
		lenBuf := make([]byte, 2)
		binary.LittleEndian.PutUint16(lenBuf, uint16(len(raw)))
		next = append(next, lenBuf...)
		return append(next, raw...), nil
	}
}

func encodeInvokePayload(functionName string, params []any) ([]byte, error) {
	functionName = strings.TrimSpace(functionName)
	if functionName == "" {
		return nil, fmt.Errorf("函数名不能为空")
	}

	nameBytes := []byte(functionName)
	if len(nameBytes) > math.MaxUint8 {
		return nil, fmt.Errorf("函数名过长: %d", len(nameBytes))
	}
	if len(params) > math.MaxUint8 {
		return nil, fmt.Errorf("参数数量过多: %d", len(params))
	}

	payload := make([]byte, 0, maxIPCDataSize)
	payload = append(payload, byte(len(nameBytes)))
	payload = append(payload, nameBytes...)
	payload = append(payload, byte(len(params)))

	var err error
	for _, param := range params {
		payload, err = appendInvokeValue(payload, param)
		if err != nil {
			return nil, err
		}
		if len(payload) > maxIPCDataSize {
			return nil, fmt.Errorf("调用数据过大: %d > %d", len(payload), maxIPCDataSize)
		}
	}

	return payload, nil
}

func decodeInvokeResponse(data []byte) (any, error) {
	if len(data) == 0 {
		return nil, nil
	}

	valueType := data[0]
	offset := 1

	switch valueType {
	case rpcValueNull:
		if offset != len(data) {
			return nil, fmt.Errorf("空值响应包含多余数据")
		}
		return nil, nil
	case rpcValueBool:
		if len(data) < offset+1 {
			return nil, fmt.Errorf("布尔响应数据不足")
		}
		return data[offset] != 0, nil
	case rpcValueInt32:
		if len(data) < offset+4 {
			return nil, fmt.Errorf("int32 响应数据不足")
		}
		return int32(binary.LittleEndian.Uint32(data[offset : offset+4])), nil
	case rpcValueFloat64:
		if len(data) < offset+8 {
			return nil, fmt.Errorf("float64 响应数据不足")
		}
		return math.Float64frombits(binary.LittleEndian.Uint64(data[offset : offset+8])), nil
	case rpcValueString:
		if len(data) < offset+2 {
			return nil, fmt.Errorf("字符串响应长度不足")
		}
		strLen := int(binary.LittleEndian.Uint16(data[offset : offset+2]))
		offset += 2
		if len(data) < offset+strLen {
			return nil, fmt.Errorf("字符串响应内容不足")
		}
		return string(data[offset : offset+strLen]), nil
	default:
		return nil, fmt.Errorf("未知响应类型: %d", valueType)
	}
}

func parseValue(value any) (int32, error) {
	switch v := value.(type) {
	case int:
		if v < math.MinInt32 || v > math.MaxInt32 {
			return 0, fmt.Errorf("超出 int32 范围: %d", v)
		}
		return int32(v), nil
	case int8:
		return int32(v), nil
	case int16:
		return int32(v), nil
	case int32:
		return v, nil
	case int64:
		if v < math.MinInt32 || v > math.MaxInt32 {
			return 0, fmt.Errorf("超出 int32 范围: %d", v)
		}
		return int32(v), nil
	case uint:
		if v > math.MaxInt32 {
			return 0, fmt.Errorf("超出 int32 范围: %d", v)
		}
		return int32(v), nil
	case uint8:
		return int32(v), nil
	case uint16:
		return int32(v), nil
	case uint32:
		if v > math.MaxInt32 {
			return 0, fmt.Errorf("超出 int32 范围: %d", v)
		}
		return int32(v), nil
	case uint64:
		if v > math.MaxInt32 {
			return 0, fmt.Errorf("超出 int32 范围: %d", v)
		}
		return int32(v), nil
	case float32:
		if math.Trunc(float64(v)) != float64(v) {
			return 0, fmt.Errorf("必须是整数: %v", v)
		}
		if v < math.MinInt32 || v > math.MaxInt32 {
			return 0, fmt.Errorf("超出 int32 范围: %v", v)
		}
		return int32(v), nil
	case float64:
		if math.Trunc(v) != v {
			return 0, fmt.Errorf("必须是整数: %v", v)
		}
		if v < math.MinInt32 || v > math.MaxInt32 {
			return 0, fmt.Errorf("超出 int32 范围: %v", v)
		}
		return int32(v), nil
	case string:
		sunValue, err := strconv.ParseInt(strings.TrimSpace(v), 10, 32)
		if err != nil {
			return 0, fmt.Errorf("格式错误: %w", err)
		}
		return int32(sunValue), nil
	case json.Number:
		sunValue, err := strconv.ParseInt(v.String(), 10, 32)
		if err != nil {
			return 0, fmt.Errorf("格式错误: %w", err)
		}
		return int32(sunValue), nil
	default:
		return 0, fmt.Errorf("不支持的参数类型: %T", value)
	}
}

func encodeGenericPayload(params []any) ([]byte, error) {
	switch len(params) {
	case 0:
		return nil, nil
	case 1:
		switch value := params[0].(type) {
		case nil:
			return nil, nil
		case []byte:
			return value, nil
		case string:
			return []byte(value), nil
		default:
			payload, err := json.Marshal(value)
			if err != nil {
				return nil, fmt.Errorf("参数编码失败: %w", err)
			}
			return payload, nil
		}
	default:
		payload, err := json.Marshal(params)
		if err != nil {
			return nil, fmt.Errorf("参数编码失败: %w", err)
		}
		return payload, nil
	}
}

func normalizeCommandParams(params []any) []any {
	if len(params) != 1 {
		return params
	}

	nested, ok := params[0].([]any)
	if !ok {
		return params
	}

	return nested
}

func (g *ProcessService) SendCommandString(command uint32, data string) (string, error) {
	return g.SendCommand(command, data)
}

func (g *ProcessService) Invoke(functionName string, params []any) (any, error) {
	payload, err := encodeInvokePayload(functionName, params)
	if err != nil {
		return nil, err
	}

	resp, err := g.sendCommandRaw(cmdInvoke, payload)
	if err != nil {
		return nil, err
	}

	return decodeInvokeResponse(resp)
}

func (g *ProcessService) SendCommand(command uint32, params ...any) (string, error) {
	params = normalizeCommandParams(params)

	payload, err := encodeGenericPayload(params)
	if err != nil {
		return "", err
	}

	resp, err := g.sendCommandRaw(command, payload)
	if err != nil {
		return "", err
	}

	return trimNullTerminator(resp), nil
}

func (g *ProcessService) sendCommandRaw(command uint32, data []byte) ([]byte, error) {
	ipcCallMux.Lock()
	defer ipcCallMux.Unlock()

	client, err := getIPCClientWithRetry()
	if err != nil {
		return nil, err
	}

	resp, err := client.SendCommand(command, data)
	if err == nil {
		return resp, nil
	}

	if !isRecoverablePipeError(err) {
		return nil, err
	}

	resetIPCClient()

	client, reconnectErr := getIPCClientWithRetry()
	if reconnectErr != nil {
		return nil, reconnectErr
	}

	return client.SendCommand(command, data)
}

func (g *ProcessService) ListProcesses() ([]ProcessInfo, error) {
	return listProcesses()
}

func (g *ProcessService) FindProcessesByName(processName string) ([]ProcessInfo, error) {
	return findProcessesByName(processName)
}

func (g *ProcessService) GetProcessPID(processName string) (uint32, error) {
	return getProcessPID(processName)
}

func (g *ProcessService) TerminateProcess(pid uint32) error {
	return terminateProcess(pid)
}

func (g *ProcessService) InjectDLL(pid uint32, dllPath string) error {
	return injectDLL(pid, dllPath)
}

func (g *ProcessService) UnloadDLL(pid uint32, dllPath string) error {
	return unloadDLL(pid, dllPath)
}

func (g *ProcessService) IsDLLInjected(pid uint32, dllPath string) (bool, error) {
	return isDLLInjected(pid, dllPath)
}

func (g *ProcessService) GetMainWindowBounds(processName string) (*WindowBounds, error) {
	return getMainWindowBounds(processName)
}

func (g *ProcessService) SetOverlayVisible(processName string, visible bool) error {
	return setOverlayVisible(processName, visible)
}

func (g *ProcessService) SyncOverlayWindow(processName string) error {
	return syncOverlayWindow(processName)
}
