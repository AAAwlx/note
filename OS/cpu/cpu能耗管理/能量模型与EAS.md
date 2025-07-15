# 能量模型与EAS

## 能量模型

### perf_domain

​​性能域（perf_domain）​​ 是 Linux 内核调度器 EAS 中的一个概念，用于描述​​一组具有相同性能特性的 CPU​​。同时对于这样一组 CPU 都有自己对应的 cpufreq policy。

```c
/* 性能域结构体 */
struct perf_domain {
	struct em_perf_domain *em_pd; /* 能源模型性能域指针 */
	struct perf_domain *next;     /* 指向下一个性能域的指针 */
	struct rcu_head rcu;          /* RCU 头，用于延迟销毁 */
};
```

如果一个平台有 ​​4+3+1​​ 的 CPU 架构（如 4 个大核、3 个中核、1 个小核），那么：

​​大核（4 个）​​ → 1 个 cpufreq policy→ 1 个 perf_domain
​​中核（3 个）​​ → 1 个 cpufreq policy→ 1 个 perf_domain
​​小核（1 个）​​ → 1 个 cpufreq policy→ 1 个 perf_domain

因此，整个系统会有 ​​3 个 perf_domain​​，它们以链表形式组织，链表头存储在 ​​root domain​​ 中。

### em_perf_domain

在EM framework中，我们使用em_perf_domain来抽象一个performance domain：

```c
struct em_perf_domain {
	struct em_perf_table __rcu *em_table;// 指向运行时可修改的 em_perf_table 的指针
	int nr_perf_states;// 性能状态的数量
	int min_perf_state;// 允许的最小性能状态索引
	int max_perf_state;// 允许的最大性能状态索引
	unsigned long flags;
	unsigned long cpus[];
};
```
```c
/**
 * struct em_perf_table - 性能状态表
 * @rcu:	用于安全访问和销毁的 RCU
 * @kref:	用于跟踪用户的引用计数器
 * @state:	按升序排列的性能状态列表
 */
struct em_perf_table {
	struct rcu_head rcu; // RCU 用于在并发环境中安全地更新和销毁数据
	struct kref kref;    // 引用计数器，用于管理对象的生命周期
	struct em_perf_state state[]; // 性能状态数组，按升序排列
};
```

### struct em_perf_state

每个性能域都有若干个perf level，每一个perf level对应能耗是不同的，Linux 中使用 struct em_perf_state 来表示一个perf level下的能耗信息。

```c
struct em_perf_state {
	unsigned long performance; // 在给定频率下的 CPU 性能（容量）
	unsigned long frequency;   // 以 KHz 为单位的频率，与 CPUFreq 保持一致
	unsigned long power;       // 在此级别消耗的功率（由 1 个 CPU 或注册设备消耗）。它可以是总功率：静态和动态功率的总和。
	unsigned long cost;        // 成本系数,与此级别相关的成本系数，用于能量计算。
	unsigned long flags;       // 标志位
};
```

能量计算公式：

power * max_frequency / frequency
