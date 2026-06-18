# 18650 检测模型 · 训练→量化→部署 流水线 (MODEL_PIPELINE)

> M5 自训 18650 检测器的完整复现记录。**固件侧已 vendored 自包含**(2026-06-18):
> 模型组件在 `components/battery_detect/`、esp-dl 走 registry `==3.3.5`(`dependencies.lock` 锁定)、build 绿。
> **训练/量化工作区仍在仓外** `D:\WJ\jixiebi\ai\esp-detection`(非 git;重训/重量化才需要),本文是其唯一记录。
> 训练环境: conda env `chargereborn`(`D:\WJ\jixiebi\ai\envs\chargereborn`),见 `mem:ai-training-env`。

## 0. 结果
- Float: **mAP50 0.96 / mAP50-95 0.78**(31图42实例的多电池验证集)。
- 板上 int8+letterbox: 干净单颗 ~0.27 置信度、框准;`score_thr=0.20`。推理 ~220ms(~3.5Hz)。
- int16 实测**更差**(板上 esp-dl int16 路径置信度反而低 + 慢 2.5x)→ **用 int8**。

## 1. 数据集
- 位置: `D:\WJ\jixiebi\dataset\18650\{images,labels}`(纯英文路径!中文名 opencv 读不了)。
- 208 张: 66(round1 单颗 `18650_*`) + 142(round2 `b2_*`,**含 80 张多电池**) + 12 空背景负样本。
- 采集: 板子采集页连拍(见 `docs/ai/DATASET_GUIDE.md`)。**类名框填英文**(否则文件名中文)。
- 标注: `dataset\label_tool.py`(多框鼠标标注器,Unicode 安全)。队友分包: `dataset\make_dist.py N` → `dist\batch_k.zip`(每包自带工具+说明),回收 labels 合并进主 labels。
- 划分: `dataset\build_dataset.py` → `18650_yolo\{images,labels}\{train,val}` + `battery.yaml` + calib。
  - ⚠️ **已修**: calib 从"前50张"改成**全部 train 图**(薄校准是量化崩的根因之一)。

## 2. 训练
```
conda activate chargereborn   # 或直接用 D:\WJ\jixiebi\ai\envs\chargereborn\python.exe
cd D:\WJ\jixiebi\ai\esp-detection
python espdet_run.py --class_name battery --pretrained_path None ^
  --dataset D:\WJ\jixiebi\dataset\18650_yolo\battery.yaml --size 224 224 --target esp32s3 ^
  --calib_data D:\WJ\jixiebi\dataset\18650_calib ^
  --espdl D:\WJ\jixiebi\dataset\espdet_pico_224_224_battery.espdl ^
  --img <一张val图.jpg>
```
- `--class_name` 用 **battery**(英文,会进 C++ 标识符/组件名;`18650` 数字开头非法)。显示名"18650"在 `components/ai/ai.cpp` 的 `ai_class_name()` 改。
- 输出 best.pt → onnx → 量化 espdl → 自动生成 `esp-dl\models\battery_detect\` 部署组件。
- 续跑(已有 best.pt,跳过训练只量化): `--pretrained_path runs\detect\trainN\weights\best.pt`。

## 3. ★ 关键修复(都在外部 esp-detection,espdet_run 可能覆盖,需重打)
| 文件 | 改动 | 为什么 |
|---|---|---|
| `nn/modules/esp_head.py` | `super().__init__(nc=nc, ch=ch)`(去掉 `reg_max=1`) | esp_head 模板按新版 ultralytics 写,但钉死的是 8.3.112,`Detect.__init__` 不收 reg_max → 训练崩 |
| `train.py` | `batch=128→16`, `epochs=20→800` | **batch>数据量(66)→ 每 epoch 仅 1 步 → 完全没训**(mAP 0.01)。改后 mAP 0.96 |
| `deploy/quantize.py` `CaliDataset` | 直接 `Resize` → **letterbox(保宽高比+114填充)** | ★**头号坑**: 校准预处理必须和 训练/板上(letterbox)一致。不一致 → 量化范围估错 → 置信度塌成噪声 → **板上 n=0**。修后 int8 当场出框 |
| (量化调用) | calib=全train集, `batchsz=8` | 校准数据要足、步数要够(薄校准→360% 量化噪声) |
| `esp-dl/models/battery_detect/espdet_detect.cpp` | 去掉 `dl::image::DL_IMAGE_CAP_RGB565_BIG_ENDIAN`(用 3 参 ImagePreprocessor) | esp-dl 3.3.5 没这个枚举(模板按 esp-dl master 写)。**每次 espdet_run 重生成都会复发,必须重打** |
| `esp-dl/models/battery_detect/espdet_detect.hpp` | `default_score_thr 0.25→0.20` | 本模型 int8 绝对置信度偏低 |

> 独立重量化(不重训)脚本: `dataset\requant.py`(用现成 best.onnx + letterbox calib + int8)。

## 4. 部署到固件
1. 量化好的 espdl 在 `esp-dl\models\battery_detect\models\s3\espdet_pico_224_224_battery.espdl`。
2. **已 vendored**: 模型组件在本地 `components/battery_detect/`(IDF 自动发现),`components/ai` 经 `REQUIRES battery_detect` 引用;esp-dl 走 registry `==3.3.5`。rodata 内嵌模型(`build/espdl_models/battery_detect.espdl`)。
3. `sdkconfig.defaults`: `CONFIG_ESPDET_DETECT_MODEL_IN_FLASH_RODATA / FLASH_ESPDET_PICO_224_224_BATTERY / ESPDET_PICO_224_224_BATTERY`。
4. build(proxy 清空 + `scripts/idf.ps1` 或 idf-bridge)→ flash(COM7)→ 测(COM11 串口日志 / 浏览器 192.168.4.1)。

## 5. 复现状态
- ✅ **固件侧自包含(2026-06-18 完成)**: `battery_detect`(修好的 cpp/hpp/Kconfig/CMake + espdl)已 vendored 进 `components/battery_detect/`;`*.espdl` 解除 gitignore(交付物入库);两个 override_path 去掉(esp-dl→registry `==3.3.5`+lock,battery_detect→本地组件)。**build 绿、`dependencies.lock` 无 ai/ 外部路径**,评委 clone 即 build。
  - 板上识别复验(识别不变)待补: `$env:ESPPORT='COM7'; scripts/idf.ps1 flash` → COM11 看日志。
- ⚠️ **训练/量化复现仍需仓外工作区**: §3 训练侧修复(esp_head/train/quantize)在 `ai/esp-detection`,重训/重量化才需要;部署侧修复(espdet_detect.cpp/hpp)已随 vendored 组件入库。

## 6. 提升精度/速度的后手(按需)
- 置信度更高: QAT(蓝图正解)/ 基于预训练 espdet 微调 / 更多数据。
- 速度: `--size 160` 重训(模型砍半);当前 224 int8 ~220ms 够用。
- int16: 实测板上更差,**别用**。
