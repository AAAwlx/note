# libbpf生成代码

libbpf会通过生成

```c
/*
 * struct bpf_object_skeleton - 定义一个 eBPF 对象的框架
 *
 * 这个结构体包含了加载和初始化 eBPF 对象所需的所有信息。
 * 它主要用于组织和关联 eBPF maps 和 programs，以及相关的配置数据。
 */
struct bpf_object_skeleton {
    size_t sz; /* 结构体的大小，用于确保向前/向后兼容性 */

    /* eBPF 对象的名称 */
    const char *name;

    /* 指向 eBPF 对象数据的指针 */
    const void *data;
    /* eBPF 对象数据的大小 */
    size_t data_sz;

    /* 指向一个或多个 bpf_object 结构体的指针数组 */
    struct bpf_object **obj;

    /* eBPF maps 的数量 */
    int map_cnt;
    /* bpf_map_skeleton 结构体的大小 */
    int map_skel_sz;
    /* 指向 bpf_map_skeleton 结构体数组的指针，用于定义 eBPF maps */
    struct bpf_map_skeleton *maps;

    /* eBPF programs 的数量 */
    int prog_cnt;
    /* bpf_prog_skeleton 结构体的大小 */
    int prog_skel_sz;
    /* 指向 bpf_prog_skeleton 结构体数组的指针，用于定义 eBPF programs */
    struct bpf_prog_skeleton *progs;
};
```

同时也会生成ebpf load和attach 等等的函数供用户态调用

```c
static void
minimal_bpf__destroy(struct minimal_bpf *obj)
{
	if (!obj)
		return;
	if (obj->skeleton)
		bpf_object__destroy_skeleton(obj->skeleton);
	free(obj);
}

static inline int
minimal_bpf__create_skeleton(struct minimal_bpf *obj);

static inline struct minimal_bpf *
minimal_bpf__open_opts(const struct bpf_object_open_opts *opts)
{
	struct minimal_bpf *obj;
	int err;

	obj = (struct minimal_bpf *)calloc(1, sizeof(*obj));
	if (!obj) {
		errno = ENOMEM;
		return NULL;
	}

	err = minimal_bpf__create_skeleton(obj);
	if (err)
		goto err_out;

	err = bpf_object__open_skeleton(obj->skeleton, opts);
	if (err)
		goto err_out;

	return obj;
err_out:
	minimal_bpf__destroy(obj);
	errno = -err;
	return NULL;
}

static inline struct minimal_bpf *
minimal_bpf__open(void)
{
	return minimal_bpf__open_opts(NULL);
}

static inline int
minimal_bpf__load(struct minimal_bpf *obj)
{
	return bpf_object__load_skeleton(obj->skeleton);
}

static inline struct minimal_bpf *
minimal_bpf__open_and_load(void)
{
	struct minimal_bpf *obj;
	int err;

	obj = minimal_bpf__open();
	if (!obj)
		return NULL;
	err = minimal_bpf__load(obj);
	if (err) {
		minimal_bpf__destroy(obj);
		errno = -err;
		return NULL;
	}
	return obj;
}

static inline int
minimal_bpf__attach(struct minimal_bpf *obj)
{
	return bpf_object__attach_skeleton(obj->skeleton);
}

static inline void
minimal_bpf__detach(struct minimal_bpf *obj)
{
	bpf_object__detach_skeleton(obj->skeleton);
}
```

生成的格式为 ${filename}_bpf__${op} filename为文件名 op为操作方法

操作方法包括 
1. open 
   open 的作用于打开ebpf内核文件
2. load
   load 用于将ebpf内核文件加载到内核中
3. attach 
    attach用于指定需要挂载的函数
    该函数在ebpf内核文件的使用sec宏指定
    ```c
    SEC("tracepoint/syscalls/sys_enter_write") 
    ```
4. detach 
5. destroy 
6. elf_bytes 
