amd-pstate 是 AMD CPU 性能扩展驱动程序，它在现代 AMD APU 和 Linux 内核中的 CPU 系列上引入了新的 CPU 频率控制机制。新机制基于协作处理器性能控制 （CPPC），它提供比传统 ACPI 硬件 P 状态更精细的频率管理。当前的 AMD CPU/APU 平台正在使用 ACPI P-states 驱动程序来管理 CPU 频率和时钟，仅在 3 个 P-state 中切换。CPPC 取代了 ACPI P 状态控制，并允许 Linux 内核使用灵活、低延迟的接口将性能提示直接传达给硬件。

## 性能控制

* auto_sel_en：是否允许硬件自主决定epp值（1允许，0禁止）
* max/min_perf： CPU 允许的最低性能级别
* des_perf：目标性能级别
* epp：能耗性能偏好设置。调节 CPU 响应 DES_PERF 的激进程度



## 三种模式



apci只支持三种p对比amd-pstate