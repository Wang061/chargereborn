---
name: esp-idf-build-fix
description: ESP-IDF 构建错误的系统化排查（CMake/Kconfig/链接/组件/include/target/区溢出）。build 红时用。
---
# ESP-IDF 构建错误排查

1. 看 `mcp__idf-bridge__build` 的 `errors` 分类 + `tail`（或 logs/build/ 最新日志）。
2. 按类型对症：

| 类型 | 典型原因 | 修法 |
|---|---|---|
| missing-include / 头找不到 | 组件 `CMakeLists.txt` 的 `REQUIRES`/`PRIV_REQUIRES` 缺、`INCLUDE_DIRS` 未加 | 补依赖/包含目录 |
| undefined reference(链接) | 源文件未入 `SRCS`、缺组件依赖、声明≠定义 | 加入 SRCS / 补 REQUIRES / 对齐签名 |
| component-not-found | 组件目录名/注册名/`idf_component.yml` 错 | 修名字/路径/manifest |
| Kconfig 依赖 | 选项 `depends on` 未满足、sdkconfig 与代码不符 | 调 Kconfig / sdkconfig，用 /esp-menucheck |
| target-mismatch | build/ 是别的 target | 确认后 fullclean（ask）+ set-target esp32s3（ask）|
| region overflow | iram/flash 超 | 关无用大特性、调优化级、查分区表 |

3. **只改与该错误相关的最小范围**，重新 build 到绿。
4. 反复失败 → `/esp-loop` 自动迭代。陌生错误查 espressif-documentation。
5. 别改 target/版本、别引入并行构建系统。
