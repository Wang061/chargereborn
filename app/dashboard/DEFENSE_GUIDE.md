# ChargeReborn 移动端网页答辩说明

## 1. 网页功能介绍

本网页是 ChargeReborn 退役锂电池智能拆解系统的远程监控与控制终端，面向大赛现场答辩展示。它集中展示四项能力：

1. WebRTC 实时视频监控：通过局域网或云端信令查看 ESP32-S3 Brain 摄像头画面。
2. 远程急停：网页一键下发 ESTOP 指令，Brain 再联动 Steward 执行安全停机。
3. 电池全流程溯源：输入电池唯一 ID，查看识别、抓取、切割、归档等时间戳。
4. 碳减排仪表盘：展示累计、今日、本周节约 CO₂ 克数，并用轻量 Canvas 图表可视化。

## 2. 技术实现思路

- 前端是纯静态 SPA：`index.html`、`styles.css`、`app.js`，可直接本地打开。
- 双向通信使用 WebSocket，统一 JSON 协议，便于 ESP32-S3 Brain 或云端服务解析。
- 视频优先使用浏览器原生 WebRTC：Brain/云端发送 Offer，网页返回 Answer，并交换 ICE。
- 若现场硬件视频链路尚未完全打通，可用 `video_frame` 消息发送 JPEG Base64 帧作为回退。
- 图表不依赖第三方付费库，使用原生 Canvas 绘制近七日 CO₂ 节约柱状图。
- 页面内置演示模式：未连接硬件时会自动刷新模拟状态，便于投屏演示 UI 和答辩逻辑。

## 3. 答辩讲解话术

可以按下面顺序讲：

1. “这是我们的远程监控终端，用于现场查看 Brain 模块状态。顶部绿色/红色指示灯表示云端 WebSocket 是否在线。”
2. “左侧实时画面区域对接 Brain 的 OV2640 摄像头视频流，画面下方同步显示当前 AI 识别状态和推理 FPS。”
3. “右侧红色按钮是远程急停。点击后需要二次确认，确认后网页通过 WebSocket 发送 ESTOP 指令，Brain 再通过 ESP-NOW 通知机械臂控制端停机。”
4. “溯源模块对应欧盟电池护照监管思路。每节电池生成唯一 ID，处理流程中的识别、抓取、切割、归档时间都会被记录，后续可追溯。”
5. “碳排放仪表盘把每节电池理论节约 CO₂ 克数进行累计，展示今日、本周和总量，让环保收益在答辩现场可视化。”

## 4. 现场部署步骤

### 方案 A：直接本地打开

1. 打开 `chargereborn-master/app/dashboard/index.html`。
2. 在顶部 `Brain HTTP` 输入框填入 `http://192.168.4.1`。
3. 点击“连接固件”。
4. 页面会显示 `http://192.168.4.1:81/stream` 的 MJPEG 视频，并轮询 `/detect` 与 `/arm_target`。

### 方案 B：本地代理 / Mock 服务（推荐）

如果浏览器跨域限制导致 `/detect` 或 `/arm_target` 不能读取，用本地代理方式：

```powershell
cd C:\Users\lenovo\Desktop\Embed\chargereborn-master\app\dashboard
$env:BRAIN_HTTP="http://192.168.4.1"
node mock-server.js
```

然后访问：

```text
http://localhost:8088
```

打开后点击“本地代理”。页面会通过 `/brain/detect`、`/brain/arm_target`、`/brain/stream` 访问真实 Brain。

无硬件演示时，直接运行：

```powershell
node mock-server.js
```

不设置 `BRAIN_HTTP` 时默认返回模拟数据。

## 5. Brain 端快速对接建议

1. Brain 建立 WebSocket 客户端或服务端，按 `PROTOCOL.md` 收发 JSON。
2. 周期性发送 `device_status` 和 `carbon_update`。
3. 每处理一节电池后发送 `battery_log`。
4. 收到 `control + ESTOP` 后立即执行本地急停，并返回 `command_ack`。
5. 视频链路先用 `video_frame` 回退方案保底，再逐步升级为 WebRTC。

