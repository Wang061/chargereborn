---
description: 审查/设计外设驱动与组件抽象。
---
调用 **esp-driver-architect** subagent 审查或设计驱动（HAL 分层、组件化、driver API、I2C/SPI/UART/LEDC 舵机 PWM）。
实现前先用 espressif-documentation 核对 5.5.4 的外设 API（避免废弃接口）。遵守 rules/coding-standard.md（分层、TAG、无 magic number）。
