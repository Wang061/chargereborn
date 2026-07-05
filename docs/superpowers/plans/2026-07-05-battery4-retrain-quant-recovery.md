# 4类电池检测器数据刷新 + 量化置信度塌陷修复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 合并旧 394 张 + 新 500 张数据重训 4 类 ESPDet-Pico（224×224 不变），用 esp-dl 官方 `espdl-quantize` 结构化流程系统性解决 int8 量化置信度较 float 塌陷 5-10x 的问题，上板 A/B 验收后交由用户拍板是否替换默认模型。

**Architecture:** 五段顺序流水线，每段的产出是下一段的输入：数据合并（外部 Python 脚本）→ 训练（ultralytics/GPU）→ 量化调参（esp-ppq + esp-dl 官方 skill）→ 固件集成（ESP-IDF 组件替换）→ 上板验证（build/flash/monitor）。前三段在仓外工作区（`D:\WJ\jixiebi\ai\esp-detection`、`D:\WJ\jixiebi\dataset`，非 git），后两段在本仓库（`D:\WJ\jixiebi\WORKplace`，git）。

**Tech Stack:** Python 3.11 (conda env `chargereborn`)、PyTorch 2.2.0+cu121、ultralytics 8.3.112、esp-ppq（`esp_ppq`）、ESP-IDF 5.5.4、esp32s3。

## Global Constraints

- `IDF_VERSION = 5.5.4`、`IDF_TARGET = esp32s3`——不升级、不改 target。
- 训练/量化环境固定 conda `chargereborn`（`D:\WJ\jixiebi\ai\envs\chargereborn\python.exe`），版本钉死：`torch==2.2.0+cu121`、`numpy==1.24.4`、`pandas==2.2.3`、`ultralytics==8.3.112`——不升级任何一个。
- 类别映射固定 `{0: "21700", 1: "18650", 2: "9V", 3: "AA"}`——改动会破坏固件侧 `ai_class_name()` 映射，任何环节都不得重排序。
- 分辨率固定 224×224——本轮不碰（已与用户确认）。
- `FLASH_BAUD=921600`（失败回退 `460800`）、`MONITOR_BAUD=115200`。
- **flash 操作必须每次当场向用户确认**——项目铁律，不得自动执行。
- Windows 环境：`workers>0` 的 PyTorch DataLoader 脚本必须有 `if __name__=="__main__":` 守卫（spawn 语义，非 Linux fork）；chargereborn 环境本机曾在 `workers=8` 时因 pagefile 不足报 `WinError 1455`。
- 校准/训练预处理必须 letterbox（保宽高比+114填充），与板上 `ImagePreprocessor` 一致——不一致会导致量化后置信度塌成噪声（`ref-espdet-train-quant-gotchas` 头号坑）。

**前置阅读**（执行者开工前必看）：`docs/superpowers/specs/2026-07-05-battery4-retrain-quant-recovery-design.md`（本计划对应的设计文档，包含完整背景/根因/验收标准）。

---

### Task 1: 提交现有 checkpoint（与本轮改动解耦）

**Files:**
- Modify（无需改动内容，仅提交现状）: `components/ai/Kconfig`、`components/ai/ai.cpp`、`components/battery_detect4/espdet4_detect.hpp`

**Interfaces:**
- Consumes: 无（独立操作）
- Produces: 一个干净的 git 提交点，后续所有改动都 diff 自这个提交，不与本轮改动混在一起

- [ ] **Step 1: 核对当前 diff 与预期一致**

```bash
cd /d/WJ/jixiebi/WORKplace
git diff -- components/ai/Kconfig components/ai/ai.cpp components/battery_detect4/espdet4_detect.hpp
```

预期：三处改动——Kconfig 默认值切到 `AI_DETECTOR_BATTERY_DETECT4`、`ai.cpp` 新增边缘框过滤（`margin=5` 像素）、`espdet4_detect.hpp` 的 `default_score_thr` 从 `0.20` 改成 `0.08`。如果 diff 与此不符（例如中途又有其他改动混入），停下向用户确认，不要盲目提交。

- [ ] **Step 2: 提交**

```bash
cd /d/WJ/jixiebi/WORKplace
git add components/ai/Kconfig components/ai/ai.cpp components/battery_detect4/espdet4_detect.hpp
git commit -m "fix(ai): battery_detect4默认+score_thr=0.08+边缘框过滤(0705上板验证过)

int8量化致置信度偏低(~0.05-0.18),边缘假框(如9V[621,0,639,58])需过滤。
board-validated 2026-07-05: AA检出率60-70%,边缘假框消除,220-221ms/帧。
独立checkpoint,与本轮(数据刷新+量化重做)改动解耦。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 3: 验证提交落地**

```bash
git log --oneline -1
git status --short
```

预期：`git status --short` 对这三个文件不再显示 `M`；`git log` 顶部是刚才的提交。

---

### Task 2: 扩展 build_dataset4.py —— 多源合并 + QA 可视化 montage

**Files:**
- Modify: `D:\WJ\jixiebi\dataset\build_dataset4.py`
- Create: `D:\WJ\jixiebi\dataset\test_build_dataset4.py`

**Interfaces:**
- Consumes: 无（纯脚本改造）
- Produces: `collect()` 支持多图像源+多标签源列表；`render_qa_montage(pairs, names, out_path, per_class, thumb)` 函数；两者均被 `main()` 调用

- [ ] **Step 1: 写测试（先验证现状行为，再验证新行为）**

创建 `D:\WJ\jixiebi\dataset\test_build_dataset4.py`（不依赖 pytest，纯 assert，因为 chargereborn 环境当前没装 pytest 且不想为测试新增依赖）：

```python
"""Plain-assert regression tests for build_dataset4.py's multi-source collect().
Run: D:\WJ\jixiebi\ai\envs\chargereborn\python.exe test_build_dataset4.py
No pytest dependency - chargereborn env doesn't have it and we don't want to
add a new package to a version-pinned training env just for this.
"""
import os, sys, tempfile, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_dataset4 as bd


def _touch(path, content=""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)


def test_collect_merges_multiple_roots():
    tmp = tempfile.mkdtemp(prefix="bd4_test_")
    try:
        img_a = os.path.join(tmp, "imgs_a")
        img_b = os.path.join(tmp, "imgs_b")
        lbl_a = os.path.join(tmp, "lbls_a")
        lbl_b = os.path.join(tmp, "lbls_b")
        _touch(os.path.join(img_a, "IMG_0001.jpg"))
        _touch(os.path.join(img_b, "IMG_0002.jpg"))
        _touch(os.path.join(lbl_a, "IMG_0001.txt"), "1 0.5 0.5 0.2 0.2\n")
        _touch(os.path.join(lbl_b, "IMG_0002.txt"), "3 0.4 0.4 0.1 0.1\n")

        orig_img_roots, orig_lbl_roots = bd.SRC_IMG_ROOTS, bd.SRC_LBL_ROOTS
        bd.SRC_IMG_ROOTS = [img_a, img_b]
        bd.SRC_LBL_ROOTS = [lbl_a, lbl_b]
        try:
            imgs, lbls, dup = bd.collect()
        finally:
            bd.SRC_IMG_ROOTS, bd.SRC_LBL_ROOTS = orig_img_roots, orig_lbl_roots

        assert set(imgs.keys()) == {"IMG_0001", "IMG_0002"}, imgs
        assert set(lbls.keys()) == {"IMG_0001", "IMG_0002"}, lbls
        assert dup == [], dup
        print("PASS: test_collect_merges_multiple_roots")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def test_collect_detects_cross_root_basename_collision():
    tmp = tempfile.mkdtemp(prefix="bd4_test_")
    try:
        img_a = os.path.join(tmp, "imgs_a")
        img_b = os.path.join(tmp, "imgs_b")
        _touch(os.path.join(img_a, "IMG_0001.jpg"))
        _touch(os.path.join(img_b, "IMG_0001.jpg"))  # same basename, different root

        orig_img_roots, orig_lbl_roots = bd.SRC_IMG_ROOTS, bd.SRC_LBL_ROOTS
        bd.SRC_IMG_ROOTS = [img_a, img_b]
        bd.SRC_LBL_ROOTS = []
        try:
            imgs, lbls, dup = bd.collect()
        finally:
            bd.SRC_IMG_ROOTS, bd.SRC_LBL_ROOTS = orig_img_roots, orig_lbl_roots

        assert len(dup) == 1, dup
        assert dup[0][0] == "IMG_0001", dup
        print("PASS: test_collect_detects_cross_root_basename_collision")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    test_collect_merges_multiple_roots()
    test_collect_detects_cross_root_basename_collision()
    print("ALL TESTS PASSED")
```

- [ ] **Step 2: 跑测试，确认因为 `SRC_IMG_ROOTS`/`SRC_LBL_ROOTS` 还不存在而失败**

```bash
cd /d/WJ/jixiebi/dataset
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" test_build_dataset4.py
```

预期：`AttributeError: module 'build_dataset4' has no attribute 'SRC_IMG_ROOTS'`（现状脚本用的是单数 `SRC_IMG_ROOT`/`SRC_LBL_DIR`）。

- [ ] **Step 3: 把 `build_dataset4.py` 的单源改成多源列表 + 加 montage 函数**

把文件顶部常量和 `collect()` 替换成：

```python
SRC_IMG_ROOTS = [
    r"D:\WJ\jixiebi\ai\vm.jpg\vm.jpg",
    r"D:\WJ\jixiebi\ai\new_source\打光前",
    r"D:\WJ\jixiebi\ai\new_source\打光后",
]
SRC_LBL_ROOTS = [
    r"D:\WJ\jixiebi\ai\vm.jpg\vm.text",
    r"D:\WJ\jixiebi\ai\new_source\labels_my-project-name_2026-07-05-07-15-14",
    r"D:\WJ\jixiebi\ai\new_source\labels_my-project-name_2026-07-05-08-35-51",
]
ROOT  = r"D:\WJ\jixiebi\dataset\battery4_yolo"
CALIB = r"D:\WJ\jixiebi\dataset\battery4_calib"
MONTAGE = r"D:\WJ\jixiebi\dataset\_qa_montage_battery4_merged.jpg"
NAMES = {0: "21700", 1: "18650", 2: "9V", 3: "AA"}  # = teammate data.yaml order; ai_class_name() must match
VAL_FRAC = 0.15
MONTAGE_PER_CLASS = 6
random.seed(42)

def collect():
    imgs = {}
    dup = []
    for root in SRC_IMG_ROOTS:
        for ext in ("*.jpg", "*.JPG", "*.jpeg", "*.png"):
            for p in glob.glob(os.path.join(root, "**", ext), recursive=True):
                b = os.path.splitext(os.path.basename(p))[0]
                if b in imgs and imgs[b] != p:
                    dup.append((b, imgs[b], p))
                imgs[b] = p
    lbls = {}
    for root in SRC_LBL_ROOTS:
        for p in glob.glob(os.path.join(root, "*.txt")):
            b = os.path.splitext(os.path.basename(p))[0]
            if b in lbls and lbls[b] != p:
                dup.append((b, lbls[b], p))
            lbls[b] = p
    return imgs, lbls, dup
```

`parse_label()` 和 `main()` 的其余逻辑（QA 打印、分层切分、calib 构建、yaml 输出）不变——它们已经是通用的，不关心图像/标签来自几个源目录。**唯一要额外核对**：`main()` 里 `print("images found : %d  (recursive under %s)" % (len(imgs), SRC_IMG_ROOT))` 这行引用了旧的单数变量名 `SRC_IMG_ROOT`，要改成 `", ".join(SRC_IMG_ROOTS)` 或直接去掉具体路径只打印数量，否则会 `NameError`。

- [ ] **Step 4: 在 `main()` 里加 montage 渲染（`pairs` 算出来之后、hard-error 检查之前）**

在 `main()` 函数里找到这一段：
```python
    pairs = []          # (img_path, lbl_path, class_set)
    cls_inst = collections.Counter()
```
往上翻到这段代码执行完（`pairs` 列表填好）之后、`print("label format errors: %d" % len(all_errs))` 之前，插入：
```python
    montage_path = render_qa_montage(pairs, NAMES, MONTAGE, per_class=MONTAGE_PER_CLASS)
    print("QA montage: %s (inspect visually before trusting new_source class labels)" % montage_path)
```

在文件里追加 `render_qa_montage` 函数（放在 `parse_label` 之后、`main` 之前）：

```python
def render_qa_montage(pairs, names, out_path, per_class=6, thumb=96):
    """Grid of sample crops per class, split OLD (vm.jpg) vs NEW (new_source) side
    by side, so a human/agent can visually confirm new_source's class-index
    labeling matches the assumed convention before training on it blind."""
    from PIL import Image, ImageDraw

    buckets = collections.defaultdict(list)  # (cls, source) -> [(img_path, box)]
    for ip, lp, _cs in pairs:
        boxes, errs = parse_label(lp)
        if errs:
            continue
        source = "OLD" if "vm.jpg" in ip else "NEW"
        for c, cx, cy, w, h in boxes:
            buckets[(c, source)].append((ip, (cx, cy, w, h)))
    for key in buckets:
        random.shuffle(buckets[key])

    cols = per_class * 2  # cols [0, per_class) = OLD, [per_class, 2*per_class) = NEW
    rows = len(names)
    row_h = thumb + 16
    canvas = Image.new("RGB", (cols * thumb, rows * row_h), (30, 30, 30))
    draw = ImageDraw.Draw(canvas)

    for row, c in enumerate(sorted(names)):
        y0 = row * row_h
        draw.text((2, y0), "%d:%s" % (c, names[c]), fill=(255, 255, 0))
        for source_idx, source in enumerate(("OLD", "NEW")):
            base_col = source_idx * per_class
            items = buckets.get((c, source), [])[:per_class]
            for i, (ip, (cx, cy, w, h)) in enumerate(items):
                try:
                    img = Image.open(ip).convert("RGB")
                except Exception:
                    continue
                iw, ih = img.size
                bx1 = max(0, int((cx - w / 2) * iw) - 4)
                by1 = max(0, int((cy - h / 2) * ih) - 4)
                bx2 = min(iw, int((cx + w / 2) * iw) + 4)
                by2 = min(ih, int((cy + h / 2) * ih) + 4)
                crop = img.crop((bx1, by1, bx2, by2)).resize((thumb, thumb))
                x0 = (base_col + i) * thumb
                canvas.paste(crop, (x0, y0 + 16))
                tag_color = (0, 255, 0) if source == "OLD" else (255, 80, 80)
                draw.text((x0 + 2, y0 + 16), source[0], fill=tag_color)
    canvas.save(out_path, quality=90)
    return out_path
```

- [ ] **Step 5: 重跑测试，确认通过**

```bash
cd /d/WJ/jixiebi/dataset
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" test_build_dataset4.py
```

预期输出：
```
PASS: test_collect_merges_multiple_roots
PASS: test_collect_detects_cross_root_basename_collision
ALL TESTS PASSED
```

若失败，读报错信息定位（多半是 `SRC_IMG_ROOTS`/`SRC_LBL_ROOTS` 拼写或 monkeypatch 还原逻辑问题），修复后重跑，不要跳过。

---

### Task 3: 跑 QA 门禁 + 构建合并数据集

**Files:**
- 无新文件（运行 Task 2 产出的脚本）

**Interfaces:**
- Consumes: Task 2 的 `build_dataset4.py`
- Produces: `D:\WJ\jixiebi\dataset\battery4_yolo\{images,labels}\{train,val}`、`battery4_calib\`、`battery4_yolo\battery4.yaml`；本任务报告实际的 train/val/calib 计数供 Task 4 使用

- [ ] **Step 1: 跑 QA 模式（不写文件，只看报告 + 生成 montage）**

```bash
cd /d/WJ/jixiebi/dataset
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" build_dataset4.py --qa
```

核对输出：
- `images found` 应接近 894（394 旧 + 500 新，减去个别真实缺失文件的误差）。
- `labels found` 应接近 893（新数据 499 个标签里有 1 张图缺标签，属已知正常情况）。
- `!! basename collisions` 一行**必须不出现**（如果出现，说明新旧数据编号意外重叠，立即停止，不要继续，回报用户）。
- `label format errors: 0`。
- 每类实例数（`per-class: id name instances images` 那张表）四类都应有数百级别的实例，不应有某一类明显是 0 或个位数（如果某类样本极少，说明可能是类别序假设错了，标签全砸进了别的类——下一步用 montage 交叉验证）。
- 记下 `usable pairs: N` 这个数字，供后续核对。

- [ ] **Step 2: 用 Read 工具查看 QA montage 图片，人工核实类别序**

```
文件路径: D:\WJ\jixiebi\dataset\_qa_montage_battery4_merged.jpg
```

用 Read 工具打开这张图。图里每一行是一个类别（21700/18650/9V/AA），每行左半是 OLD（旧 vm.jpg 数据，绿色 "O" 标签，已知准确）样本裁剪，右半是 NEW（`new_source`，红色 "N" 标签，本次要验证）样本裁剪。逐行对比：**同一行里 OLD 和 NEW 的裁剪图看起来应该是同一种电池**（21700 明显比 18650 粗长、9V 是方形叠层电池、AA 细长）。如果某一行 OLD 是圆柱电池而 NEW 却明显是方形 9V（或反之），说明 `new_source` 的类别序和假设不一致——**停下**，不要继续到 Step 3，把发现的错位情况报告给用户，等确认正确映射后再回来修正 `NAMES`/标签处理逻辑。

- [ ] **Step 3: 类别序确认无误后，跑真正的构建**

```bash
cd /d/WJ/jixiebi/dataset
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" build_dataset4.py
```

预期输出末尾的 `== BUILD ==` 段落打印 `train=N val=M calib=K`，其中 `N+M` 应等于 Step 1 记下的 `usable pairs`，`K`（calib）应等于 `N`（train 全量，减去极少数空标签负样本——calib 只收非空标签图，这是既有逻辑不是新引入的）。

- [ ] **Step 4: 校验产出目录结构**

```bash
echo "train images: $(find /d/WJ/jixiebi/dataset/battery4_yolo/images/train -type f | wc -l)"
echo "train labels: $(find /d/WJ/jixiebi/dataset/battery4_yolo/labels/train -type f | wc -l)"
echo "val images: $(find /d/WJ/jixiebi/dataset/battery4_yolo/images/val -type f | wc -l)"
echo "val labels: $(find /d/WJ/jixiebi/dataset/battery4_yolo/labels/val -type f | wc -l)"
echo "calib images: $(find /d/WJ/jixiebi/dataset/battery4_calib -type f | wc -l)"
cat /d/WJ/jixiebi/dataset/battery4_yolo/battery4.yaml
```

预期：train images 数量 = train labels 数量（一一对应），val 同理；`battery4.yaml` 的 `names:` 段落必须仍是 `0: '21700'` / `1: '18650'` / `2: '9V'` / `3: 'AA'`（未被意外改动）。把这里的实际 train/val 数字记下来，Task 4 会用到（估计 train≈760、val≈134，但以实际打印为准）。

---

### Task 4: 训练——冒烟测试 + 正式训练

**Files:**
- Create: `D:\WJ\jixiebi\dataset\smoke_train_battery4_merged.py`
- Create: `D:\WJ\jixiebi\dataset\run_train_battery4_merged.py`

**Interfaces:**
- Consumes: Task 3 的 `D:\WJ\jixiebi\dataset\battery4_yolo\battery4.yaml`
- Produces: `best.pt` 的实际路径（`results.save_dir/weights/best.pt`，run 名可能因目录已存在被 ultralytics 自动加后缀，以脚本实际打印为准），供 Task 5 使用

- [ ] **Step 1: 写冒烟测试脚本**

```python
# D:\WJ\jixiebi\dataset\smoke_train_battery4_merged.py
# Quick stability check: workers=2 with only 3 epochs, before committing to the
# full multi-hour run. If this crashes with WinError 1455 (DLL load failure,
# pagefile too small for N worker processes each loading torch+CUDA), fall back
# to workers=0 for the real run in run_train_battery4_merged.py.
import sys, os

if __name__ == "__main__":
    import multiprocessing
    multiprocessing.freeze_support()

    sys.path.insert(0, r"D:\WJ\jixiebi\ai\esp-detection")
    os.chdir(r"D:\WJ\jixiebi\ai\esp-detection")
    from train import Train

    results = Train(
        pretrained_path=None,
        dataset=r"D:\WJ\jixiebi\dataset\battery4_yolo\battery4.yaml",
        imgsz=224,
        name="battery4_merged_smoke",
        epochs=3,
        workers=2,
    )
    print("SMOKE_SAVE_DIR=%s" % results.save_dir)
```

- [ ] **Step 2: 跑冒烟测试（前台跑，就 3 epoch，几分钟内应该出结果）**

```bash
cd /d/WJ/jixiebi/dataset
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" smoke_train_battery4_merged.py
```

**分支判断**：
- 若崩溃报 `OSError` / `WinError 1455` / `DataLoader worker exited unexpectedly`：`workers=2` 在本机不稳定，Step 3 的正式训练脚本改用 `workers=0`。
- 若 3 个 epoch 正常跑完、打印 `SMOKE_SAVE_DIR=...`、且 box_loss/cls_loss 数值有限（非 NaN/inf）：`workers=2` 可用，Step 3 保持 `workers=2`。

- [ ] **Step 3: 写正式训练脚本（workers 数值按 Step 2 结论填入）**

```python
# D:\WJ\jixiebi\dataset\run_train_battery4_merged.py
# Full training run on the merged dataset (old 394 + new_source 500).
# workers value: fill in 2 or 0 per smoke_train_battery4_merged.py's outcome.
import sys, os

if __name__ == "__main__":
    import multiprocessing
    multiprocessing.freeze_support()

    sys.path.insert(0, r"D:\WJ\jixiebi\ai\esp-detection")
    os.chdir(r"D:\WJ\jixiebi\ai\esp-detection")
    from train import Train

    results = Train(
        pretrained_path=None,
        dataset=r"D:\WJ\jixiebi\dataset\battery4_yolo\battery4.yaml",
        imgsz=224,
        name="battery4_merged",
        workers=2,  # <-- set to 0 here if Step 2's smoke test crashed
    )
    print("SAVE_DIR=%s" % results.save_dir)
```

- [ ] **Step 4: 后台启动正式训练，日志落盘**

```bash
cd /d/WJ/jixiebi/dataset
mkdir -p logs
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" run_train_battery4_merged.py > logs/train_battery4_merged.log 2>&1 &
echo "training PID: $!"
```

用 `run_in_background` 方式跑（后台任务，不阻塞会话）。预计 3-6+ 小时（视 Step 2 的 workers 结论而定）。

- [ ] **Step 5: 定期检查训练进度（不要频繁 poll，几十分钟看一次）**

```bash
tail -30 /d/WJ/jixiebi/dataset/logs/train_battery4_merged.log
```

关注：epoch 计数是否在推进、box_loss/cls_loss 是否总体下降、是否有 `best.pt saved` 之类的里程碑输出。

- [ ] **Step 6: 训练完成后验证产出**

```bash
grep "SAVE_DIR=" /d/WJ/jixiebi/dataset/logs/train_battery4_merged.log
```

记下打印的 `SAVE_DIR` 路径（形如 `runs/detect/battery4_merged` 或 `runs/detect/battery4_merged2`，取决于目录是否已存在），核对：

```bash
ls -la "/d/WJ/jixiebi/ai/esp-detection/<SAVE_DIR>/weights/best.pt"
grep -A5 "best.pt saved\|Results saved" /d/WJ/jixiebi/dataset/logs/train_battery4_merged.log | tail -20
```

验收线（对应设计文档 §6）：训练日志里最终 val mAP50 ≥ 0.85（重点关注 18650 类是否也 ≥0.85——抓取主角）。若明显低于这个线，先不要往下走 Task 5，回头检查是不是 Task 3 的类别映射或数据切分出了问题。

---

### Task 5: 量化 contract 模块 + 合约校验

**Files:**
- Create: `D:\WJ\jixiebi\dataset\espdl_quantize_battery4\user_quant.py`

**Interfaces:**
- Consumes: Task 4 的 `best.pt` 路径（`<esp-detection>/<SAVE_DIR>/weights/best.pt`）；Task 3 的 `battery4_yolo/battery4.yaml`
- Produces: 可被 espdl-quantize skill 的 `run_iteration.py --check-contract` 校验通过的 `user_quant.py`；导出的 `best.onnx`

- [ ] **Step 1: 从 best.pt 导出 ONNX（复用现有 Export()）**

```bash
cd /d/WJ/jixiebi/ai/esp-detection
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" -c "
from deploy.export import Export
PT = r'<替换成 Task 4 Step 6 记下的实际 best.pt 绝对路径>'
Export(PT, [224, 224])
import os
onnx_path = PT.replace('.pt', '.onnx')
assert os.path.exists(onnx_path), 'export failed: ' + onnx_path
print('ONNX_PATH=' + onnx_path)
"
```

记下打印的 `ONNX_PATH`。

- [ ] **Step 2: 写 `user_quant.py` contract（改自 `esp-dl` 官方模板 + 项目现有 `eval_quantized_model.py`）**

```python
# D:\WJ\jixiebi\dataset\espdl_quantize_battery4\user_quant.py
# Contract module for esp-dl's espdl-quantize agent skill.
# Adapted from:
#   - esp-dl/tools/agents/skills/espdl-quantize/assets/user_quant_onnx_example.py (contract shape)
#   - D:\WJ\jixiebi\ai\esp-detection\deploy\eval_quantized_model.py (real mAP eval via
#     ultralytics DetectionValidator, nc=1 cat template -> nc=4 battery here)
#   - D:\WJ\jixiebi\ai\esp-detection\deploy\quantize.py (letterbox CaliDataset)
import os
import sys

_ESP_DETECTION = r"D:\WJ\jixiebi\ai\esp-detection"
if _ESP_DETECTION not in sys.path:
    sys.path.insert(0, _ESP_DETECTION)

import torch
from torch.utils.data import DataLoader, Dataset
from PIL import Image

# ---------------------------------------------------------------------------
# QUANT_CONFIG
# ---------------------------------------------------------------------------
QUANT_CONFIG = {
    "model_type": "onnx",
    "onnx_path": r"<替换成 Task 5 Step 1 记下的 ONNX_PATH>",
    "input_shape": [3, 224, 224],
    "batch_size": 8,
    "target": "esp32s3",
    "num_of_bits": 8,
    "device": "cuda",
    "calib_steps": 32,
    "primary_metric": "map50",
    "metric_direction": "max",
    "target_metric": 0.85,   # matches design doc §6 acceptance bar; hitting this
                             # auto-triggers the skill's Phase-4 finalize
    "analyse_steps": 8,
    "top_k_layers": 20,
}

CALIB_DIR = r"D:\WJ\jixiebi\dataset\battery4_calib"
DATA_YAML = r"D:\WJ\jixiebi\dataset\battery4_yolo\battery4.yaml"
NC = 4


# ---------------------------------------------------------------------------
# Calibration dataloader — must be letterbox (ref-espdet-train-quant-gotchas #1)
# ---------------------------------------------------------------------------
class _LetterboxCaliDataset(Dataset):
    def __init__(self, path, img_shape):
        height, width = img_shape if isinstance(img_shape, (list, tuple)) else (img_shape, img_shape)
        self.height, self.width = height, width
        self.paths = [
            os.path.join(path, n) for n in os.listdir(path)
            if n.lower().endswith((".jpg", ".jpeg", ".png", ".bmp"))
        ]
        from torchvision import transforms
        self.to_tensor = transforms.ToTensor()

    def __len__(self):
        return len(self.paths)

    def _letterbox(self, img):
        iw, ih = img.size
        scale = min(self.width / iw, self.height / ih)
        nw, nh = int(round(iw * scale)), int(round(ih * scale))
        img = img.resize((nw, nh), Image.BILINEAR)
        canvas = Image.new("RGB", (self.width, self.height), (114, 114, 114))
        canvas.paste(img, ((self.width - nw) // 2, (self.height - nh) // 2))
        return canvas

    def __getitem__(self, idx):
        img = Image.open(self.paths[idx]).convert("RGB")
        return self.to_tensor(self._letterbox(img))


def create_calib_dataloader():
    img_size = QUANT_CONFIG["input_shape"][-1]
    # num_workers=0: calib set is small (~700+ imgs but calib_steps=32 batches is
    # all that's consumed), nothing to gain from multiprocessing here, and this
    # sidesteps the whole fork/spawn discussion entirely (no worker processes to
    # fork/spawn in the first place regardless of what device the main process uses).
    return DataLoader(
        dataset=_LetterboxCaliDataset(CALIB_DIR, img_shape=img_size),
        batch_size=QUANT_CONFIG["batch_size"],
        shuffle=False,
        num_workers=0,
    )


def collate_fn(batch):
    return batch.to(QUANT_CONFIG["device"])


# ---------------------------------------------------------------------------
# Evaluation — real per-class mAP50 via ultralytics DetectionValidator,
# adapted from deploy/eval_quantized_model.py (nc=1 cat template -> nc=4 battery)
# ---------------------------------------------------------------------------
def _run_ultralytics_val(quant_graph, split="val"):
    from esp_ppq.executor import TorchExecutor
    from ultralytics import YOLO
    from ultralytics.models.yolo.detect.val import DetectionValidator
    from ultralytics.nn.modules.head import Detect
    from nn.esp_tasks import custom_parse_model
    import ultralytics.nn.tasks as tasks
    from deploy.eval_quantized_model import (
        QuantizedModelValidator,
        make_quant_validator_class,
    )

    tasks.parse_model = custom_parse_model

    device = QUANT_CONFIG["device"]
    executor = TorchExecutor(graph=quant_graph, device=device)
    QuantDetectionValidator = make_quant_validator_class(executor)

    # eval_quantized_model.py's ppq_graph_inference hardcodes NC=1 as a local
    # variable (cat example) — can't override a local via import, so replace
    # the module-level function with our own nc=4 version before running val.
    # QuantizedModelValidator.__call__ looks up `ppq_graph_inference` as a bare
    # name resolved in eval_quantized_model's own module globals at call time,
    # so reassigning eqm.ppq_graph_inference here does take effect. Safe because
    # each espdl-quantize iteration is its own fresh `python run_iteration.py`
    # process (see contract.md) — this patch never leaks across iterations.
    import deploy.eval_quantized_model as eqm

    def _patched_inference(executor, task, inputs, device):
        graph_outputs = executor(inputs)
        if task != "detect":
            raise NotImplementedError(task)
        bs = inputs.shape[0]
        boxes_ls = [(graph_outputs[i]) for i in range(0, 6, 2)]
        boxes = torch.cat([graph_outputs[2 * i].view(bs, 4, -1) for i in range(3)], dim=-1)
        scores = torch.cat([graph_outputs[2 * i + 1].view(bs, NC, -1) for i in range(3)], dim=-1)
        preds = dict(boxes=boxes, scores=scores, feats=boxes_ls)
        detect_model = Detect(nc=NC, reg_max=1, end2end=False, ch=[32, 64, 128])
        detect_model.stride = [8.0, 16.0, 32.0]
        detect_model.to(device)
        return detect_model._inference(preds)

    eqm.ppq_graph_inference = _patched_inference

    model = YOLO(r"<替换成 Task 4 Step 6 记下的实际 best.pt 绝对路径>")
    results = model.val(
        data=DATA_YAML,
        split=split,
        imgsz=QUANT_CONFIG["input_shape"][-1],
        device="cpu",
        validator=QuantDetectionValidator,
        save_json=False,
        save=False,
        plots=False,
    )
    return {
        "map50": float(results.box.map50),
        "map50_95": float(results.box.map),
        "map50_per_class": {NAMES: float(v) for NAMES, v in zip(
            ["21700", "18650", "9V", "AA"], results.box.maps)},
    }


def evaluate(quant_graph) -> dict:
    return _run_ultralytics_val(quant_graph, split="val")


def evaluate_fast(quant_graph) -> dict:
    # Same eval — model + val set are both small (mid-hundreds of images), a
    # single pass is expected to run in well under a minute even on CPU-side
    # decode. If Task 6 Step 1's baseline run shows this is actually slow
    # (>2-3 min), come back and wire up a 30-image subset here instead.
    return evaluate(quant_graph)
```

**注意两处必须替换的占位符**：`QUANT_CONFIG["onnx_path"]`（Task 5 Step 1 的输出）、`_run_ultralytics_val` 里 `YOLO(...)` 的 `best.pt` 路径（Task 4 Step 6 的输出）。这是本计划里少数几个必须由执行者用上一步的真实产出去填的值，不是遗漏。

- [ ] **Step 3: 校验 contract（skill Phase 0）**

```bash
SKILL_DIR="/d/WJ/jixiebi/ai/esp-detection/esp-dl/tools/agents/skills/espdl-quantize"
cd /d/WJ/jixiebi/dataset/espdl_quantize_battery4
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/run_iteration.py" \
  --user-quant user_quant.py \
  --output-dir outputs/contract_check \
  --check-contract
```

预期：校验通过，无 import 错误、`QUANT_CONFIG` 字段齐全、`create_calib_dataloader()`/`evaluate` 可调用。若报错，多半是路径占位符没替换或 `esp_ppq`/`onnxsim` 缺依赖（`pip install -e <esp-ppq路径>[cpu]` + `pip install -r "$SKILL_DIR/assets/extra_requirements.txt"`，按 skill SKILL.md Phase 0 的说明装）——修复后重跑，不要跳过这一步直接进 Task 6（这是 skill 自己设计的"先校验合约、再花时间跑量化"的门禁）。

---

### Task 6: 量化 Phase 1-2——基线 + 校准×TQT 强制扫描

**Files:**
- 无新文件（调用 skill 自带脚本）

**Interfaces:**
- Consumes: Task 5 的 `user_quant.py`
- Produces: `outputs/iter_0` 到 `outputs/iter_3` 的量化结果 + `outputs/comparison.json`（含 `next_step_hint`，供 Task 7 使用）

- [ ] **Step 1: Phase 1 基线（iter_0，用默认 `espdl_setting()`，不加任何额外 pass）**

```bash
SKILL_DIR="/d/WJ/jixiebi/ai/esp-detection/esp-dl/tools/agents/skills/espdl-quantize"
cd /d/WJ/jixiebi/dataset/espdl_quantize_battery4
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/run_iteration.py" \
  --user-quant user_quant.py \
  --output-dir outputs/iter_0 \
  --baseline
```

跑完读 `outputs/iter_0/metrics.json`（mAP50 数字）、`outputs/iter_0/layerwise_error.json`（top-5 误差层）。这一轮就是"现状等价基线"——设计文档 §5 里说的安全网。

- [ ] **Step 2: Phase 2——三轮强制扫描，严格顺序跑，不要并行**

`kl + TQT(default)`：
```bash
mkdir -p outputs/iter_1
cat > outputs/iter_1/setting.json <<'EOF'
{
  "iteration_id": 1,
  "rationale": "Phase 2 mandatory sweep leg 1/3: kl calibration + TQT(default schedule), per skill SKILL.md Phase 2.",
  "calib_algorithm": "kl",
  "tqt_optimization": {"enabled": true, "lr": 1e-5, "steps": 500, "block_size": 4, "is_scale_trainable": true, "gamma": 0.0, "int_lambda": 0.0, "collecting_device": "cuda"}
}
EOF
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/run_iteration.py" \
  --user-quant user_quant.py --output-dir outputs/iter_1 --setting outputs/iter_1/setting.json
```

跑完后比较一次（见下方"每轮之间跑一次 compare"），确认 `next_step_hint` 仍是 `phase-2-calib-tqt-sweep` 再继续。

`mse + TQT(default)`：
```bash
mkdir -p outputs/iter_2
cat > outputs/iter_2/setting.json <<'EOF'
{
  "iteration_id": 2,
  "rationale": "Phase 2 mandatory sweep leg 2/3: mse calibration + TQT(default schedule), per skill SKILL.md Phase 2.",
  "calib_algorithm": "mse",
  "tqt_optimization": {"enabled": true, "lr": 1e-5, "steps": 500, "block_size": 4, "is_scale_trainable": true, "gamma": 0.0, "int_lambda": 0.0, "collecting_device": "cuda"}
}
EOF
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/run_iteration.py" \
  --user-quant user_quant.py --output-dir outputs/iter_2 --setting outputs/iter_2/setting.json
```

`percentile + TQT(default)`：
```bash
mkdir -p outputs/iter_3
cat > outputs/iter_3/setting.json <<'EOF'
{
  "iteration_id": 3,
  "rationale": "Phase 2 mandatory sweep leg 3/3: percentile calibration + TQT(default schedule), per skill SKILL.md Phase 2. Composition discipline #4: never retire a calibration on calib-only score, this leg is often the hidden winner on heavy-tailed activations.",
  "calib_algorithm": "percentile",
  "tqt_optimization": {"enabled": true, "lr": 1e-5, "steps": 500, "block_size": 4, "is_scale_trainable": true, "gamma": 0.0, "int_lambda": 0.0, "collecting_device": "cuda"}
}
EOF
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/run_iteration.py" \
  --user-quant user_quant.py --output-dir outputs/iter_3 --setting outputs/iter_3/setting.json
```

**每轮之间跑一次 compare**（不要三轮跑完才看一次——任何一轮命中 `target_metric` 会短路后续）：

```bash
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/compare_iterations.py" --output-dir outputs
```

读 `outputs/comparison.json["next_step_hint"]`：只要它还是 `phase-2-calib-tqt-sweep`，继续跑下一轮；如果提前变成 `phase-4-final-report`（某一轮已达标），跳到 Task 7 的 Step 2（跳过剩余 Phase 2 轮次和整个 Phase 3）。

- [ ] **Step 3: 三轮扫描完成后确认进入 Phase 3**

```bash
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/compare_iterations.py" --output-dir outputs
```

预期 `next_step_hint["phase"]` 变成 `phase-3-residual`，且 `next_step_hint["advice"]` 里带着一份可以直接抄的 `setting.json` 模板（mutate 自 `best_iteration`）。把这份 `comparison.json` 的当前状态记下——Task 7 从这里接着走。

---

### Task 7: 量化 Phase 3+——残差修复到收敛或预算上限

**Files:**
- 无新文件

**Interfaces:**
- Consumes: Task 6 结束时的 `outputs/comparison.json` 状态
- Produces: `outputs/best/`（含最终 espdl + `setting.json`）、`outputs/final_report.md`

- [ ] **Step 1: 逐轮跑 Phase 3（最多 skill 内建 5 轮），每轮流程一致**

```bash
SKILL_DIR="/d/WJ/jixiebi/ai/esp-detection/esp-dl/tools/agents/skills/espdl-quantize"
cd /d/WJ/jixiebi/dataset/espdl_quantize_battery4
```

对每一轮（iter_4 开始）：
1. 读 `outputs/comparison.json["next_step_hint"]["advice"]`，复制里面嵌的 `setting.json` 模板（这是 mutate 自 best-so-far 的完整配置，只改一个变量）到 `outputs/iter_N/setting.json`，把 `rationale` 字段填成具体触发这个改动的层级观测（例如"iter_0 的 layer_stats.json 显示 /model.x/Conv 的 Noise Mean=0.18 > 0.1×Noise Std=0.07，触发 bias_correct"——照抄 hint 里的模板，不要只写 `enabled=true` 缺参数，尤其 equalization 模板必须带 `opt_level=2`，见 skill SKILL.md「Common pitfalls」）。
2. 跑：
```bash
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/run_iteration.py" \
  --user-quant user_quant.py --output-dir outputs/iter_N --setting outputs/iter_N/setting.json
```
3. 跑 compare：
```bash
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/compare_iterations.py" --output-dir outputs
```
4. 读新的 `next_step_hint`：
   - `phase-4-final-report` → 达标或 plateau，跳到 Step 2。
   - `phase-3-residual` → 继续下一轮（Phase 3 内建上限 5 轮）。
   - `phase-5-agent-driven` → Phase 3 五轮跑完仍未达标，进 Step 3（Phase 5，本轮设预算上限 5 轮，呼应设计文档 §5）。

- [ ] **Step 2: 达标/plateau 自动收尾——确认产出**

```bash
cat outputs/final_report.md
```

预期开头有 `<!-- auto-generated marker -->`，`## Summary` 里 `Best iteration` + `map50` 数值 + `target_metric` 对比。若 `final_report.md` 不存在（compare 没自动触发），跑一次带 `--use-full-eval` 的复核：

```bash
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/run_iteration.py" \
  --user-quant user_quant.py --setting outputs/best/setting.json \
  --output-dir outputs/iter_final_recheck --use-full-eval
```

- [ ] **Step 3（仅当进入 Phase 5 时）：≤5 轮 agent 自由组合，到预算强制收尾**

读 `next_step_hint["phase5_signals"]`（哪些 lever 已经证明有效、哪些校准算法还没在当前 lever 堆栈上试过），按 skill SKILL.md「Phase 5」一节的模式挑一个（STACK 已验证有效的 lever 组合 / CROSS-POLLINATE 校准算法 / ABLATE / DIVE INTO ARTIFACTS），每轮的 `rationale` 必须点名是哪一轮的数据支撑这个改动（skill 的硬性要求，不点名视为无依据的猜测）。跑满 5 轮或提前达标/plateau 后：

```bash
"/d/WJ/jixiebi/ai/envs/chargereborn/python.exe" "$SKILL_DIR/scripts/compare_iterations.py" \
  --output-dir outputs --finalize --force-finalize
```

（`--force-finalize` 是必须的——不加这个参数，Phase 5 里没达标又没 plateau 时脚本会硬拒绝写 `outputs/best/`，这是 skill 自己的防呆机制，不是本计划的问题。）

- [ ] **Step 4: 记录最终结果供后续任务使用**

```bash
cat outputs/best/setting.json
ls -la outputs/best/*.espdl
grep "map50" outputs/best/metrics.json
```

记下 `outputs/best/*.espdl` 的绝对路径和最终 mAP50/per-class 数字——Task 8 需要这个路径；Task 10 的文档沉淀需要这些数字。**核对**：最终 mAP50 不应低于 Task 6 Step 1 iter_0 基线（如果反而更差，说明某个 Phase 3/5 环节选错了 lever，回头看 `comparison.json["iteration_ranks"]` 确认真的是排名第一的 iteration 被选中，而不是脚本 bug）。

---

### Task 8: 固件集成

**Files:**
- Modify: `components/battery_detect4/models/s3/espdet_pico_224_224_battery4.espdl`（覆盖现有 498KB 文件）

**Interfaces:**
- Consumes: Task 7 的 `outputs/best/*.espdl` 绝对路径
- Produces: 绿色 build，供 Task 9 使用

- [ ] **Step 1: 覆盖模型文件**

```bash
cp "<Task 7 Step 4 记下的 outputs/best/*.espdl 绝对路径>" \
   "/d/WJ/jixiebi/WORKplace/components/battery_detect4/models/s3/espdet_pico_224_224_battery4.espdl"
```

Git 已有旧版本（commit `85f08f7`）兜底，不需要额外手动备份。

- [ ] **Step 2: build**

用 `mcp__idf-bridge__build`（项目铁律：优先走 idf-bridge，不裸跑 idf.py）。

- [ ] **Step 3: 确认 build 绿 + 模型确实链入**

检查 build 输出里 map 文件或 build 日志确认新 espdl 的字节数变化被链入（新旧文件大小大概率不同，可用 `mcp__idf-bridge__size` 交叉核对 flash 占用是否随之变化）。build 红则转 `esp-build-fix` 流程，不要带着红 build 进 Task 9。

---

### Task 9: 上板验证 + 阈值/边缘过滤重新标定

**Files:**
- Modify: `components/battery_detect4/espdet4_detect.hpp`（`default_score_thr`，按实测调整）
- Modify: `components/ai/ai.cpp`（边缘框过滤 margin，如实测需要调整；大概率不用动）

**Interfaces:**
- Consumes: Task 8 的绿色 build
- Produces: 上板验证通过的固件 + 更新后的阈值

**⚠️ flash 操作前必须向用户当场确认（项目铁律，不得跳过）。**

- [ ] **Step 1: 向用户确认后 flash**

用 `mcp__idf-bridge__flash`（`FLASH_BAUD=921600`，失败回退 `460800`）。

- [ ] **Step 2: 打开 monitor，观察真实置信度**

用 `mcp__idf-bridge__monitor_start` 或 `/esp-monitor`。请用户配合：先在当前光照下（不额外补光，对应 `new_source` 的"打光前"场景）对着摄像头放一颗已知电池，读几帧串口打印的真实 score 数字；再让用户开补光灯（对应"打光后"场景），重复同样动作。**只信串口打印的真实数字，不要臆测**（`ref-espdet-train-quant-gotchas` #8 的既有教训）。

- [ ] **Step 3: 按实测数字调整阈值**

若两种光照下的真实置信度都显著高于现状的 0.08~0.27（比如稳定在 0.3+），把 `components/battery_detect4/espdet4_detect.hpp` 的 `default_score_thr` 上调（具体数值取实测最弱光照条件下真阳性分数的一个安全下界，不要凭空定，也不要照抄这里任何示例数字）。若边缘假框现象（`ai.cpp` 里过滤的那种碰画面边缘的假框）在新模型上依然出现，边缘过滤逻辑保留；若新模型上已经不再需要，可以视实测决定是否放宽或保留（保留无害，只是可能损失极少数真实贴边的检测）。

- [ ] **Step 4: 改完重新 build + flash + 复验**

重复 Task 8 Step 2 + 本任务 Step 1-2，确认调整后两种光照下都能稳定出框、无边缘假框、连续跑（至少 60 秒以上）无 WDT/panic/brownout。

- [ ] **Step 5: 核对设计文档 §6 全部验收线**

- [ ] Float mAP50 ≥ 0.85（Task 7 已确认）
- [ ] 上板单帧 ≤ ~300ms（读 monitor 日志里的 `infer=` 数字）
- [ ] 连续跑无 WDT/panic/brownout
- [ ] 打光前/打光后两种光照下置信度均较现状（0.08~0.27）有明显提升

全部打勾后，向用户展示 A/B 对比（新模型 vs 现有 `git` 里的旧模型），由用户拍板是否设为默认（不要单方面替换默认配置）。

---

### Task 10: 文档沉淀 + 最终提交

**Files:**
- Modify: `docs/ai/MODEL_PIPELINE.md`（升版本号，记录本轮流程）
- Modify: `docs/ai/CRASH_SIGNATURES.md`（若上板过程中出现新崩溃签名）
- Modify: 记忆 `brain-vision-progress`、`ref-espdet-train-quant-gotchas`（通过 Write 工具直接改 `C:\Users\WJ0706\.claude\projects\D--WJ-jixiebi-WORKplace\memory\` 下对应文件）

**Interfaces:**
- Consumes: Task 1-9 全部产出的具体数字/发现
- Produces: 最终 git 提交

- [ ] **Step 1: 更新 `docs/ai/MODEL_PIPELINE.md`**

在文件末尾追加一节记录本轮（数据源、train/val/calib 实际计数、workers 结论、best mAP50、espdl-quantize 最终 setting 摘要、上板实测置信度对比），格式仿照文件里已有的编号小节风格。

- [ ] **Step 2: 更新 `ref-espdet-train-quant-gotchas` 记忆文件**

追加编号条目（接续现有 #12）记录本轮新踩的坑，至少包括：`workers=2` 是否真的在本机稳定跑通（无论结果是"稳定可用"还是"依然崩溃回退0"，都是有价值的记录）；espdl-quantize skill 最终收敛在哪个 lever 组合（比如"percentile+TQT+equalization"或其他实际结果，不要照抄本计划里的示例）。

- [ ] **Step 3: 更新 `brain-vision-progress` 记忆文件**

把本轮的最终结果（模型是否替换默认、mAP/置信度提升幅度、截止日期前的剩余状态）追加进现有条目，不要重写整个文件，用 `mcp__tools__serena_edit_memory` 或直接 Edit 追加。

- [ ] **Step 4: 最终提交**

```bash
cd /d/WJ/jixiebi/WORKplace
git add components/battery_detect4/models/s3/espdet_pico_224_224_battery4.espdl \
        components/battery_detect4/espdet4_detect.hpp \
        components/ai/ai.cpp \
        docs/ai/MODEL_PIPELINE.md \
        docs/ai/CRASH_SIGNATURES.md
git status --short  # 确认没有漏掉或多带无关文件
git commit -m "feat(battery_detect4): 合并新数据重训+espdl-quantize修复置信度塌陷

数据: vm.jpg(394)+new_source(500,打光前/后)合并重切分~894张。
量化: 用esp-dl官方espdl-quantize结构化流程(Phase1-3/5)替代此前
中断3次的盲试bias_correct,esp32s3 per-tensor权重下equalization是
canonical解法,TQT此前完全未试过。
上板验证: 打光前/打光后两种光照置信度均较现状(0.08~0.27)提升。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
git log --oneline -3
```

---

## Self-Review Notes（写完后自查）

- **Spec 覆盖**：设计文档 §3(数据)→Task 2-3，§4(训练)→Task 4，§5(量化)→Task 5-7，§6(固件+验收)→Task 8-9，§7(收尾)→Task 1 + Task 10。§8/§9(风险/延后)已作为约束和验收线嵌入各任务，无需单独任务。
- **占位符扫描**：仅 Task 5 的 `onnx_path`/`best.pt` 路径、Task 4 Step 3 的 `workers` 数值是"必须由上一步实际产出回填"的值，均在文中显式标注为待替换，不是遗漏的 TBD。
- **类型/接口一致性**：`NAMES`/`NC=4`/`{0:"21700",1:"18650",2:"9V",3:"AA"}` 在 Task 2/5 两处出现，取值一致；`battery4.yaml` 路径在 Task 3/4/5 三处引用一致；`best.pt`/`best.onnx`/`espdl` 路径链路（Task4→5→7→8）逐任务传递，无断链。
