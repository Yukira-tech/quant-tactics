/*
@file watcher.go
@brief 运行时文件变化监听器

层级：
    backend/netService/goServer/

项目内绝对路径：
    backend/netService/goServer/watcher.go

模块作用：
    以轮询方式监控 runtime_files/ 下关键文件和目录的修改时间，
    检测到变化后触发回调。

使用者：
    main.go 创建 FileWatcher 并启动监听循环。
    文件变化后触发 SnapshotManager.Build 重建快照。

项目角色：
    Go 服务的文件感知层，负责连接运行时文件与快照重建。

引入说明：
    依赖标准库 log、os、path/filepath、time。

维护记录：
    2026-08-28 初始创建
    2026-08-29 增加训练进度与思考目录监听，按 COMMENT_STYLE 规整注释
*/

package main

import (
	"log"
	"os"
	"path/filepath"
	"time"
)

// FileWatcher 基于轮询的文件变化监听器
type FileWatcher struct {
	runtimeDir string
	lastCheck  time.Time
}

// NewFileWatcher 创建文件监听器
func NewFileWatcher(runtimeDir string) *FileWatcher {
	return &FileWatcher{
		runtimeDir: runtimeDir,
		lastCheck:  time.Now(),
	}
}

// HasChanged 检查运行时文件是否有更新
// 返回 true 表示有文件被修改
func (fw *FileWatcher) HasChanged() bool {
	changed := false
	files := []string{
		filepath.Join(fw.runtimeDir, "portfolio", "snapshot.json"),
		filepath.Join(fw.runtimeDir, "signals"),
		filepath.Join(fw.runtimeDir, "positions"),
		filepath.Join(fw.runtimeDir, "training", "progress.json"),
		filepath.Join(fw.runtimeDir, "agent_thinking"),
	}

	for _, f := range files {
		info, err := os.Stat(f)
		if err != nil {
			continue
		}
		if info.ModTime().After(fw.lastCheck) {
			changed = true
		}
	}

	// 检查 signals/ 下的文件
	sigFiles, _ := filepath.Glob(filepath.Join(fw.runtimeDir, "signals", "*.json"))
	for _, f := range sigFiles {
		if info, err := os.Stat(f); err == nil {
			if info.ModTime().After(fw.lastCheck) {
				changed = true
			}
		}
	}

	// 检查 positions/ 下的文件
	posFiles, _ := filepath.Glob(filepath.Join(fw.runtimeDir, "positions", "*.json"))
	for _, f := range posFiles {
		if info, err := os.Stat(f); err == nil {
			if info.ModTime().After(fw.lastCheck) {
				changed = true
			}
		}
	}

	// 检查 agent_thinking/ 下的文件
	thinkFiles, _ := filepath.Glob(filepath.Join(fw.runtimeDir, "agent_thinking", "*.json"))
	for _, f := range thinkFiles {
		if info, err := os.Stat(f); err == nil {
			if info.ModTime().After(fw.lastCheck) {
				changed = true
			}
		}
	}

	fw.lastCheck = time.Now()
	return changed
}

// StartWatching 启动监听循环，有变化时触发回调
func (fw *FileWatcher) StartWatching(interval time.Duration, onChange func()) {
	go func() {
		for {
			if fw.HasChanged() {
				log.Println("[watcher] 检测到运行时文件更新")
				onChange()
			}
			time.Sleep(interval)
		}
	}()
}