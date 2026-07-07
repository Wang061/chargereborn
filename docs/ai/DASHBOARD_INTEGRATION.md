# DASHBOARD_INTEGRATION.md — chargereborn-dashboard 固件对接计划书

> 面向实现 `components/net/net_ws.c`（新文件，你来写）的队友。固件侧的埋点已经就位
> （见 `components/armctrl/include/armctrl.h` 的 `armctrl_set_event_cb`/`armctrl_get_stats`/
> `armctrl_estop`），本文档告诉你怎么把这些接到 `chargereborn-dashboard/PROTOCOL.md`
> 定义的六类 WebSocket 消息上。

## 1. 固件侧要加什么（新文件，不改 armctrl/armlink/ai 内部）

1. `menuconfig` 开 `CONFIG_HTTPD_WS_SUPPORT=y`（ESP-IDF `esp_http_server` 组件的 WebSocket
   支持，5.5.4 自带，不需要额外组件）。
2. 新建 `components/net/net_ws.c` + `net_ws.h`，在**现有** `net_http_start()` 的同一个
   `httpd_handle_t`（80 口）上注册一个 `/ws` 的 `httpd_uri_t`（`.is_websocket = true`）。
   **不要另起一个 httpd 实例**——SoftAP 单核资源紧张，复用现有 server 更省内存/更简单。
3. `chargereborn-dashboard/app.js` 的默认地址（`ws://192.168.4.1:8080/ws`）改成
   `ws://192.168.4.1/ws`（80 口，免第二个端口）。这是 `app.js` 里的一行常量改动。

## 2. PROTOCOL.md 六类消息 ↔ 固件数据源映射

| PROTOCOL.md 消息 | 固件数据源 | 实现方式 |
|---|---|---|
| `device_status` | `ai_get_last()`(fps 用 `1000/infer_ms` 估算) + `armctrl_is_running()` | 起一个 1Hz FreeRTOS 定时任务，组 JSON 广播给所有连接的 ws 客户端 |
| `battery_log` | `armctrl_set_event_cb()` 注册的回调 | 回调里把 `armctrl_cycle_log_t` 的 4 个时间戳（`t_identified_us`/`t_picked_us`/`t_cut_us`/`t_placed_us`，单位 us，需要你自己转成 `"YYYY-MM-DD HH:MM:SS"` 或前端能读的格式）与 `ok` 字段组成 `battery_log` JSON。**已知缺口**：当前不追踪具体电池类别（`model`/`confidence` 字段）——`target_track` 只输出几何量，不带类别；`id` 字段可以先用 `seq_id` 顶替（如 `"BAT-" + seq_id`），如果确实需要类别/置信度，需要先扩展 `armlink`/`target_track` 让最佳关联帧的 `cls`/`score` 也透传出来，这是本轮范围外的工作，量不大但需要改 `track_output_t` 加字段 + `armlink.c` 透传，评估后再做 |
| `carbon_update` | `armctrl_get_stats()` 的 `total`/`session` | 按你们定的"每颗电池节约多少克 CO2"系数相乘；`today`/`week`/`series` 若没有按天分桶的持久化，可以先用 `session`（本次上电内计数）近似顶替，说明是近似值 |
| `control`(`ESTOP`) | `armctrl_estop()` | 收到 ws 消息后直接调用，不要自己重新发 `$DST:0!`——`armctrl_estop()` 已经处理好了发送+锁存+停循环。回 `command_ack` |
| `webrtc_offer`/`webrtc_answer`/`webrtc_ice` | 无固件数据源 | 若时间来不及打通完整 WebRTC 信令，直接跳过，用下一行的 `video_frame` 回退方案 |
| `video_frame` | `camera_capture()` 拿到的 JPEG buffer | 建议 **≤2fps**、单客户端限流：JPEG 直接 base64（膨胀 1.33x），`httpd_ws_send_frame_async` 异步发送避免阻塞 detect_task。QVGA 分辨率下预估带宽 100-200Kbps，SoftAP 单客户端可行；多客户端/高帧率会挤占检测循环的相机吞吐，需要实测 |
| `trace_query` | 无——需要你自己加一个环形缓冲 | 建议在 `net_ws.c` 里维护一个 RAM 环形数组（≥16 条 `armctrl_cycle_log_t` 副本），`armctrl_set_event_cb` 的回调里 push 进去；收到 `trace_query` 按 `id`/`seq_id` 线性查找 |

> **关于 `ok` 字段的语义（重要，Task 10 review 补充）**：`armctrl_cycle_log_t.ok == true` 的准确语义是
> "抓取+切割成功，且完成了放回尝试"——`place_back` 失败**不会**把 `ok` 翻成 `false`（与固件既有
> "放回失败非致命"流程一致，此时 `t_placed_us` 仍会记录尝试完成时刻）。前端不要把 `ok=true` 渲染成
> "已成功放回"，按"已完成处理（切割成功）"表述更准确。

## 3. 已知的表述纠正

- `chargereborn-dashboard/DEFENSE_GUIDE.md` 第 27 行答辩话术提到"Brain 再通过 ESP-NOW 通知机械臂控制端停机"——**本项目没有 ESP-NOW**，Brain→KM1 是直接 UART（GPIO1→KM1 RX2/GPIO41）。答辩话术需要改成"Brain 经 UART 直接向机械臂控制板发送急停指令"，避免被现场提问戳穿。

## 4. 验证步骤（分阶段，不要一把梭）

1. **PC 端假连接**：用 `wscat`（或任意 WS 客户端）连 `ws://<板子IP>/ws`，逐条手测：连接后应收到周期性 `device_status`；发 `{"type":"control","command":"ESTOP",...}` 应看到板子日志出现 `armctrl_estop` 的 `[estop]` 日志且收到 `command_ack`。
2. **真实 dashboard 联调**：`chargereborn-dashboard/index.html` 直接打开，连板子，过一遍 `DEFENSE_GUIDE.md` 的五步答辩话术，逐项核对界面显示与固件实际状态一致。
3. **并发稳定性**：dashboard 保持连接的同时跑一次完整抓取循环（`/arm_run?on=1`），观察 `/status` 的 `heap_free` 曲线 30 分钟内应保持平稳（无持续下降 = 无泄漏）；若 `video_frame` 打开，同时观察检测帧率（`ai_get_last().infer_ms`）没有因 WS 发送阻塞而显著变慢。
4. 全部通过后，把 `chargereborn-dashboard/` 一并纳入固件仓库的构建产物清单（或按你们的部署方式说明放哪里），更新 `README.md` 提一句"网页监控终端見 `chargereborn-dashboard/`"。

## 5. 不要碰的边界

- **不要改 `components/armctrl`/`components/armlink`/`components/ai` 内部实现**——所有需要的钩子已经在 Task 10 就位；如果发现真的需要新字段（如上面提到的电池类别透传），单独找我或在 PR 里说明，不要绕开钩子直接 include 内部头文件掏私有状态。
- **`armctrl_set_event_cb` 只在系统启动时注册一次**：请在系统启动时（首个抓取循环开始前）注册一次，运行中不要反复注册/注销——回调指针与 `arg` 是两次独立的非原子写入，循环运行中途更换存在瞬时空窗（先过空指针检查、后调用时已被置空）。一次注册、终身持有即可。
- **`armcal` 的 NVS 命名空间和 `armctrl` 的 `stats` 命名空间都不要碰**——WebSocket 层需要新的持久化（比如按天分桶的碳减排曲线）请开一个新的 NVS 命名空间，不要复用/覆盖已有的。
- 急停路径**只能**通过 `armctrl_estop()`，不要在 `net_ws.c` 里自己拼 UART 帧发送——那样会绕开 `s_estop` 锁存机制，导致"网页急停了但固件状态没同步"的不一致。
