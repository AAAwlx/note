# 使用bpf系统调用编写一个ebpf程序

一个完整的ebpf程序包括两部分，一部分是被挂载到内核中的，一部分作为ebpf程序的加载器，在用户态进行处理。

```c
#include <linux/bpf.h>

static int (*bpf_trace_printk)(const char *fmt, int fmt_size, ...) = (void *) BPF_FUNC_trace_printk;

unsigned long prog(void){
    char fmt[]="Get";
    bpf_trace_printk(fmt, sizeof(fmt));
    return 0;
}
```

其中bpf_trace_printk是一个由bpf子系统提供的一个助手函数。
