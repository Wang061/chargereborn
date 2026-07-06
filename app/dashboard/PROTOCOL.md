# ChargeReborn Dashboard 通信协议

## 0. 当前固件 HTTP/MJPEG 兼容接口

当前 `chargereborn-master` Brain 固件已经暴露 HTTP/MJPEG 接口，dashboard 优先兼容这些接口：

| 接口 | 方法 | 说明 |
| --- | --- | --- |
| `http://192.168.4.1/` | GET | 固件自带采集/检测页面 |
| `http://192.168.4.1/detect` | GET | 读取 AI 检测缓存 JSON |
| `http://192.168.4.1/arm_target` | GET | 读取当前最佳机械臂抓取目标 |
| `http://192.168.4.1:81/stream` | GET | MJPEG 摄像头视频流 |

为避免本地页面跨域，可运行：

```powershell
cd C:\Users\lenovo\Desktop\Embed\chargereborn-master\app\dashboard
node mock-server.js
```

然后在网页点击“本地代理”。代理路径如下：

| Dashboard 路径 | 代理到 |
| --- | --- |
| `/brain/detect` | `http://192.168.4.1/detect` |
| `/brain/arm_target` | `http://192.168.4.1/arm_target` |
| `/brain/stream` | `http://192.168.4.1:81/stream` |

无硬件时 `mock-server.js` 默认返回模拟 `/brain/detect`、`/brain/arm_target`、`/brain/stream`，便于答辩演示。

---

本文定义网页监控终端与 ESP32-S3 Brain 模块经云端 WebSocket 通信的 JSON 格式。所有消息均为 UTF-8 JSON 字符串。

## 1. 连接

默认地址可在网页顶部输入框修改：

```text
ws://192.168.4.1:8080/ws
```

现场若走云端中转，可改为：

```text
wss://your-domain.example/ws/chargereborn
```

网页连接成功后发送：

```json
{
  "type": "hello",
  "client": "chargereborn_dashboard",
  "ts": 1783310400000
}
```

## 2. 网页上行到 Brain

### 2.1 远程急停

```json
{
  "type": "control",
  "command": "ESTOP",
  "level": "critical",
  "source": "web_dashboard",
  "ts": 1783310400000
}
```

Brain 收到后建议立即：

1. 进入本地急停状态。
2. 通过 ESP-NOW 通知 Steward 停止舵机输出。
3. 回传 `command_ack`。

### 2.2 请求刷新视频

```json
{
  "type": "request_video_refresh",
  "requestId": "uuid-or-timestamp",
  "ts": 1783310400000
}
```

### 2.3 查询电池溯源

```json
{
  "type": "trace_query",
  "batteryId": "BAT-20260706-001",
  "ts": 1783310400000
}
```

### 2.4 WebRTC Answer

Brain 或云端信令服务发送 offer 后，网页回传 answer：

```json
{
  "type": "webrtc_answer",
  "payload": {
    "sdp": "v=0..."
  },
  "ts": 1783310400000
}
```

### 2.5 WebRTC ICE

```json
{
  "type": "webrtc_ice",
  "payload": {
    "candidate": "candidate:...",
    "sdpMid": "0",
    "sdpMLineIndex": 0
  }
}
```

## 3. Brain 下行到网页

### 3.1 设备状态

```json
{
  "type": "device_status",
  "payload": {
    "mode": "自动处理",
    "aiStatus": "识别 18650",
    "fps": 7.4,
    "online": true
  },
  "ts": 1783310400000
}
```

### 3.2 指令确认

```json
{
  "type": "command_ack",
  "payload": {
    "command": "ESTOP",
    "ok": true,
    "message": "ESTOP accepted, Steward notified"
  },
  "ts": 1783310400000
}
```

### 3.3 电池处理日志

```json
{
  "type": "battery_log",
  "payload": {
    "id": "BAT-20260706-001",
    "model": "18650",
    "confidence": 0.94,
    "abnormal": false,
    "timestamps": {
      "identifiedAt": "2026-07-06 09:21:14",
      "pickedAt": "2026-07-06 09:21:17",
      "cutAt": "2026-07-06 09:21:21",
      "archivedAt": "2026-07-06 09:21:22"
    }
  },
  "ts": 1783310400000
}
```

### 3.4 碳减排数据

```json
{
  "type": "carbon_update",
  "payload": {
    "total": 1260,
    "today": 126,
    "week": 684,
    "series": [72, 90, 108, 126, 90, 108, 126]
  },
  "ts": 1783310400000
}
```

### 3.5 WebRTC Offer

```json
{
  "type": "webrtc_offer",
  "payload": {
    "sdp": "v=0..."
  },
  "ts": 1783310400000
}
```

### 3.6 WebRTC ICE

```json
{
  "type": "webrtc_ice",
  "payload": {
    "candidate": "candidate:...",
    "sdpMid": "0",
    "sdpMLineIndex": 0
  }
}
```

### 3.7 简化视频帧回退方案

若现场来不及跑通 WebRTC，可先用 JPEG Base64 帧展示：

```json
{
  "type": "video_frame",
  "payload": {
    "jpegBase64": "/9j/4AAQSkZJRgABAQ..."
  },
  "ts": 1783310400000
}
```

网页已内置该回退显示逻辑。
