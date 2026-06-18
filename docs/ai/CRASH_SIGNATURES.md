# CRASH_SIGNATURES.md — 崩溃签名库（/learn 沉淀）

每修好一个 bug，用 `/learn` 追加一条：**症状 → 根因 → 修法 → 预防**。
让高频故障变成可检索资产；下次 monitor/triage 先查这里。

格式：

```
### <短标题>  (日期)
- 症状: <panic 关键字 / 现象 / 回溯特征>
- 根因: <真正原因>
- 修法: <最小修改 + 文件:行>
- 预防: <规则/检查/skill，避免再犯>
- 标签: #wdt #stack #heap #flash #i2c #freertos ...
```

---

<!-- 示例（删除或保留作模板）
### Task watchdog on app_main blocking  (2026-01-01)
- 症状: "Task watchdog got triggered. ... CPU 0: main"
- 根因: app_main 里忙等未让出，触发 TWDT
- 修法: 用 vTaskDelay/事件驱动替换忙等（main/app_main.c:NN）
- 预防: 见 rules/coding-standard.md「避免忙等」；freertos-task-design skill
- 标签: #wdt #freertos
-->

（暂无真实条目——首个 bug 修复后由 /learn 写入。）

---

### /detect 轮询卡死 / httpd 任务栈溢出  (2026-06-18)
- 症状: 浏览器点"识别开始"(每 180ms 轮询 `/detect`)即卡死/视频冻结;`detect_task` 串口仍正常打印(板不一定复位)。
- 根因: `detect_get` handler 栈占用超 httpd 默认 4096 栈。`ai_box_t` 加 `angle_deg/anisotropy` 后 `ai_result_t r`≈336B + `char buf[1024→1536]` ≈ 1872B 局部 + httpd 框架开销 → 撑爆 4096。
- 修法: `net_http_start()` 加 `config.stack_size = 8192;`(components/net/http_srv.c)。与 81 口 stream server 一致。
- 预防: 在 httpd handler 放大局部缓冲/结构体前核对 `HTTPD_DEFAULT_CONFIG().stack_size`(=4096);大缓冲用 static/堆;改 `ai_result_t`/`ai_box_t` 体积时注意所有栈上拷贝点。
- 标签: #stack #httpd #freertos
