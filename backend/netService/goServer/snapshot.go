/*
@file snapshot.go
@brief 快照管理器：从 runtime_files 读取并缓存最新状态

层级：
    backend/netService/goServer/

项目内绝对路径：
    backend/netService/goServer/snapshot.go

模块作用：
    从 runtime_files/ 读取持仓、信号、组合快照、训练进度和 Agent 思考状态，
    合并成 PortfolioSnapshot，并提供线程安全的缓存读取。

使用者：
    main.go 各 HTTP handler 通过 SnapshotManager.Get() 获取最新快照。
    watcher.go 检测到文件变化后触发 Build 重建快照。

项目角色：
    Go 服务的数据聚合层，负责把运行时文件翻译成前端可用的 JSON 结构。

引入说明：
    依赖标准库 encoding/json、fmt、log、os、path/filepath、sort、sync、time。

维护记录：
    2026-08-28 初始创建
    2026-08-29 增加训练进度与思考状态读取，按 COMMENT_STYLE 规整注释
*/

package main

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"
)

// Kline 单根K线
type Kline struct {
	Code   uint32  `json:"code"`
	Ymd    int32   `json:"ymd"`
	Open   float64 `json:"open"`
	High   float64 `json:"high"`
	Low    float64 `json:"low"`
	Close  float64 `json:"close"`
	Volume int64   `json:"volume"`
}

// FactorOutput 因子输出
type FactorOutput struct {
	Symbol       string  `json:"symbol"`
	Timestamp    string  `json:"timestamp"`
	MaShort      float64 `json:"ma_short"`
	MaLong       float64 `json:"ma_long"`
	DonchianHigh float64 `json:"donchian_high"`
	DonchianLow  float64 `json:"donchian_low"`
	Atr          float64 `json:"atr"`
}

// SignalOutput 信号输出
type SignalOutput struct {
	Symbol    string  `json:"symbol"`
	Timestamp string  `json:"timestamp"`
	Signal    string  `json:"signal"`
	Strength  float64 `json:"strength"`
	Source    string  `json:"strategy_source"`
}

// PositionOutput 持仓输出
type PositionOutput struct {
	Symbol             string  `json:"symbol"`
	Timestamp          string  `json:"timestamp"`
	Quantity           int64   `json:"quantity"`
	AvgCost            float64 `json:"avg_cost"`
	CurrentPrice       float64 `json:"current_price"`
	FloatingPnl        float64 `json:"floating_pnl"`
	InventoryAvailable int64   `json:"inventory_available"`
	InventoryFrozen    int64   `json:"inventory_frozen"`
}

// TrainingProgress 训练进度，对应 runtime_files/training/progress.json
type TrainingProgress struct {
	State        string         `json:"state"` // running | done | idle
	Epoch        int            `json:"epoch"`
	TotalEpochs  int            `json:"total_epochs"`
	Batch        int            `json:"batch"`
	TotalBatches int            `json:"total_batches"`
	Loss         float64        `json:"loss"`
	ValLoss      float64        `json:"val_loss"`
	ValAcc       float64        `json:"val_acc"`
	Speed        float64        `json:"speed"` // batch/秒
	EtaSeconds   float64        `json:"eta_seconds"`
	History      []EpochHistory `json:"history"`
	Timestamp    string         `json:"timestamp"`
}

// EpochHistory 单个 epoch 的历史记录
type EpochHistory struct {
	Epoch     int     `json:"epoch"`
	TrainLoss float64 `json:"train_loss"`
	TrainAcc  float64 `json:"train_acc"`
	ValLoss   float64 `json:"val_loss"`
	ValAcc    float64 `json:"val_acc"`
}

// ThinkingEvent 单条思考事件
type ThinkingEvent struct {
	Type string `json:"type"`           // status | tool | tool_result | text
	Name string `json:"name,omitempty"` // 工具名，仅 tool 类事件有
	Text string `json:"text"`
	T    string `json:"t"` // 事件时间，如 14:00:01
}

// ThinkingState 单只股票的思考状态，对应 runtime_files/agent_thinking/{code}.json
type ThinkingState struct {
	Symbol  string          `json:"symbol"`
	State   string          `json:"state"` // thinking | done
	Started string          `json:"started"`
	Events  []ThinkingEvent `json:"events"`
	Final   json.RawMessage `json:"final,omitempty"` // 最终决策，done 时才有
}

// PortfolioSnapshot 组合快照
type PortfolioSnapshot struct {
	Timestamp       string            `json:"timestamp"`
	TotalEquity     float64           `json:"total_equity"`
	TotalPnl        float64           `json:"total_pnl"`
	MaxDrawdown     float64           `json:"max_drawdown"`
	TargetPortfolio []TargetPosition  `json:"target_portfolio"`
	Signals         []SignalOutput    `json:"signals"`
	Positions       []PositionOutput  `json:"positions"`
	Training        *TrainingProgress `json:"training,omitempty"` // 无训练时为空
	Thinking        []ThinkingState   `json:"thinking,omitempty"` // LLM 未启用时为空
}

// TargetPosition 目标持仓
type TargetPosition struct {
	Symbol         string `json:"symbol"`
	TargetQuantity int64  `json:"target_quantity"`
}

// SnapshotManager 快照管理器，负责读取运行时文件
type SnapshotManager struct {
	mu         sync.RWMutex
	snapshot   PortfolioSnapshot
	lastUpdate time.Time
}

// NewSnapshotManager 创建快照管理器
func NewSnapshotManager() *SnapshotManager {
	return &SnapshotManager{}
}

// Build 从 runtime_files 构建最新快照
func (sm *SnapshotManager) Build(runtimeDir string) error {
	snapshot := PortfolioSnapshot{
		Timestamp: time.Now().Format("2006-01-02 15:04:05"),
	}

	// 读取持仓
	posFiles, _ := filepath.Glob(filepath.Join(runtimeDir, "positions", "*.json"))
	for _, f := range posFiles {
		data, err := os.ReadFile(f)
		if err != nil {
			continue
		}
		var pos PositionOutput
		if json.Unmarshal(data, &pos) == nil {
			snapshot.Positions = append(snapshot.Positions, pos)
			snapshot.TotalPnl += pos.FloatingPnl
		}
	}

	// 读取信号
	sigFiles, _ := filepath.Glob(filepath.Join(runtimeDir, "signals", "*.json"))
	for _, f := range sigFiles {
		data, err := os.ReadFile(f)
		if err != nil {
			continue
		}
		var sig SignalOutput
		if json.Unmarshal(data, &sig) == nil {
			snapshot.Signals = append(snapshot.Signals, sig)
		}
	}

	// 读取组合快照
	portfolioFile := filepath.Join(runtimeDir, "portfolio", "snapshot.json")
	if data, err := os.ReadFile(portfolioFile); err == nil {
		var ps struct {
			TotalEquity     float64          `json:"total_equity"`
			MaxDrawdown     float64          `json:"max_drawdown"`
			TargetPortfolio []TargetPosition `json:"target_portfolio"`
		}
		if json.Unmarshal(data, &ps) == nil {
			snapshot.TotalEquity = ps.TotalEquity
			snapshot.MaxDrawdown = ps.MaxDrawdown
			snapshot.TargetPortfolio = ps.TargetPortfolio
		}
	}

	// 读取训练进度，文件不存在或损坏时保持为空
	snapshot.Training = loadTrainingProgress(filepath.Join(runtimeDir, "training", "progress.json"))

	// 读取 Agent 思考状态，读失败的单个文件跳过不中断
	snapshot.Thinking = loadThinkingStates(runtimeDir)

	// 排序保证输出稳定
	sort.Slice(snapshot.Signals, func(i, j int) bool {
		return snapshot.Signals[i].Symbol < snapshot.Signals[j].Symbol
	})
	sort.Slice(snapshot.Positions, func(i, j int) bool {
		return snapshot.Positions[i].Symbol < snapshot.Positions[j].Symbol
	})

	sm.mu.Lock()
	sm.snapshot = snapshot
	sm.lastUpdate = time.Now()
	sm.mu.Unlock()

	return nil
}

// loadTrainingProgress 读取训练进度文件，不存在或解析失败时返回 nil
func loadTrainingProgress(path string) *TrainingProgress {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil
	}
	var tp TrainingProgress
	if err := json.Unmarshal(data, &tp); err != nil {
		log.Println("[snapshot] 训练进度文件解析失败，已跳过:", path, err)
		return nil
	}
	return &tp
}

// loadThinkingStates 读取 agent_thinking 目录下全部思考文件，按 symbol 排序
func loadThinkingStates(runtimeDir string) []ThinkingState {
	files, _ := filepath.Glob(filepath.Join(runtimeDir, "agent_thinking", "*.json"))
	if len(files) == 0 {
		return nil
	}
	var states []ThinkingState
	for _, f := range files {
		data, err := os.ReadFile(f)
		if err != nil {
			continue
		}
		var ts ThinkingState
		if err := json.Unmarshal(data, &ts); err != nil {
			log.Println("[snapshot] 思考文件解析失败，已跳过:", f, err)
			continue
		}
		states = append(states, ts)
	}
	sort.Slice(states, func(i, j int) bool {
		return states[i].Symbol < states[j].Symbol
	})
	return states
}

// Get 获取当前快照
func (sm *SnapshotManager) Get() PortfolioSnapshot {
	sm.mu.RLock()
	defer sm.mu.RUnlock()
	return sm.snapshot
}

// LastUpdate 获取最后更新时间
func (sm *SnapshotManager) LastUpdate() time.Time {
	sm.mu.RLock()
	defer sm.mu.RUnlock()
	return sm.lastUpdate
}

// String 调试输出
func (p PortfolioSnapshot) String() string {
	return fmt.Sprintf("快照[%s] 权益=%.2f 盈亏=%.2f 持仓数=%d 信号数=%d",
		p.Timestamp, p.TotalEquity, p.TotalPnl, len(p.Positions), len(p.Signals))
}