//go:build windows

package main

import (
	"crypto/sha256"
	"embed"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
)

const preferredEmbeddedDLLPath = "payload/MyDLL.dll"

//go:embed payload/*
var embeddedPayloadFS embed.FS

var (
	embeddedDLLPathOnce sync.Once
	embeddedDLLPath     string
	embeddedDLLErr      error
)

func resolveDLLPath(dllPath string) (string, error) {
	dllPath = strings.TrimSpace(dllPath)
	if dllPath != "" {
		return resolveExistingPath(dllPath)
	}

	return resolveEmbeddedDLLPath()
}

func resolveEmbeddedDLLPath() (string, error) {
	embeddedDLLPathOnce.Do(func() {
		embeddedDLLPath, embeddedDLLErr = materializeEmbeddedDLL()
	})

	if embeddedDLLErr != nil {
		return "", embeddedDLLErr
	}
	return embeddedDLLPath, nil
}

func materializeEmbeddedDLL() (string, error) {
	embeddedDLLPayload, embeddedDLLSourcePath, err := readEmbeddedDLLPayload()
	if err != nil {
		return "", err
	}

	if len(embeddedDLLPayload) < 2 || embeddedDLLPayload[0] != 'M' || embeddedDLLPayload[1] != 'Z' {
		return "", fmt.Errorf("内置 DLL 无效，请用真实 DLL 覆盖 %s 后重新构建", embeddedDLLSourcePath)
	}

	cacheRoot, err := os.UserCacheDir()
	if err != nil || strings.TrimSpace(cacheRoot) == "" {
		cacheRoot = os.TempDir()
	}
	if strings.TrimSpace(cacheRoot) == "" {
		return "", errors.New("无法确定缓存目录来释放内置 DLL")
	}

	payloadHash := sha256.Sum256(embeddedDLLPayload)
	fileName := fmt.Sprintf("pvz_rh_payload_%x.dll", payloadHash[:8])
	targetDir := filepath.Join(cacheRoot, "pvz_rh_hack", "embedded")
	targetPath := filepath.Join(targetDir, fileName)

	if err := os.MkdirAll(targetDir, 0o755); err != nil {
		return "", fmt.Errorf("创建内置 DLL 缓存目录失败: %w", err)
	}

	needsWrite := true
	existingData, err := os.ReadFile(targetPath)
	if err == nil {
		if sha256.Sum256(existingData) == payloadHash {
			needsWrite = false
		}
	} else if !errors.Is(err, os.ErrNotExist) {
		return "", fmt.Errorf("读取缓存 DLL 失败: %w", err)
	}

	if needsWrite {
		if err := os.WriteFile(targetPath, embeddedDLLPayload, 0o600); err != nil {
			return "", fmt.Errorf("写入内置 DLL 到缓存目录失败: %w", err)
		}
	}

	return filepath.Clean(targetPath), nil
}

func readEmbeddedDLLPayload() ([]byte, string, error) {
	payload, err := embeddedPayloadFS.ReadFile(preferredEmbeddedDLLPath)
	if err == nil {
		return payload, preferredEmbeddedDLLPath, nil
	}

	matches, globErr := fs.Glob(embeddedPayloadFS, "payload/*.dll")
	if globErr != nil {
		return nil, "", fmt.Errorf("查找内置 DLL 失败: %w", globErr)
	}
	if len(matches) == 0 {
		return nil, "", errors.New("未找到内置 DLL，请将 DLL 放到 payload 目录（推荐命名 pvz_rh_payload.dll）")
	}

	sort.Strings(matches)
	fallbackPath := matches[0]
	payload, err = embeddedPayloadFS.ReadFile(fallbackPath)
	if err != nil {
		return nil, "", fmt.Errorf("读取内置 DLL 失败(%s): %w", fallbackPath, err)
	}
	return payload, fallbackPath, nil
}
