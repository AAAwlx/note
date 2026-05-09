# ringbuf

## 概念

ringbuf 是一个“多生产者、单消费者”（multi-producer, single-consumer，MPSC） 队列，可安全地在多个 CPU 之间共享和操作。

## ringbuf结构体

在使用map时首先要确定他的类型以及大小。

```c
struct {
    __uint(type,BPF_MAP_TYPE_RINGBUF);// 指定map的类型
    __uint(max_entries,256 * 1024);// 指定map的大小
}rb SEC(".map");
```

## 内核提供的与ringbuf相关的助手函数

该函数用于申请 ringbuf 的空间，申请好后返回一个地址。其中 size 为 ringbuf 的大小。

```c
static void *(*bpf_ringbuf_reserve)(void *ringbuf, __u64 size, __u64 flags) = (void *) 131;
```

该函数用于将data指针指向地址中的内容复制到ringbuf中，其中size为要复制内容的大小

```c
static long (*bpf_ringbuf_output)(void *ringbuf, void *data, __u64 size, __u64 flags) = (void *) 130;
```

该函数用于将 ringbuf 从内核空间提交到用户空间

```c
static void (*bpf_ringbuf_submit)(void *data, __u64 flags) = (void *) 132;
```

该函数用于将 ringbuf中指定的内容删除

```c
static void (*bpf_ringbuf_discard)(void *data, __u64 flags) = (void *) 133;
```

上述内容用于内核态向用户态传递信息，同时用户态也能向内核态传递信息，使用 ring_buffer_user__reserve ring_buffer_user__submit 等一系列函数

## libbpf中提供给用户态ringbuf的操作

```c
LIBBPF_API struct ring_buffer *
ring_buffer__new(int map_fd, ring_buffer_sample_fn sample_cb, void *ctx,
		 const struct ring_buffer_opts *opts);
LIBBPF_API void ring_buffer__free(struct ring_buffer *rb);
LIBBPF_API int ring_buffer__add(struct ring_buffer *rb, int map_fd,
				ring_buffer_sample_fn sample_cb, void *ctx);
LIBBPF_API int ring_buffer__poll(struct ring_buffer *rb, int timeout_ms);
LIBBPF_API int ring_buffer__consume(struct ring_buffer *rb);
LIBBPF_API int ring_buffer__epoll_fd(const struct ring_buffer *rb);
```

**ring_buffer__new**

该函数用于创建一个监听事件，用来监听 ring_buffer 上是否有数据的写入/读取

```c
struct ring_buffer *rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
```

其中第二个参数是该事件发生时执行的时间处理回调函数。在事件发生时会进入到该函数中进行处理。

在该事件被创建出来后可以使用 ring_buffer__poll 函数进行轮询监听。通常在循环中使用。

```c
while (1)
{
    err = ring_buffer__poll(rb, -1);
    if (err == -EINTR)
        continue;
    if (err < 0)
    {
        fprintf(stderr, "Error polling ring buffer: %d\n", err);
        break;
    }
}
```

类似于 poll 与 epoll 向监听事件增加要被监听文件的描述符一样，ring_buffer__add 函数可以向一个监听事件中添加新的被监听的 map

ring_buffer__free 用于释放这个被监听的map。 
