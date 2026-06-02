# Rule: 编码规范（ESP-IDF / C / FreeRTOS）

## 结构
- 新增功能必须**组件化**：放 `components/<name>/`，带 `CMakeLists.txt` + `idf_component_register`，不准把所有逻辑塞 `main/`。
- **驱动与业务分层**：硬件驱动（HAL/BSP）与应用逻辑分开；驱动暴露清晰 API，不泄漏寄存器细节给业务层。
- 一个文件一个清晰职责；文件变大是"做太多"的信号，应拆分。

## 日志与常量
- 统一 `static const char *TAG = "<模块>";`，用 `ESP_LOGE/W/I/D/V`，不用裸 `printf` 做正式日志。
- 禁止 magic number：用 `#define`/`enum`/`static const` 命名，注明单位（ms/Hz/字节）。

## FreeRTOS
- 任务创建必须注明：栈大小理由、优先级理由。
- **ISR 中禁止**：阻塞调用、`ESP_LOGx`、大计算、malloc；只做最小工作 + `xQueueSendFromISR` 等。
- 共享数据用队列/信号量/事件组保护；声明清楚哪个任务/ISR 访问。
- 避免忙等；用 `vTaskDelay`/事件驱动。

## 错误处理
- 检查 `esp_err_t` 返回；关键路径用 `ESP_ERROR_CHECK` 或显式分支 + 日志。
- 资源（句柄/内存/锁）有申请必有释放路径。

## 通用
- 只改与任务直接相关的文件；不做无关重构（除非该坏味影响当前任务）。
- 匹配周围代码风格（命名、注释密度、缩进）。
