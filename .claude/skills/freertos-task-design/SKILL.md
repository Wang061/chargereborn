---
name: freertos-task-design
description: 设计 FreeRTOS 任务——栈/优先级/核绑定/同步/ISR 边界。新增任务或重构并发时用。
---
# FreeRTOS 任务设计

- **栈大小**：从经验值起（简单任务 2048~4096B，含 printf/浮点/网络更大），用 `uxTaskGetStackHighWaterMark` 实测水位后收敛；**必须在代码注释写栈大小理由**。
- **优先级**：数值越大越高。避免高优先级任务忙等饿死others；I/O 等待型可较高、计算型较低。**注释写优先级理由**。
- **核绑定**：默认不绑（让调度器选）；只有明确需要（如紧贴某外设中断、隔离实时任务）才 `xTaskCreatePinnedToCore`，并说明理由。
- **同步**：共享数据用 queue / mutex（防优先级反转，别用 binary semaphore 当锁）/ event group；生产者-消费者优先 queue。
- **ISR 边界**：ISR 内只用 `*FromISR` API，禁 ESP_LOGx/malloc/阻塞；耗时工作交给任务（队列/通知）。`portYIELD_FROM_ISR` 处理唤醒。
- **让出**：避免忙等；用 `vTaskDelay`/`ulTaskNotifyTake`/事件驱动，防 TWDT。
- **创建检查**：检查返回值；句柄/队列有创建必有销毁路径。

配合 esp-freertos-reviewer 审查。崩溃见 esp-monitor-triage（栈/WDT 段）。
