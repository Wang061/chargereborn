# 设计：4类电池检测器数据刷新 + 量化置信度塌陷修复

> **生成**：2026-07-05
> **作者**：ChargeReborn Brain 视觉主轴
> **前置**：`docs/ai/MODEL_PIPELINE.md`（训练→量化→部署流水线 v3）、`docs/ai/DECISIONS.md`（2026-07-04 4类重训决策）、记忆 `brain-vision-progress`、`ref-espdet-train-quant-gotchas`、`docs/superpowers/specs/2026-06-30-vision-robustness-dataset-design.md`（本轮沿用其"训练分布对齐部署分布"方法论，但改跑在 4 类分支上）
> **硬约束**：投稿截止 **2026-07-09 18:00**（今日 07-05，剩 ~4 天），G1 硬件 session 与本轮并行竞争时间。
> **本文作用**：定义"用队友新拍的 500 张数据重训 4 类检测器 + 解决 int8 量化置信度塌陷"这一轮的范围、方法、验证与切分。

---

## 0. 一句话

合并旧 394 张（`vm.jpg`）+ 新 500 张（`new_source`，含刻意补光前/后对照）≈894 张重新训练 ESPDet-Pico nc=4（224×224 不变），用 esp-dl 官方 `espdl-quantize` 结构化调参方法（而非盲试 bias_correct）系统性解决 int8 置信度较 float 塌陷 5-10x 的问题，上板 A/B 后由用户拍板是否替换当前默认模型。

## 1. 背景与根因

**现状（分支 `feat/battery-espdet4`，已上板但有已知缺陷）**：
- 4 类 ESPDet-Pico（21700/18650/9V/AA），float mAP50=0.982（394 张旧数据集，317 train/56 val）。
- int8 量化后板上实测置信度较 float **塌陷 5-10x**（AA: float 0.92 → 板上 0.08~0.27），已用 `score_thr` 0.20→0.08 + 边缘框过滤兜底，能出框但检出率打折（约 60-70%）。
- 根因两条，**互相独立、都要治**：
  1. **量化侧**：`target=esp32s3` 架构上只支持 per-tensor 权重量化（per-channel 只有 esp32p4 才有），4 类 score head 各通道值域差异在 per-tensor 量化下被迫共用一个 scale，精度被最宽通道"稀释"。已尝试的 `bias_correct+equalization` 重量化 3 次因进程被外部打断（后台任务/会话边界，非 CUDA 崩溃——已用日志核实）未跑完。
  2. **训练侧**：现训练集几乎全部来自单一采集批次，弱光/杂背景泛化弱（沿用单类模型时期就观察到的规律：好光 0.6、弱光~0.2）。队友今早新拍 500 张，特意分"打光前"(301张)/"打光后"(199张)两批，直接冲着补齐光照多样性去的。

**新数据**：`D:\WJ\jixiebi\ai\new_source`，500 张 iPhone 照片 + 499 个 YOLO 格式标签（makesense.ai 导出，1 张漏标属正常"未标注排除"）。无 `classes.txt`，类别序假设与现有 `battery4.yaml`（0=21700/1=18650/2=9V/3=AA）一致，**未验证**——构建数据集时会加可视化 montage 核实。

## 2. 范围

**做**：
- 合并新旧数据重新训练（224×224，from scratch，不改分辨率——用户已确认这轮不引入分辨率变量）。
- 用 esp-dl 官方 `espdl-quantize` 结构化方法解决量化置信度塌陷（本轮重点，用户明确要求）。
- 固件集成 + 阈值/边缘过滤重新标定 + 上板 A/B 验收。

**不做（本轮明确排除）**：
- 分辨率改动（160×160 等）——已问过用户，维持 224 不动。
- 扩类（5类+）、鼓包/漏液异常二分类——蓝图后续工作，不在本轮。
- 系统侧多帧投票/迟滞兜底——沿用 0630 设计文档的"本轮不做"结论。

## 3. 数据流水线

把 `D:\WJ\jixiebi\dataset\build_dataset4.py` 的单一 `SRC_IMG_ROOT`/`SRC_LBL_DIR` 泛化成多源列表：

- 图像源（递归 glob）：`vm.jpg\vm.jpg`（394，旧）+ `new_source\打光前`（301）+ `new_source\打光后`（199）。
- 标签源（平铺 glob `*.txt`）：`vm.text`（旧）+ `new_source` 下两个 `labels_my-project-name_*` 时间戳文件夹（新）。
- 按 basename 合并，沿用现有跨源冲突检测（脚本级硬校验，不只是口头核对——已确认新旧 IMG 编号范围不重叠：旧 3641~4040，新 4076~4581，但仍跑检测不假设）。

**新增 QA 步骤**（因为新数据没带 classes.txt）：出一版按类别抽样的可视化 montage（仿旧流水线 `dataset/18650/_qa_montage.jpg` 做法），肉眼确认 new_source 的类别序确实是 21700/18650/9V/AA，而不是假设成立就直接喂进训练。

合格后按现有"class-signature 分层抽 15% 入 val"逻辑重切分：预计 train≈760/val≈134，calib=全部 train（沿用"薄校准是量化崩根因"教训）。额外核对：val 集里"打光前"和"打光后"两种光照都有代表性样本（不需要新增分层逻辑，随机分层抽样已经会覆盖，只是要verify）。输出位置不变：`battery4_yolo/{images,labels}/{train,val}` + `battery4_calib/` + `battery4.yaml`，类名映射不变，固件侧 `ai_class_name()` 零改动。

## 4. 训练

沿用 `D:\WJ\jixiebi\ai\esp-detection\train.py` 现有配置：`epochs=800` 天花板 / `patience=100`(默认早停) / `batch=16` / `imgsz=224` / `device="0"`(GPU) / `mosaic=1.0` `mixup=0.0` `copy_paste=0.1` 等增强不动。`pretrained_path=None` 从零训（沿用既有先例，不做微调）。

**worker 策略**：已知 `workers=8` 在本机会因 pagefile 不足报 `WinError 1455`（DLL 加载失败）。这次数据量从 317→~760 张（batch=16 下约 20→48 batches/epoch），若仍用 `workers=0` 同步加载，单 epoch 时长预计从 ~20s 涨到 ~48s，配合上次 best 出现在 epoch412 的经验，整体训练可能要 3-6+ 小时。计划：先用 `workers=2` 冒烟测试 2-3 个 epoch，确认不复现 DLL 崩溃（当前系统仅 2.8GB 空闲物理内存，但 pagefile 有 32GB 富余空间）；稳定则用 `workers=2` 跑正式训练，不稳定立即回退 `workers=0`。全程作为可轮询的后台任务运行、日志落盘，不阻塞会话；在冒烟测试通过、训练明显企稳或出现里程碑式 mAP 提升、训练收尾这几个自然节点向用户汇报，不会让用户长时间干等或反过来毫无音信。

## 5. 量化（本轮重点：结构化解决置信度塌陷，而非重试猜测）

**关键发现**：`D:\WJ\jixiebi\ai\esp-detection\esp-dl\tools\agents\skills\espdl-quantize\`（本地已有，2026-06-11 clone）是 ESP-DL 官方提供的量化调参 agent skill，专门解决"int8 精度塌陷，该调哪个 esp-ppq 参数"这类问题，比之前手动猜 `bias_correct+equalization` 严谨得多。核心事实：

- **`target=esp32s3` 架构上权重只能 per-tensor 量化**（per-channel 只有 esp32p4 有）。之前"score head 4通道值域不同、PPQ 未做 per-channel"的猜测其实是 esp32s3 的硬约束，不是漏配。官方文档明确：**per-tensor 权重目标下，layer-wise equalization 是标准 Tier-A 解法**——正是之前中断的 `quant_battery4_bc.py` 已经在用但没跑完的手段。
- 但那次直接跳过了**校准算法**（kl/mse/percentile）和 **TQT**（训练量化阈值）——skill 文档把 TQT 称为"esp-dl POWER_OF_2 目标上最强的单一手段"，之前完全没试过；且明确警告"跳过 Phase 2 校准×TQT 组合扫描是 esp-dl 目标上头号搜索失败原因"（校准算法单独评分不能预测和 TQT 组合后的表现，percentile 单独看可能变差、配合 TQT 反而是最优）。
- TQT 是梯度训练，官方建议用 GPU（CPU 上慢 10-100x）——这正好**消解**而非需要绕开 CUDA 顾虑：校准 DataLoader 本身维持 `num_workers=0`（校准集小，没有并行的必要，也就没有可以 fork 的子进程），量化调参进程本身正常用 GPU 即可，不需要之前设想的 `CUDA_VISIBLE_DEVICES=""` 防御手段。
- `deploy/eval_quantized_model.py`（已在库）是官方模板，把量化后的 PPQ 图接进 ultralytics 真实 `DetectionValidator`算出实打实的 per-class mAP50，不是玩具指标——改 `NC=1→4`、`esp32p4→esp32s3`、指向 `battery4.yaml` 即可复用，不用从零写检测后处理。

**执行方式**：写 `user_quant.py` contract（skill 要求的固定接口：`QUANT_CONFIG` + `create_calib_dataloader()` + `evaluate(quant_graph)`），配置 `model_type=onnx`、`target=esp32s3`、`num_of_bits=8`、`device=cuda`、`primary_metric=map50`、**`target_metric` 直接取 §6 验收线（mAP50≥0.85~0.90）**——达标即触发 skill 自动 Phase-4 收尾，不用另定一套脱节的数字，calib loader 复用现有 letterbox `CaliDataset`。之后严格按 skill 自身流程走（阶段数是 skill 内建结构，不是本文臆造）：

1. **Phase 0**：`--check-contract` 校验 contract 模块。
2. **Phase 1**：跑一次默认 `espdl_setting()` 基线（iter_0），记录 mAP50 + 各类平均置信度 + top-5 误差层。**这一轮天然就是安全网**——iter_0 用的正是现状"常规量化"的等价设置，后续每一轮都会和 best-so-far 比较排名，最差结果也不会差于现状，只会持平或更好。
3. **Phase 2（强制，3 轮，严格顺序）**：`kl+TQT(default)` / `mse+TQT(default)` / `percentile+TQT(default)`，用 `compare_iterations.py` 驱动，不凭校准算法单独表现提前否决任何一个。
4. **Phase 3（skill 内建上限 5 轮，逐个单变量叠加在 best-so-far 上）**：按数据说话，候选大概率包括 `bias_correct`（score head 输出偏置修正）、`equalization`（完整模板，`opt_level=2`——esp32s3 canonical 解法）、TQT 参数升级。
5. Phase 1+2+3 合计 skill 内建 9 轮迭代。若到此仍未达 `target_metric` 且明显还有提升空间，才进 **Phase 5**（agent 自由组合/叠加）——本轮给 Phase 5 单独再设 **≤5 轮**的用户预算上限（呼应 deadline 压力），到点用 `--finalize --force-finalize` 收尾，不无限搜索。

产出：`outputs/best/` 对应的 espdl。因为 iter_0=现状等价基线、且全程排名跟踪，最终结果保证不差于现状（0.08~0.27 那版），大概率有实质提升——上板实测验证，不只信离线数字。

## 6. 固件集成 + 上板验收

胜出的 espdl 换入 `components/battery_detect4/models/s3/`（git 已有旧版本兜底）。`default_score_thr`（现 0.08，是给旧模型量化塌陷打的补丁）和 `ai.cpp` 边缘框过滤按新模型实测置信度重新标定，具体数值等上板实测，不预设。build 绿后 flash 需当场确认（项目铁律）。

验收标准（沿用 2026-07-04 定的标准 + 本轮新增一条）：
- Float mAP50 ≥ 0.85（重点看 18650，抓取主角，目标 ≥0.90）。
- 上板单帧 ≤ ~300ms（架构不变，预期与现状 220ms 持平）。
- 连续跑无 WDT/panic/brownout。
- **新增**：分别在"打光前"和"打光后"两种光照条件下实测置信度，确认较现状（0.08~0.27）有明显、可复现的提升——这才是这批数据和这轮量化工作要解决的真实问题，不能只看离线 mAP 数字。

## 7. 收尾

先把现有 3 个未提交文件（`components/ai/Kconfig` 默认切换、`components/ai/ai.cpp` 边缘过滤、`components/battery_detect4/espdet4_detect.hpp` score_thr=0.08）作为独立 checkpoint 提交，再叠这轮改动。新模型上板 A/B 通过后，是否替换当前默认——按之前先例，由用户最终拍板，不单方面替换。

完工后沉淀：
- 新踩的坑（`workers=2` 是否真稳定跑通、espdl-quantize 流程的实际最优 setting）写回 `ref-espdet-train-quant-gotchas` / `MODEL_PIPELINE.md`（升版本号）。
- 更新记忆 `brain-vision-progress`。
- 若出现新崩溃签名，`/learn` 写入 `CRASH_SIGNATURES.md`。

## 8. 风险与应对

| 风险 | 应对 |
|---|---|
| `workers=2` 复现 DLL/pagefile 崩溃 | 立即回退 `workers=0`（已知安全，只是慢），不追加尝试 workers=4/8 |
| 训练/量化 3-6+ 小时占用 GPU | 用户已确认可接受（"后台跑,到点汇报"），后台任务运行，不阻塞其他工作 |
| new_source 类别序假设错误 | 训练前先出可视化 montage 核实，发现问题立即停止并向用户报告，不会带着错误标签硬训 |
| espdl-quantize 9 轮内建迭代(+最多5轮Phase5)未达标 | 按 skill 规则 `--force-finalize` 收尾，取当前最优 iteration 的 espdl，不无限拖时间；iter_0=现状等价基线+全程排名跟踪，结果保证不差于现状，仍可能有提升即为净收益 |
| 训练/量化耗时挤压视频/报告制作时间 | 量化调参必须等训练产出 best.pt 后才能跑（严格顺序依赖，无法并行），预留至少半天缓冲；量化本身单轮几分钟量级，真正的时间大头在训练 |
| 新旧数据基线漂移（打光后图片背景单一，模型可能学到背景捷径） | 验收阶段人工检查误检情况（尤其边缘/背景干扰物），不能只信 mAP 数字 |

## 9. 明确延后（本轮不做，记录备查）

- 160×160 等分辨率/速度优化——用户已确认这轮不碰。
- 5类+扩类、鼓包/漏液异常二分类——蓝图后续独立立项。
- 系统侧多帧投票/迟滞硬件兜底——沿用 0630 文档结论。
