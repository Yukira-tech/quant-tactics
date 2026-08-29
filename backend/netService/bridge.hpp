/*
@file main.go
@brief Go 服务器主入口：路由注册、WebSocket、静态托管

层级：
    backend/netService/goServer/

项目内绝对路径：
    backend/netService/goServer/main.go

模块作用：
    启动 HTTP 服务器，注册快照、组合、信号、训练、思考等 API 路由，
    提供 WebSocket 实时推送，并静态托管前端页面。

使用者：
    go run . 启动后，浏览器访问 http://localhost:8080 使用前端监控面板。
    C++ 与 YukiPilot 通过 runtime_files/ 间接提供数据。

项目角色：
    Go 服务层入口，是所有 HTTP/WS 请求的分发中心。

引入说明：
    依赖标准库 encoding/json、log、net/http、time。
    依赖 github.com/gorilla/websocket 提供 WebSocket 升级能力。

维护记录：
    2026-08-28 初始创建
    2026-08-29 增加 training/thinking 接口与前端静态托管
*/

package main

import (
	"encoding/json"
	"log"
	"net/http"
	"time"

	"github.com/gorilla/websocket"
)

var (
	snapshotMgr *SnapshotManager
	watcher     *FileWatcher
)

// 前端静态目录（相对 goServer 目录）
const frontendDir = "../../frontend"

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

// 返回完整快照
func snapshotHandler(w http.ResponseWriter, r *http.Request) {
	snap := snapshotMgr.Get()
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	json.NewEncoder(w).Encode(snap)
}

// 返回组合信息
func portfolioHandler(w http.ResponseWriter, r *http.Request) {
	snap := snapshotMgr.Get()
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"total_equity":     snap.TotalEquity,
		"total_pnl":        snap.TotalPnl,
		"max_drawdown":     snap.MaxDrawdown,
		"target_portfolio": snap.TargetPortfolio,
	})
}

// 返回所有信号
func signalsHandler(w http.ResponseWriter, r *http.Request) {
	snap := snapshotMgr.Get()
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	json.NewEncoder(w).Encode(snap.Signals)
}

// 返回训练进度；无训练文件时返回 idle
func trainingHandler(w http.ResponseWriter, r *http.Request) {
	snap := snapshotMgr.Get()
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	if snap.Training == nil {
		json.NewEncoder(w).Encode(map[string]string{"state": "idle"})
		return
	}
	json.NewEncoder(w).Encode(snap.Training)
}

// 返回全部 Agent 思考状态；空时返回 [] 而非 null
func thinkingHandler(w http.ResponseWriter, r *http.Request) {
	snap := snapshotMgr.Get()
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	thinking := snap.Thinking
	if thinking == nil {
		thinking = []ThinkingState{}
	}
	json.NewEncoder(w).Encode(thinking)
}

// WebSocket 实时推送快照
func wsHandler(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println("[ws] upgrade failed:", err)
		return
	}
	defer conn.Close()

	log.Println("[ws] client connected")

	// 连接建立后立即推送一次，之后每 2 秒推送
	sendSnapshot(conn)

	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		if err := sendSnapshot(conn); err != nil {
			log.Println("[ws] send failed:", err)
			return
		}
	}
}

func sendSnapshot(conn *websocket.Conn) error {
	snap := snapshotMgr.Get()
	return conn.WriteJSON(snap)
}

func main() {
	runtimeDir := "../runtime_files"

	snapshotMgr = NewSnapshotManager()
	watcher = NewFileWatcher(runtimeDir)

	// 启动时先构建一次快照
	if err := snapshotMgr.Build(runtimeDir); err != nil {
		log.Println("[init] 快照构建失败:", err)
	}
	log.Println("[init] 初始快照:", snapshotMgr.Get().String())

	// 监听运行时文件变化，有更新就重建快照
	watcher.StartWatching(1*time.Second, func() {
		if err := snapshotMgr.Build(runtimeDir); err != nil {
			log.Println("[watcher] 快照重建失败:", err)
		}
	})

	// 先注册具体 API 路由，最后注册静态文件兜底
	http.HandleFunc("/api/snapshot", snapshotHandler)
	http.HandleFunc("/api/portfolio", portfolioHandler)
	http.HandleFunc("/api/signals", signalsHandler)
	http.HandleFunc("/api/training", trainingHandler)
	http.HandleFunc("/api/thinking", thinkingHandler)
	http.HandleFunc("/ws", wsHandler)

	// 静态托管前端
	fileServer := http.FileServer(http.Dir(frontendDir))
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/" {
			http.Redirect(w, r, "/public/index.html", http.StatusFound)
			return
		}
		fileServer.ServeHTTP(w, r)
	})

	addr := ":8080"
	log.Printf("Go 服务器启动: http://localhost%s", addr)
	log.Printf("  - 快照接口: http://localhost%s/api/snapshot", addr)
	log.Printf("  - 组合接口: http://localhost%s/api/portfolio", addr)
	log.Printf("  - 信号接口: http://localhost%s/api/signals", addr)
	log.Printf("  - 训练接口: http://localhost%s/api/training", addr)
	log.Printf("  - 思考接口: http://localhost%s/api/thinking", addr)
	log.Printf("  - 前端入口: http://localhost%s/", addr)
	log.Printf("  - WebSocket: ws://localhost%s/ws", addr)

	if err := http.ListenAndServe(addr, nil); err != nil {
		log.Fatal(err)
	}
}
