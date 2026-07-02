# 设计：18650 检测器鲁棒性提升（数据驱动重训 v4）

> **生成**：2026-06-30
> **作者**：ChargeReborn Brain 视觉主轴
> **前置**：`docs/ai/MODEL_PIPELINE.md`（M5 训练→量化→部署流水线）、记忆 `brain-vision-progress`、`ref-espdet-train-quant-gotchas`
> **硬约束**：投稿截止 **2026-07-09 18:00**（今日 06-30，剩 ~9 天），还要并行做视频+报告。
> **本文作用**：定义"用更好的数据把单类 18650 检测器练鲁棒"这一轮的范围、方法、验证与切分。这是 What/Why；How 由配套 implementation plan 承接。

---

## 0. 一句话

在**明亮室内光**下采 ~200-250 张匹配部署条件（固定补光+干净台面）的新数据、合并旧 208 张暗图、开亮度/HSV 增强，按已文档化流水线重训→int8 量化→上板，把亮场真实置信度从 ~0.27 抬到稳定 ≥0.4-0.5，消除弱光闪烁与误识别。

## 1. 背景与根因

- 现状（master，已上板）：单类 `battery`(18650) ESPDet-Pico 224，Float mAP50 **0.96**，板上 int8+letterbox 干净单颗 ~0.27、~220ms（3.5Hz）。
- **痛点**：模型对环境敏感——好光置信度可达 0.6，**弱光/杂背景只 ~0.2 徘徊**（`score_thr=0.20` → 时有时无闪烁），偶发误识别其他物体。
- **根因**：训练集 208 张几乎全是暗图、背景单一，泛化弱；且**训练分布(暗) ≠ 部署分布(补光亮台面)**。
- 决策：部署条件已定为**固定补光 + 干净台面**（可控）。因此最高杠杆 = **让训练分布对齐部署分布**，质量/匹配 > 数量。

## 2. 范围（本轮明确边界）

**做**：单类 18650 检测器的鲁棒性/真实置信度提升，纯数据驱动 + 增强。

**不做**（写入"后手"，本轮不碰，防 9 天翻车）：
- 扩 5 类电池型号（蓝图承诺，但高风险，单独立项）。
- 鼓包/漏液异常二分类。
- `--size 160` 提速。
- 系统侧多帧投票/迟滞（可作为之后余量的兜底，不进本轮）。

## 3. 数据策略（项目瓶颈，手工，用户+队友）

- 新采 **~200-250 张明亮室内光**下数据（明亮光暂代最终补光灯，接受轻微差异）。刻意覆盖：
  - **数量**：单颗 + 多颗(2-5) 混排。
  - **几何**：多角度（俯视/斜视）、多位置（画面四角 + 中心）、**多朝向**（电池长轴各方向 → 同时强化 `battery_angle` 结构张量估计）。
  - **负样本**：空背景 + **干扰物**（笔 / 电阻 / 硬币 / 其他圆柱物体）→ 直接打击"偶发误识别其他物体"。
- 旧 208 张暗图**保留合并** → 总 ~400-450 张（暗图反而撑宽光照范围，作鲁棒余量）。
- 工具链（不变）：板子采集页连拍（`docs/ai/DATASET_GUIDE.md`）→ `dataset\label_tool.py` 多框标注 → `dataset\make_dist.py N` 分包给队友 → 回收 labels 合并进主 labels。
- **质量铁律**：紧框；类名英文 `battery`（数字开头非法）；**纯英文路径**（中文名 opencv 读不了）。

## 4. 训练侧（沿用 MODEL_PIPELINE §2-3，本 session 固化）

- `dataset\build_dataset.py` 重建 `18650_yolo\{images,labels}\{train,val}` + `battery.yaml` + **全量 letterbox calib**（薄校准是量化崩根因之一）。
- 重训 from scratch：ESPDet-Pico 224×224、`batch=16`、`epochs~800`、`--class_name battery --target esp32s3`。
- **增强**：确认/微调 ultralytics 的 `hsv_v`(亮度)/`hsv_s`(饱和)/`hsv_h`(色相) 增强开启 → 在亮场前后留出范围（免费的光照鲁棒乘子）。避免过激致掉点。
- 重训前**核对** §3 三处训练侧修复仍在：
  - `nn/modules/esp_head.py`：`super().__init__(nc=nc, ch=ch)`（去 `reg_max=1`）。
  - `train.py`：`batch=16`（< 数据量）、`epochs=800`。
  - `deploy/quantize.py` `CaliDataset`：letterbox（保宽高比 +114 填充）。

## 5. 量化与部署

- **int8 + letterbox calib**（int16 实测板上更差且慢 2.5x，**绝不用**）。
- espdet_run 生成部署包后**必须重打**（每次重生成都复发）：
  - `esp-dl/models/battery_detect/espdet_detect.cpp`：去掉 `dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN`，用 3 参 `ImagePreprocessor(m_model,{0,0,0},{255,255,255})`。
  - `espdet_detect.hpp`：`default_score_thr`（本模型 int8 绝对置信度偏低，按板上实测定）。
- vendored 进 `components/battery_detect/`（覆盖 `*.espdl` + 重打的 cpp/hpp）；esp-dl 走 registry `==3.3.5`。
- `mcp__idf-bridge__build` 绿（proxy 清空 + `scripts/idf.ps1`）。

## 6. 已知坑清单（预先挂上，照着躲）

| 坑 | 症状 | 躲法 |
|---|---|---|
| letterbox calib 不一致（头号） | 板上 n=0，float 完好 | CaliDataset 用 letterbox，全量 calib |
| batch ≥ 数据量 | mAP 0.01（没训） | batch=16（现数据多，安全） |
| ImagePreprocessor cap 枚举 | 编译失败 | 重打 espdet_detect.cpp（3 参构造） |
| int16 | 置信度更低 + 慢 | 只用 int8 |
| 中文路径 | opencv 读不了 | 纯英文路径 |

## 7. 验证（实测为准，不靠"看起来行"）

- **Float**：val `mAP50`（期望 ≥0.96，重点看**亮场子集**表现）；`onnx + letterbox` 基线核对 sigmoid 峰值。
- **上板**（`$env:ESPPORT='COM7'` flash → COM11 串口日志 / 浏览器 192.168.4.1）：明亮光下读**真实串口 score 数字**。
  - 验收线：亮场单颗稳定 **≥0.4-0.5**（vs 现 0.27）；多颗分框稳定；**干扰物不误框**；闪烁消失。
- **A/B 对比**：同一批测试图跑新 vs 旧模型，量化提升幅度 → 报告/答辩硬数据。
- 沉淀：更新 `MODEL_PIPELINE.md` → v4；`/learn` 记录新坑（如有）；更新记忆 `brain-vision-progress`。

## 8. 本 session 切分（角色与交付）

| 角色 | 任务 | 时机 |
|---|---|---|
| **AI（本 session）** | 钉死可执行 implementation plan；固化训练增强/量化脚本（防 espdet_run 复发坑）；产出**队友采集清单**（拍什么/多少/怎么标/质量门） | 现在 |
| **用户 + 队友（离线手工）** | 采 200-250 张明亮光数据 + 标注（make_dist 分包） | 数据采集期 |
| **AI（数据齐后）** | 驱动 build_dataset→训练→量化→部署→上板 A/B 验证；沉淀文档 | 数据回收后 |

## 9. 风险与应对

| 风险 | 应对 |
|---|---|
| 9 天内采集+标注拖慢，挤压视频/报告 | 规模可降级到方案 B（~50-80 张 + 激进增强 + 旧暗图合成提亮）；队友并行标注 |
| 增强过激致掉点 | 先用默认/温和增强出基线，A/B 验证后再调 |
| 量化坑复发 | §6 清单逐条核对；espdet_run 重生成后必重打 cpp/hpp |
| 明亮室内光与最终补光灯有差异 | 接受轻微差异；最终补光灯到位后可快速 requant（`dataset\requant.py`，不重训） |
| 上板提升不达验收线 | 回看数据多样性/标注质量；必要时叠系统侧多帧投票兜底（后手） |

## 10. 后手（本轮不做，记录备查）

- 扩 5 类型号（蓝图 ≥92% 叙事）；鼓包/漏液异常二分类；`--size 160` 提速（~2x）；系统侧多帧投票/迟滞 + 固定补光灯硬件兜底；QAT。
