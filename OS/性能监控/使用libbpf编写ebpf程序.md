# 使用libbpf编写ebpf程序

编写ebpf内核程序：

```c
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h> // 需要包含的头文件

char LICENSE[] SEC("license") = "GPL"; // SEC宏会将此字符串放到一个elf文件中的独立段里面供加载器检查

SEC("tracepoint/syscalls/sys_enter_write") // 此处使用的tracepoint，在每次进入write系统调用的时候触发
int handle_tp(void *ctx)
{
        int pid = bpf_get_current_pid_tgid() >> 32;
        bpf_printk("BPF triggered from PID %d.\n", pid); //可cat /sys/kernel/debug/tracing/trace查看调试信息
        return 0;
}
```

编译 ebpf 内核程序：

```sh
clang -g -O2 -target bpf -D__TARGET_ARCH_x86 -I/usr/src/kernels/$(uname -r)/include/uapi/ -I/usr/src/kernels/$(uname -r)/include/ -I/usr/include/bpf/ -c minimal.bpf.c -o minimal.bpf.o
```