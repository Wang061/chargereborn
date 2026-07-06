const state = {
  socket: null,
  peer: null,
  connected: false,
  httpConnected: false,
  httpPollTimer: null,
  httpBaseUrl: "http://192.168.4.1",
  carbon: {
    total: 1260,
    today: 126,
    week: 684,
    series: [72, 90, 108, 126, 90, 108, 126],
  },
  traces: new Map(),
  demoTimer: null,
};

const els = {
  httpBaseUrl: document.querySelector("#httpBaseUrl"),
  httpConnectBtn: document.querySelector("#httpConnectBtn"),
  proxyBtn: document.querySelector("#proxyBtn"),
  wsUrl: document.querySelector("#wsUrl"),
  connectBtn: document.querySelector("#connectBtn"),
  statusDot: document.querySelector("#statusDot"),
  statusText: document.querySelector("#statusText"),
  statusChip: document.querySelector("#statusChip"),
  messageLine: document.querySelector("#messageLine"),
  refreshVideoBtn: document.querySelector("#refreshVideoBtn"),
  mjpegStream: document.querySelector("#mjpegStream"),
  remoteVideo: document.querySelector("#remoteVideo"),
  fallbackFrame: document.querySelector("#fallbackFrame"),
  videoStage: document.querySelector(".video-stage"),
  aiStatus: document.querySelector("#aiStatus"),
  aiFps: document.querySelector("#aiFps"),
  deviceMode: document.querySelector("#deviceMode"),
  armTarget: document.querySelector("#armTarget"),
  estopBtn: document.querySelector("#estopBtn"),
  confirmDialog: document.querySelector("#confirmDialog"),
  confirmEstopBtn: document.querySelector("#confirmEstopBtn"),
  lastCommand: document.querySelector("#lastCommand"),
  commandState: document.querySelector("#commandState"),
  traceForm: document.querySelector("#traceForm"),
  batteryId: document.querySelector("#batteryId"),
  traceResult: document.querySelector("#traceResult"),
  totalCo2: document.querySelector("#totalCo2"),
  todayCo2: document.querySelector("#todayCo2"),
  weekCo2: document.querySelector("#weekCo2"),
  carbonChart: document.querySelector("#carbonChart"),
};

const demoTrace = {
  id: "BAT-20260706-001",
  model: "18650",
  confidence: 0.94,
  abnormal: false,
  timestamps: {
    identifiedAt: "2026-07-06 09:21:14",
    pickedAt: "2026-07-06 09:21:17",
    cutAt: "2026-07-06 09:21:21",
    archivedAt: "2026-07-06 09:21:22",
  },
};

state.traces.set(demoTrace.id, demoTrace);

function setMessage(text) {
  els.messageLine.textContent = text;
}

function setConnectionStatus(connected) {
  state.connected = connected;
  updateOnlineIndicator();
  els.connectBtn.textContent = connected ? "断开" : "连接";
}

function updateOnlineIndicator() {
  const online = state.connected || state.httpConnected;
  els.statusDot.classList.toggle("online", online);
  els.statusDot.classList.toggle("offline", !online);
  els.statusText.textContent = online ? "在线" : "离线";
  els.deviceMode.textContent = online ? (state.httpConnected ? "固件直连" : "WebSocket") : "离线";
}

function sendMessage(payload) {
  if (!state.socket || state.socket.readyState !== WebSocket.OPEN) {
    setMessage("WebSocket 未连接，已进入演示反馈模式。");
    return false;
  }

  state.socket.send(JSON.stringify(payload));
  return true;
}

function connectWebSocket() {
  if (state.connected) {
    state.socket?.close();
    return;
  }

  const url = els.wsUrl.value.trim();
  if (!url) {
    setMessage("请填写 Brain WebSocket 地址。");
    return;
  }

  try {
    state.socket = new WebSocket(url);
  } catch (error) {
    setMessage(`连接失败：${error.message}`);
    return;
  }

  setMessage("正在连接 Brain 模块...");

  state.socket.addEventListener("open", () => {
    setConnectionStatus(true);
    setMessage("连接成功：远程监控终端已接入 Brain 模块。");
    sendMessage({ type: "hello", client: "chargereborn_dashboard", ts: Date.now() });
    requestVideoRefresh();
  });

  state.socket.addEventListener("message", (event) => {
    try {
      handleIncomingMessage(JSON.parse(event.data));
    } catch (error) {
      setMessage("收到非 JSON 数据，已忽略。");
    }
  });

  state.socket.addEventListener("close", () => {
    setConnectionStatus(false);
    setMessage("连接已断开。页面保留最近一次展示数据。");
    closePeer();
  });

  state.socket.addEventListener("error", () => {
    setMessage("WebSocket 连接异常，请检查云端地址或局域网。");
  });
}

function normalizeHttpBase(value) {
  const trimmed = value.trim().replace(/\/$/, "");
  return trimmed || "http://192.168.4.1";
}

function buildBrainUrl(pathname, options = {}) {
  const base = state.httpBaseUrl;
  if (base.startsWith("/")) return `${base}${pathname}`;

  const url = new URL(base);
  url.pathname = pathname;
  url.search = "";
  url.hash = "";
  if (options.streamPort) url.port = "81";
  return url.toString();
}

function connectHttpFirmware() {
  if (state.httpConnected) {
    disconnectHttpFirmware();
    return;
  }

  state.httpBaseUrl = normalizeHttpBase(els.httpBaseUrl.value);
  els.mjpegStream.src = buildBrainUrl("/stream", { streamPort: true });
  els.videoStage.classList.add("has-mjpeg");
  state.httpConnected = true;
  els.httpConnectBtn.textContent = "断开固件";
  updateOnlineIndicator();
  setMessage("已切换到固件 HTTP/MJPEG 模式：正在轮询 /detect 与 /arm_target。");
  pollFirmwareOnce();
  state.httpPollTimer = window.setInterval(pollFirmwareOnce, 600);
}

function disconnectHttpFirmware() {
  state.httpConnected = false;
  window.clearInterval(state.httpPollTimer);
  state.httpPollTimer = null;
  els.mjpegStream.removeAttribute("src");
  els.videoStage.classList.remove("has-mjpeg");
  els.httpConnectBtn.textContent = "连接固件";
  updateOnlineIndicator();
  setMessage("已断开固件 HTTP/MJPEG 模式。");
}

async function pollFirmwareOnce() {
  await Promise.allSettled([pollDetect(), pollArmTarget()]);
}

async function pollDetect() {
  const response = await fetch(buildBrainUrl("/detect"), { cache: "no-store" });
  if (!response.ok) throw new Error(`detect ${response.status}`);
  const data = await response.json();
  const first = data.boxes?.[0];
  const inferMs = Number(data.infer_ms || 0);
  updateDeviceStatus({
    aiStatus: data.n > 0 ? `识别 ${first?.name || "battery"} x${data.n}` : "搜索电池",
    fps: inferMs > 0 ? 1000 / inferMs : 0,
    mode: "固件直连",
  });

  if (first) {
    const id = `LIVE-${Date.now()}`;
    state.traces.set(id, {
      id,
      model: first.name,
      confidence: Number(first.s || 0),
      abnormal: false,
      timestamps: {
        identifiedAt: new Date().toLocaleString("zh-CN"),
        pickedAt: "-",
        cutAt: "-",
        archivedAt: "实时检测缓存",
      },
    });
  }
}

async function pollArmTarget() {
  const response = await fetch(buildBrainUrl("/arm_target"), { cache: "no-store" });
  if (!response.ok) throw new Error(`arm_target ${response.status}`);
  const target = await response.json();
  if (!target.valid) {
    els.armTarget.textContent = "未锁定";
    return;
  }

  els.armTarget.textContent = `(${Number(target.cx).toFixed(0)}, ${Number(target.cy).toFixed(0)}) ${Number(target.angle_deg).toFixed(0)}°`;
}

function handleIncomingMessage(message) {
  switch (message.type) {
    case "device_status":
      updateDeviceStatus(message.payload);
      break;
    case "battery_log":
      upsertTrace(message.payload);
      break;
    case "carbon_update":
      updateCarbon(message.payload);
      break;
    case "command_ack":
      handleCommandAck(message.payload);
      break;
    case "video_frame":
      showFallbackFrame(message.payload);
      break;
    case "webrtc_offer":
      handleWebRtcOffer(message.payload);
      break;
    case "webrtc_ice":
      handleRemoteIce(message.payload);
      break;
    default:
      setMessage(`收到未识别消息类型：${message.type}`);
  }
}

function updateDeviceStatus(payload = {}) {
  els.aiStatus.textContent = payload.aiStatus || "识别中";
  els.aiFps.textContent = Number(payload.fps || 0).toFixed(1);
  els.deviceMode.textContent = payload.mode || "自动处理";
}

function upsertTrace(payload = {}) {
  if (!payload.id) return;
  state.traces.set(payload.id, payload);
  setMessage(`已接收电池 ${payload.id} 的溯源记录。`);
}

function handleCommandAck(payload = {}) {
  els.commandState.textContent = payload.ok ? "硬件已确认" : "硬件拒绝/失败";
  setMessage(payload.message || "指令状态已更新。");
}

function requestVideoRefresh() {
  if (state.httpConnected) {
    els.mjpegStream.src = `${buildBrainUrl("/stream", { streamPort: true })}?t=${Date.now()}`;
    setMessage("已刷新固件 MJPEG 视频流。");
    return;
  }

  const sent = sendMessage({
    type: "request_video_refresh",
    requestId: crypto.randomUUID?.() || String(Date.now()),
    ts: Date.now(),
  });
  setMessage(sent ? "刷新画面请求已发送。" : "演示模式：等待硬件视频流接入。");
}

function sendEstop() {
  els.lastCommand.textContent = "ESTOP";
  els.commandState.textContent = "发送中";

  const sent = sendMessage({
    type: "control",
    command: "ESTOP",
    level: "critical",
    source: "web_dashboard",
    ts: Date.now(),
  });

  if (sent) {
    els.commandState.textContent = "已下发";
    setMessage("远程急停指令已通过 WebSocket 下发。");
  } else {
    els.commandState.textContent = "演示确认";
  }
}

// WebRTC 简易对接：
// 1. Brain/云端通过 WebSocket 发送 { type: "webrtc_offer", payload: { sdp } }
// 2. 网页创建 RTCPeerConnection，setRemoteDescription(offer)
// 3. 网页生成 answer，通过 WebSocket 回传 { type: "webrtc_answer", payload: { sdp } }
// 4. 双方继续交换 { type: "webrtc_ice", payload: candidate }
async function handleWebRtcOffer(payload = {}) {
  if (!payload.sdp) return;
  closePeer();

  state.peer = new RTCPeerConnection({
    iceServers: [{ urls: "stun:stun.l.google.com:19302" }],
  });

  state.peer.addEventListener("track", (event) => {
    const [stream] = event.streams;
    els.remoteVideo.srcObject = stream;
    els.videoStage.classList.add("has-video");
    els.videoStage.classList.remove("has-frame");
  });

  state.peer.addEventListener("icecandidate", (event) => {
    if (event.candidate) {
      sendMessage({ type: "webrtc_ice", payload: event.candidate });
    }
  });

  await state.peer.setRemoteDescription({ type: "offer", sdp: payload.sdp });
  const answer = await state.peer.createAnswer();
  await state.peer.setLocalDescription(answer);

  sendMessage({
    type: "webrtc_answer",
    payload: { sdp: answer.sdp },
    ts: Date.now(),
  });
  setMessage("WebRTC Answer 已返回，等待视频轨道。");
}

async function handleRemoteIce(payload = {}) {
  if (!state.peer || !payload.candidate) return;
  try {
    await state.peer.addIceCandidate(payload);
  } catch (error) {
    setMessage(`ICE 候选处理失败：${error.message}`);
  }
}

function closePeer() {
  if (state.peer) {
    state.peer.close();
    state.peer = null;
  }
  els.remoteVideo.srcObject = null;
  els.videoStage.classList.remove("has-video");
}

function showFallbackFrame(payload = {}) {
  if (!payload.jpegBase64) return;
  els.fallbackFrame.src = `data:image/jpeg;base64,${payload.jpegBase64}`;
  els.videoStage.classList.add("has-frame");
}

function traceBattery(id) {
  const record = state.traces.get(id);
  if (!record) {
    els.traceResult.innerHTML = `<p class="empty-state">未找到 ${escapeHtml(id)} 的本地记录。已向 Brain 请求云端查询。</p>`;
    sendMessage({ type: "trace_query", batteryId: id, ts: Date.now() });
    return;
  }

  renderTrace(record);
}

function renderTrace(record) {
  const abnormal = Boolean(record.abnormal);
  const confidence = Number(record.confidence || 0);
  els.traceResult.innerHTML = `
    <table class="trace-table">
      <tbody>
        <tr><th>唯一 ID</th><td>${escapeHtml(record.id)}</td></tr>
        <tr><th>电池型号</th><td>${escapeHtml(record.model || record.class || "未知")}</td></tr>
        <tr><th>识别时间</th><td>${escapeHtml(record.timestamps?.identifiedAt || "-")}</td></tr>
        <tr><th>抓取时间</th><td>${escapeHtml(record.timestamps?.pickedAt || "-")}</td></tr>
        <tr><th>切割时间</th><td>${escapeHtml(record.timestamps?.cutAt || "-")}</td></tr>
        <tr><th>归档时间</th><td>${escapeHtml(record.timestamps?.archivedAt || "-")}</td></tr>
        <tr><th>异常判定</th><td><span class="badge ${abnormal ? "warn" : "ok"}">${abnormal ? "异常复核" : "正常通过"}</span></td></tr>
        <tr><th>AI 置信度</th><td>${(confidence * 100).toFixed(1)}%</td></tr>
      </tbody>
    </table>
  `;
}

function updateCarbon(payload = {}) {
  state.carbon = {
    total: Number(payload.total ?? state.carbon.total),
    today: Number(payload.today ?? state.carbon.today),
    week: Number(payload.week ?? state.carbon.week),
    series: Array.isArray(payload.series) ? payload.series.map(Number) : state.carbon.series,
  };
  renderCarbon();
}

function animateNumber(element, nextValue) {
  const current = Number(element.textContent) || 0;
  const target = Math.round(nextValue);
  const steps = 18;
  let frame = 0;

  const tick = () => {
    frame += 1;
    const value = current + (target - current) * (frame / steps);
    element.textContent = Math.round(value).toLocaleString("zh-CN");
    if (frame < steps) requestAnimationFrame(tick);
  };

  requestAnimationFrame(tick);
}

function renderCarbon() {
  animateNumber(els.totalCo2, state.carbon.total);
  animateNumber(els.todayCo2, state.carbon.today);
  animateNumber(els.weekCo2, state.carbon.week);
  drawCarbonChart();
}

function drawCarbonChart() {
  const canvas = els.carbonChart;
  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const cssWidth = canvas.clientWidth;
  const cssHeight = canvas.clientHeight;
  canvas.width = Math.floor(cssWidth * dpr);
  canvas.height = Math.floor(cssHeight * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

  ctx.clearRect(0, 0, cssWidth, cssHeight);
  const padding = 34;
  const chartWidth = cssWidth - padding * 2;
  const chartHeight = cssHeight - padding * 2;
  const values = state.carbon.series;
  const max = Math.max(...values, 1);
  const barGap = 12;
  const barWidth = (chartWidth - barGap * (values.length - 1)) / values.length;

  ctx.strokeStyle = "#d9e2ee";
  ctx.lineWidth = 1;
  for (let i = 0; i <= 3; i += 1) {
    const y = padding + (chartHeight / 3) * i;
    ctx.beginPath();
    ctx.moveTo(padding, y);
    ctx.lineTo(cssWidth - padding, y);
    ctx.stroke();
  }

  values.forEach((value, index) => {
    const x = padding + index * (barWidth + barGap);
    const h = (value / max) * (chartHeight - 24);
    const y = padding + chartHeight - h;
    const gradient = ctx.createLinearGradient(0, y, 0, y + h);
    gradient.addColorStop(0, "#2dd4ff");
    gradient.addColorStop(1, "#22c55e");
    ctx.fillStyle = gradient;
    ctx.fillRect(x, y, barWidth, h);

    ctx.fillStyle = "#142033";
    ctx.font = "700 13px Microsoft YaHei, sans-serif";
    ctx.textAlign = "center";
    ctx.fillText(String(value), x + barWidth / 2, y - 8);

    ctx.fillStyle = "#607086";
    ctx.fillText(`D${index + 1}`, x + barWidth / 2, cssHeight - 12);
  });
}

function startDemoUpdates() {
  state.demoTimer = window.setInterval(() => {
    if (state.connected) return;
    const fps = 6.8 + Math.random() * 1.4;
    updateDeviceStatus({
      aiStatus: ["搜索电池", "识别 18650", "切割复检", "安全待机"][Math.floor(Math.random() * 4)],
      fps,
      mode: "演示模式",
    });
    updateCarbon({
      total: state.carbon.total + 18,
      today: state.carbon.today + 18,
      week: state.carbon.week + 18,
      series: [...state.carbon.series.slice(1), 90 + Math.round(Math.random() * 72)],
    });
  }, 5000);
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

els.connectBtn.addEventListener("click", connectWebSocket);
els.httpConnectBtn.addEventListener("click", connectHttpFirmware);
els.proxyBtn.addEventListener("click", () => {
  els.httpBaseUrl.value = "/brain";
  if (state.httpConnected) disconnectHttpFirmware();
  connectHttpFirmware();
});
els.refreshVideoBtn.addEventListener("click", requestVideoRefresh);
els.estopBtn.addEventListener("click", () => els.confirmDialog.showModal());
els.confirmEstopBtn.addEventListener("click", sendEstop);
els.traceForm.addEventListener("submit", (event) => {
  event.preventDefault();
  const id = els.batteryId.value.trim();
  if (!id) return;
  traceBattery(id);
});
window.addEventListener("resize", drawCarbonChart);

setConnectionStatus(false);
renderCarbon();
renderTrace(demoTrace);
startDemoUpdates();
