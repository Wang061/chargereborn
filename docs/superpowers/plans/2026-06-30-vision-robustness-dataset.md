# 18650 检测器鲁棒性提升（数据驱动重训 v4）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 ~200-250 张匹配部署条件（明亮室内光）的新数据 + 旧 208 暗图合并 + 亮度增强，重训单类 18650 检测器，把板上亮场真实置信度从 ~0.27 抬到稳定 ≥0.4-0.5，消除弱光闪烁与误识别。

**Architecture:** 纯数据驱动重训。沿用 `docs/ai/MODEL_PIPELINE.md` 已通的流水线（ESPDet-Pico 224 → int8+letterbox 量化 → vendored 进 `components/battery_detect/`）。本轮不改模型结构、不扩类、不动量化路径，只换更好的数据 + 拉高亮度增强。人在环：采集/标注是手工瓶颈（用户+队友），AI 负责脚手架固化 + 训练/量化/部署/验证回路。

**Tech Stack:** ESP-IDF 5.5.4 / ESP32-S3；esp-dl 3.3.5（registry）；ultralytics 8.3.112 + esp-detection（ESPDet-Pico）；conda env `chargereborn`（`D:\WJ\jixiebi\ai\envs\chargereborn`）；训练 GPU RTX 4070。

## Global Constraints

- IDF_VERSION = 5.5.4，IDF_TARGET = esp32s3（不得改）。构建走 `mcp__idf-bridge__*` 或 `scripts/idf.ps1`。
- 量化**只用 int8 + letterbox calib**（int16 板上更差，禁用）。
- 类名英文 `battery`（进 C++ 标识符；`18650` 数字开头非法）。显示名"18650"在 `components/ai/ai.cpp` `ai_class_name()`。
- 数据**纯英文路径**（中文名 opencv 读不了）。
- esp-detection 工作区 `D:\WJ\jixiebi\ai\esp-detection` 与 `D:\WJ\jixiebi\dataset` **在 git 仓外**（非 WORKplace 仓）→ 这两处无 commit，验证以脚本输出/grep 为准；只有 `WORKplace/` 内文件（采集简报、vendored 模型、docs）才 commit。
- FLASH 走 COM7（USB-Serial-JTAG），串口日志看 COM11（CH340），端口认友好名而非号。
- flash 必须当场确认（`安全.md`）。本模型纯识别不驱动执行器，但仍按规矩确认。

---

## 阶段总览

| 阶段 | 谁 | 任务 |
|---|---|---|
| 0 脚手架（本 session） | AI | T1 采集简报、T2 固化训练侧配置+增强、T3 部署侧重打清单 |
| 1 采集（离线手工） | 用户+队友 | T4 采 200-250 张亮图 + 标注 |
| 2 训练部署（数据齐后） | AI | T5 建集、T6 训练、T7 量化、T8 vendor+build、T9 上板 A/B、T10 沉淀 |

---

## 阶段 0：脚手架（本 session 可全做）

### Task 1: 队友采集简报

**Files:**
- Create: `WORKplace/docs/ai/DATASET_v4_COLLECTION.md`

**Interfaces:**
- Produces: 一份可直接发给队友的采集+标注 SOP，被 T4 执行。

- [ ] **Step 1: 写采集简报**，内容必须含（照抄进文档，不留 TODO）：
  - **目标**：明亮室内光下、干净台面，采 200-250 张，匹配 Demo 部署条件。
  - **多样性矩阵**（每格至少若干张）：
    - 数量：单颗 / 多颗(2-5) 混排
    - 角度：俯视 / 斜视 30-60°
    - 位置：画面四角 + 中心
    - 朝向：电池长轴 横/竖/斜 各方向（喂 `battery_angle`）
    - 光：明亮室内光为主（开灯/靠窗），少量略暗当余量
  - **负样本**（关键，治误识别）：空台面 + 干扰物（笔 / 电阻 / 硬币 / 其他圆柱物）混入，**干扰物不标框**。约占 10-15%。
  - **采集方式**：板子采集页连拍（引用 `docs/ai/DATASET_GUIDE.md`），类名框填英文 `battery`。
  - **标注**：`dataset\label_tool.py` 多框紧框标注；只标 18650，干扰物不标。
  - **分包**：`python dataset\make_dist.py <N>` → `dataset\dist\batch_k.zip`（自带工具+说明）发队友；回收 labels 合并进 `dataset\18650\labels`。
  - **质量门**：标完跑 `python dataset\qa_labels.py` 出 montage 自查（紧框、无漏标、无误标干扰物）。

- [ ] **Step 2: commit**

```bash
cd /d/WJ/jixiebi/WORKplace
git add docs/ai/DATASET_v4_COLLECTION.md
git commit -m "docs(ai): 18650 v4 鲁棒性重训采集简报(亮场+干扰物负样本)"
```

---

### Task 2: 固化训练侧配置 + 拉高亮度增强

**Files:**
- Modify: `D:\WJ\jixiebi\ai\esp-detection\train.py:17-29`（增强项）
- Verify (不改，仅核对): `D:\WJ\jixiebi\ai\esp-detection\nn\modules\esp_head.py`、`D:\WJ\jixiebi\ai\esp-detection\deploy\quantize.py`

**Interfaces:**
- Produces: 训练配置含显式 `hsv_v` 亮度增强；三处已知修复确认在位。被 T6 训练消费。

- [ ] **Step 1: 核对 esp_head reg_max 修复仍在**

Run:
```bash
grep -n "super().__init__" /d/WJ/jixiebi/ai/esp-detection/nn/modules/esp_head.py
```
Expected: 输出形如 `super().__init__(nc=nc, ch=ch)`，**不含** `reg_max=1`。若含 `reg_max=1` → 删掉它（否则 ultralytics 8.3.112 训练崩）。

- [ ] **Step 2: 核对 quantize.py CaliDataset 是 letterbox**

Run:
```bash
grep -n -i "letterbox\|Resize\|pad" /d/WJ/jixiebi/ai/esp-detection/deploy/quantize.py
```
Expected: `CaliDataset` 预处理走 letterbox（保宽高比 +114 填充），**不是**裸 `Resize`。若是裸 Resize → 改回 letterbox（**头号坑**，不一致→板上 n=0）。

- [ ] **Step 3: 在 train.py 加显式亮度/色彩增强**

把 `train_setting` dict（train.py:17-29）改为加入 hsv 三项（其余保持）：

```python
    train_setting = dict( # you can set your own train settings here.
        data=dataset,
        epochs=800, # battery from-scratch; patience(default 100) early-stops
        imgsz=imgsz,
        batch=16, # batch must be < N
        device="0",
        optimizer='auto',
        close_mosaic=30,
        mosaic=1.0,
        mixup=0.0,
        copy_paste=0.1,
        rect=False,
        hsv_h=0.015, # 色相微抖(默认)
        hsv_s=0.7,   # 饱和抖动(默认)
        hsv_v=0.55,  # ★亮度抖动: 默认0.4→0.55, 覆盖暗↔亮范围(光照鲁棒杠杆)
    )
```

- [ ] **Step 4: 验证改动语法 OK（不真训，dry import）**

Run:
```bash
cd /d/WJ/jixiebi/ai/esp-detection && /d/WJ/jixiebi/ai/envs/chargereborn/python.exe -c "import ast; ast.parse(open('train.py',encoding='utf-8').read()); print('train.py OK')"
```
Expected: `train.py OK`

> 注：`ai/esp-detection` 在仓外，无 commit。改动记录靠本 plan + T10 的 MODEL_PIPELINE v4。

---

### Task 3: 部署侧重打清单（预备，数据齐后 T8 用）

**Files:**
- Create: `WORKplace/docs/ai/DEPLOY_REPATCH_CHECKLIST.md`

**Interfaces:**
- Produces: espdet_run 重生成部署包后必做的 cpp/hpp 重打清单，被 T8 执行。

- [ ] **Step 1: 写重打清单**（照抄，含确切改法）：
  - `esp-dl/models/battery_detect/espdet_detect.cpp`：删 `dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN`，改 3 参构造 `ImagePreprocessor(m_model, {0,0,0}, {255,255,255})`（esp-dl 3.3.5 无该枚举）。
  - `esp-dl/models/battery_detect/espdet_detect.hpp`：`default_score_thr` 按 T9 板上实测定（M5 是 0.20；新模型置信度抬升后可能上调到 0.30-0.35）。
  - vendor：把量化好的 `espdet_pico_224_224_battery.espdl` + 重打的 cpp/hpp 覆盖进 `WORKplace/components/battery_detect/`。
  - **提醒**：espdet_run 每次重生成都会复发这两个坑，必须重打。

- [ ] **Step 2: commit**

```bash
cd /d/WJ/jixiebi/WORKplace
git add docs/ai/DEPLOY_REPATCH_CHECKLIST.md
git commit -m "docs(ai): 部署侧 espdet_detect cpp/hpp 重打清单(防 espdet_run 复发坑)"
```

---

## 阶段 1：采集（离线手工，AI 不执行，仅定验收）

### Task 4: 采集 + 标注 200-250 张亮图（用户 + 队友）

**Files:**
- Modify: `D:\WJ\jixiebi\dataset\18650\images\`（+新图）、`D:\WJ\jixiebi\dataset\18650\labels\`（+新标注）

**Interfaces:**
- Consumes: T1 采集简报。
- Produces: 合并后总 ~400-450 张（旧 208 + 新 ~200-250），images 与 labels 一一对应。被 T5 消费。

- [ ] **Step 1**: 按 T1 简报采集（多样性矩阵 + 干扰物负样本）。
- [ ] **Step 2**: 标注（label_tool.py），队友分包用 make_dist.py，回收合并。
- [ ] **Step 3: 验收——数量与配对**

Run:
```bash
echo "images:"; ls /d/WJ/jixiebi/dataset/18650/images | wc -l; echo "labels:"; ls /d/WJ/jixiebi/dataset/18650/labels | wc -l
```
Expected: images ≈ labels ≈ 400-450（空背景/纯干扰物负样本对应**空 .txt** 也要在 labels 里）。

- [ ] **Step 4: 验收——标注质量**

Run:
```bash
cd /d/WJ/jixiebi/dataset && /d/WJ/jixiebi/ai/envs/chargereborn/python.exe qa_labels.py
```
Expected: 生成 `_qa_montage.jpg`，人眼查：框紧、无漏标 18650、**无误标干扰物**。

---

## 阶段 2：训练→部署→验证（数据齐后，AI 驱动）

### Task 5: 重建数据集

**Files:**
- Run: `D:\WJ\jixiebi\dataset\build_dataset.py`
- Produces: `D:\WJ\jixiebi\dataset\18650_yolo\{images,labels}\{train,val}` + `battery.yaml` + `18650_calib`（全量 letterbox calib）

- [ ] **Step 1: 重建**

Run:
```bash
cd /d/WJ/jixiebi/dataset && /d/WJ/jixiebi/ai/envs/chargereborn/python.exe build_dataset.py
```

- [ ] **Step 2: 验证 split 与 calib 数量**

Run:
```bash
echo "train:"; ls /d/WJ/jixiebi/dataset/18650_yolo/images/train | wc -l
echo "val:";   ls /d/WJ/jixiebi/dataset/18650_yolo/images/val | wc -l
echo "calib:"; ls /d/WJ/jixiebi/dataset/18650_calib | wc -l
cat /d/WJ/jixiebi/dataset/18650_yolo/battery.yaml
```
Expected: train+val ≈ 总数；calib = 全 train 集（非"前50"）；yaml `nc: 1`、`names: [battery]`。

---

### Task 6: 重训（from scratch）

**Files:**
- Run: `D:\WJ\jixiebi\ai\esp-detection\espdet_run.py`
- Produces: `runs\detect\trainN\weights\best.pt` + `best.onnx` + float mAP50

- [ ] **Step 1: 训练（含量化一条龙，先看 float）**

Run（一条龙；若只想先看训练可单独 train，但 espdet_run 会接着量化）:
```bash
cd /d/WJ/jixiebi/ai/esp-detection && /d/WJ/jixiebi/ai/envs/chargereborn/python.exe espdet_run.py --class_name battery --pretrained_path None --dataset D:\WJ\jixiebi\dataset\18650_yolo\battery.yaml --size 224 224 --target esp32s3 --calib_data D:\WJ\jixiebi\dataset\18650_calib --espdl D:\WJ\jixiebi\dataset\espdet_pico_224_224_battery_v4.espdl --img <一张亮场val图.jpg>
```

- [ ] **Step 2: 验证 float mAP50**

看训练末尾输出 / `runs\detect\trainN\` 结果。
Expected: float **mAP50 ≥ 0.96**（不低于 M5 基线）；若明显掉，回看数据/标注质量再训。重点关注亮场样本被检出。

> 续跑（已有 best.pt 只重量化）：`--pretrained_path runs\detect\trainN\weights\best.pt`。

---

### Task 7: 量化校验（int8 + letterbox）

**Files:**
- Use: `D:\WJ\jixiebi\dataset\onnx_check.py`、`deploy/eval_quantized_model.py`
- Produces: 量化噪声报告 + onnx letterbox 基线对照

- [ ] **Step 1: onnx float 基线（letterbox）**

Run:
```bash
cd /d/WJ/jixiebi/dataset && /d/WJ/jixiebi/ai/envs/chargereborn/python.exe onnx_check.py
```
Expected: best.onnx + letterbox 在亮场测试图上 sigmoid score 峰值明显（float 没问题，再信量化）。

- [ ] **Step 2: 量化噪声**

看 espdet_run/quantize 输出的量化噪声。
Expected: 噪声正常（非 360%；薄校准才爆）。若爆 → 核对 calib=全量 + letterbox（头号坑）。

> 独立重量化（不重训）：`/d/WJ/jixiebi/ai/envs/chargereborn/python.exe requant.py`。

---

### Task 8: 重打部署 cpp/hpp + vendor + build

**Files:**
- Modify: `esp-dl/models/battery_detect/espdet_detect.cpp`、`.hpp`（按 T3 清单）
- Modify: `WORKplace/components/battery_detect/`（覆盖 espdl + cpp/hpp）

**Interfaces:**
- Consumes: T3 重打清单、T7 量化好的 espdl。

- [ ] **Step 1: 重打 cpp/hpp**（按 `docs/ai/DEPLOY_REPATCH_CHECKLIST.md`）

确认 `espdet_detect.cpp` 用 3 参 `ImagePreprocessor(m_model, {0,0,0}, {255,255,255})`，无 `DL_IMAGE_CAP_RGB565_BIG_ENDIAN`。

- [ ] **Step 2: vendor 覆盖进固件**

把新模型**重命名为固件期望的规范名** `espdet_pico_224_224_battery.espdl`（保持与 M5 同名 → 不动 Kconfig/sdkconfig 的 `*_ESPDET_PICO_224_224_BATTERY` 标志；v4 后缀仅留在 `dataset\` 工作区供 A/B），连同重打的 cpp/hpp 覆盖进 `WORKplace/components/battery_detect/models/s3/`（espdl）与组件源码目录（cpp/hpp）。覆盖前确认旧文件已被 git 跟踪（M5 已入库），diff 可见模型字节变化。

- [ ] **Step 3: build（绿）**

Run: `mcp__idf-bridge__build`（proxy 清空已在脚本内）
Expected: build 成功，无 `DL_IMAGE_CAP_RGB565_BIG_ENDIAN` 报错；rodata 内嵌新模型。

- [ ] **Step 4: commit（vendored 交付物入库）**

```bash
cd /d/WJ/jixiebi/WORKplace
git add components/battery_detect
git commit -m "feat(ai): 18650 检测器 v4 鲁棒性重训上板(亮场匹配数据+亮度增强)"
```

---

### Task 9: 上板 A/B 验证（实测为准）

**Files:**
- Flash + monitor（COM7 flash / COM11 日志 或 192.168.4.1）

**Interfaces:**
- Consumes: T8 build 产物。

- [ ] **Step 1: flash（当场确认）**

Run: `mcp__idf-bridge__flash`（或 `/esp-flash`；ESPPORT=COM7）
确认后执行。

- [ ] **Step 2: 明亮光下读真实串口 score**

Run: `mcp__idf-bridge__monitor_start` → `monitor_read`（或浏览器 192.168.4.1 看叠框 + score）。
Expected（验收线）:
  - 亮场单颗 score 稳定 **≥0.4-0.5**（vs M5 ~0.27）；
  - 多颗分框稳定、不漏；
  - 台面放干扰物（笔/硬币）**不误框**；
  - 框不闪烁。

- [ ] **Step 3: A/B 对比量化提升**

同一批亮场+干扰物测试图，跑新 v4 vs 旧 M5（可用 `dataset\predict_test.py` 在 PC 端跑 onnx，或板上分别 flash 读 score）。
Expected: 记录"亮场 score 提升 X、干扰物误检率降 Y" → 报告/答辩硬数据。

- [ ] **Step 4: 若不达线** → 系统化排查：回看数据多样性/标注 → 必要时调 `hsv_v`/补采 → 重训。**不把"看起来行"当验证过**。

---

### Task 10: 沉淀

**Files:**
- Modify: `WORKplace/docs/ai/MODEL_PIPELINE.md`（→ v4 段）
- Modify: `WORKplace/docs/ai/CRASH_SIGNATURES.md`（若遇新坑，经 `/learn`）
- Modify: 记忆 `brain-vision-progress`（更新进度）

- [ ] **Step 1**: MODEL_PIPELINE 增 v4 段：数据 400-450、hsv_v=0.55、新 score、A/B 结果。
- [ ] **Step 2**: 若有新坑，`/learn` 写 CRASH_SIGNATURES。
- [ ] **Step 3**: 更新记忆 `brain-vision-progress`（v4 上板、新置信度、下一步）。
- [ ] **Step 4: commit**

```bash
cd /d/WJ/jixiebi/WORKplace
git add docs/ai/MODEL_PIPELINE.md docs/ai/CRASH_SIGNATURES.md
git commit -m "docs(ai): 18650 v4 鲁棒性重训复现记录 + 验收数据"
```

---

## 验收（整轮 Done 的定义）

- [ ] 总数据 ~400-450（旧暗 + 新亮 + 干扰物负样本），qa 通过。
- [ ] float mAP50 ≥ 0.96；量化噪声正常。
- [ ] build 绿、vendored 自包含（clone 即 build）。
- [ ] 上板亮场单颗 score 稳定 ≥0.4-0.5、多颗稳、干扰物不误框、不闪。
- [ ] A/B 提升数据落进报告；MODEL_PIPELINE v4 + 记忆更新。
