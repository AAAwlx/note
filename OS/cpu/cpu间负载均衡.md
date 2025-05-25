```mermaid
sequenceDiagram
    Scheduler->>+CPU0: 检测到CPU0过载
    CPU0->>+调度域: 通过find_busiest_group()找到最忙的调度组
    调度域->>+CPU1: 选择目标CPU（负载最轻的兄弟CPU）
    CPU0->>CPU1: 迁移任务（通过move_queued_task()）
    CPU1->>rq: 将任务加入本地cfs_rq红黑树
    Note right of CPU1: 更新CPU1的min_vruntime
```
### CPU 负载均衡

场景：多核系统中任务迁移时选择目标 CPU。  

PELT 会比较各 CPU 的 cfs_rq->avg.util_avg，选择最空闲的 CPU。  

避免 CPU 过载或空闲不均。  

代码路径：  

    find_idlest_cpu() -> compare util_avg of cfs_rqs
  