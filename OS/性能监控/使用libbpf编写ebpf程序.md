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

在ebpf的内核程序编译完成后使用需要编写用户态程序

```c
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h> // rlimit使用
#include <bpf/libbpf.h>
#include "minimal.skel.h" // 这就是上一步生成的skeleton，minimal的“框架” 

int main(int argc, char **argv)
{
        struct minimal_bpf *skel; // bpftool生成到skel文件中，格式都是xxx_bpf。
        int err;

		struct rlimit rlim = {
        	.rlim_cur = 512UL << 20,
        	.rlim_max = 512UL << 20,
    	};
		// bpf程序需要加载到lock memory中，因此需要将本进程的lock mem配大些
    	if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
        	fprintf(stderr, "set rlimit error!\n");
        	return 1;
    	}
        // 第一步，打开bpf文件，返回指向xxx_bpf的指针（在.skel中定义）
        skel = minimal_bpf__open();
        if (!skel) {
                fprintf(stderr, "Failed to open BPF skeleton\n");
                return 1;
        }

       // 第二步，加载及校验bpf程序
        err = minimal_bpf__load(skel);
        if (err) {
                fprintf(stderr, "Failed to load and verify BPF skeleton\n");
                goto cleanup;
        }

        // 第三步，附加到指定的hook点
        err = minimal_bpf__attach(skel);
        if (err) {
                fprintf(stderr, "Failed to attach BPF skeleton\n");
                goto cleanup;
        }

        printf("Successfully started! Please run `sudo cat /sys/kernel/debug/tracing/trace_pipe` "
               "to see output of the BPF programs.\n");

        for (;;) {
                /* trigger our BPF program */
                fprintf(stderr, ".");
                sleep(1);
        }
cleanup:
        minimal_bpf__destroy(skel);
        return -err;
}
```

编译上述代码 # gcc -I/usr/src/kernels/$(uname -r)/include/uapi/ -I/usr/src/kernels/$(uname -r)/include/ -I/usr/include/bpf/ -c minimal.c -o minimal.o

链接成为可执行程序

gcc minimal.o -lbpf -lelf -lz -o minimal
