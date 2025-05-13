`libiouring`（通常指 Linux io_uring 库）是 Linux 内核提供的高性能异步 I/O 接口，其用户态 API 主要通过以下几个核心接口实现。以下是关键接口分类和说明：

---

**1. 初始化与销毁接口**
| 接口 | 功能 | 示例 |
|------|------|------|
| `io_uring_queue_init(entries, ring, flags)` | 初始化 io_uring 实例 | `io_uring_queue_init(1024, &ring, 0)` |
| `io_uring_queue_exit(ring)` | 销毁 io_uring 实例 | `io_uring_queue_exit(&ring)` |

参数说明：
• `entries`：环形队列大小（必须是 2 的幂次）。

• `ring`：指向 `struct io_uring` 的指针。

• `flags`：控制选项（如 `IORING_SETUP_SQPOLL` 启用内核轮询）。


---

**2. 提交队列（SQ）和完成队列（CQ）**
| 接口 | 功能 |
|------|------|
| `io_uring_get_sqe(ring)` | 获取一个空闲的提交队列项（SQE） |
| `io_uring_submit(ring)` | 提交 SQ 中的请求到内核 |
| `io_uring_peek_cqe(ring, cqe)` | 非阻塞检查 CQ 中的完成事件 |
| `io_uring_wait_cqe(ring, cqe)` | 阻塞等待 CQ 中的完成事件 |

示例：
```c
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_read(sqe, fd, buf, len, offset); // 填充 SQE
io_uring_submit(&ring); // 提交请求

struct io_uring_cqe *cqe;
io_uring_wait_cqe(&ring, &cqe); // 等待完成
```

---

**3. 操作类型准备接口**
| 接口 | 功能 |
|------|------|
| `io_uring_prep_read(sqe, fd, buf, len, offset)` | 准备异步读操作 |
| `io_uring_prep_write(sqe, fd, buf, len, offset)` | 准备异步写操作 |
| `io_uring_prep_accept(sqe, fd, addr, addrlen, flags)` | 准备异步 accept |
| `io_uring_prep_connect(sqe, fd, addr, addrlen)` | 准备异步 connect |
| `io_uring_prep_poll_add(sqe, fd, poll_mask)` | 准备异步 poll |
| `io_uring_prep_close(sqe, fd)` | 准备异步 close |

示例：
```c
// 异步写入文件
io_uring_prep_write(sqe, fd, "Hello", 5, 0);
```

---

**4. 高级特性接口**
| 接口 | 功能 |
|------|------|
| `io_uring_register(fd, opcode, arg, nr_args)` | 注册资源（如文件描述符、缓冲区） |
| `io_uring_unregister(fd, opcode, arg, nr_args)` | 注销资源 |
| `io_uring_enter(fd, to_submit, min_complete, flags)` | 直接系统调用提交/等待事件 |

常用注册选项：
• `IORING_REGISTER_BUFFERS`：注册固定缓冲区。

• `IORING_REGISTER_FILES`：注册文件描述符表。


---

**5. 辅助工具接口**
| 接口 | 功能 |
|------|------|
| `io_uring_cq_ready(ring)` | 返回 CQ 中待处理的完成事件数 |
| `io_uring_sq_space_left(ring)` | 返回 SQ 剩余空闲槽位数 |
| `io_uring_cqe_get_data(cqe)` | 从 CQE 中获取用户数据指针 |

---

**完整示例代码**
```c
#include <liburing.h>
#include <fcntl.h>

int main() {
    struct io_uring ring;
    io_uring_queue_init(1024, &ring, 0); // 初始化

    int fd = open("test.txt", O_RDONLY);
    char buf[4096];

    // 提交异步读请求
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, fd, buf, sizeof(buf), 0);
    io_uring_submit(&ring);

    // 等待完成
    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);
    printf("Read %d bytes\n", cqe->res);

    // 清理
    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
    close(fd);
    return 0;
}
```

---

**6. 性能优化技巧**
1. 注册固定资源：减少内核-用户态拷贝。
   ```c
   io_uring_register_buffers(&ring, iovecs, nr_iovecs);
   ```
2. 启用 SQPOLL：内核线程主动轮询 SQ，减少系统调用。
   ```c
   io_uring_queue_init(1024, &ring, IORING_SETUP_SQPOLL);
   ```
3. 批处理提交：单次 `io_uring_submit()` 提交多个 SQE。

---

**7. 注意事项**
• 内核版本要求：Linux 5.1+ 支持完整功能。

• 错误处理：检查所有系统调用的返回值。

• 资源泄漏：确保销毁时调用 `io_uring_queue_exit()`。


示例：

```c
/* SPDX-License-Identifier: MIT */
/*
 * Very basic proof-of-concept for doing a copy with linked SQEs. Needs a
 * bit of error handling and short read love.
 */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "liburing.h"

#define QD	64 //// 队列深度（SQ/CQ 大小）
#define BS	(32*1024)

struct io_data {
    size_t offset;     // 当前操作的文件偏移量
    int index;         // 操作序号（0=读，1=写）
    struct iovec iov;  // 用于分散/聚集 I/O 的数据缓冲区
};

static int infd, outfd;
static int inflight;// 当前未完成的 I/O 操作计数
// 初始化一个io_uring
static int setup_context(unsigned entries, struct io_uring *ring)
{
	int ret;

	ret = io_uring_queue_init(entries, ring, 0);
	if (ret < 0) {
		fprintf(stderr, "queue_init: %s\n", strerror(-ret));
		return -1;
	}

	return 0;
}

static int get_file_size(int fd, off_t *size)
{
	struct stat st;

	if (fstat(fd, &st) < 0)
		return -1;
	if (S_ISREG(st.st_mode)) {
		*size = st.st_size;
		return 0;
	} else if (S_ISBLK(st.st_mode)) {
		unsigned long long bytes;

		if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
			return -1;

		*size = bytes;
		return 0;
	}

	return -1;
}

static void queue_rw_pair(struct io_uring *ring, off_t size, off_t offset) {
    struct io_uring_sqe *sqe;
    struct io_data *data;
    void *ptr;

    // 分配内存：数据缓冲区 + io_data 元数据
    ptr = malloc(size + sizeof(*data));
    data = ptr + size;  // 元数据紧接在数据缓冲区之后

    // 初始化元数据
    data->index = 0;    // 0 表示读操作
    data->offset = offset;
    data->iov.iov_base = ptr;  // 数据缓冲区指针
    data->iov.iov_len = size;  // 数据长度

    // 提交读请求
    sqe = io_uring_get_sqe(ring);
    io_uring_prep_readv(sqe, infd, &data->iov, 1, offset); // 准备读操作
    sqe->flags |= IOSQE_IO_LINK;  // 链接下一个 SQE（写操作）
    io_uring_sqe_set_data(sqe, data); // 关联元数据

    // 提交写请求（与读操作链接）
    sqe = io_uring_get_sqe(ring);
    io_uring_prep_writev(sqe, outfd, &data->iov, 1, offset); // 准备写操作
    io_uring_sqe_set_data(sqe, data); // 关联相同的元数据
}

static int handle_cqe(struct io_uring *ring, struct io_uring_cqe *cqe) {
    struct io_data *data = io_uring_cqe_get_data(cqe);
    int ret = 0;

    data->index++;  // 递增操作序号（0→1 表示读完成，1→2 表示写完成）

    if (cqe->res < 0) {  // 错误处理
        if (cqe->res == -ECANCELED) {  // 操作被取消，重试
            queue_rw_pair(ring, data->iov.iov_len, data->offset);
            inflight += 2;
        } else {
            printf("cqe error: %s\n", strerror(-cqe->res));
            ret = 1;
        }
    }

    // 读写均完成时释放内存
    if (data->index == 2) {
        void *ptr = (void *) data - data->iov.iov_len;
        free(ptr);  // 释放数据缓冲区
    }
    io_uring_cqe_seen(ring, cqe);  // 标记 CQE 已处理
    return ret;
}

static int copy_file(struct io_uring *ring, off_t insize) {
    struct io_uring_cqe *cqe;
    off_t this_size, offset = 0;

    while (insize) {
        // 填充 SQ 直到队列满或文件读完
        while (insize && inflight < QD) {
            this_size = (insize > BS) ? BS : insize;
            queue_rw_pair(ring, this_size, offset); // 提交读写对
            offset += this_size;
            insize -= this_size;
            inflight += 2;  // 每个 pair 增加 2 个 inflight
        }

        if (inflight) io_uring_submit(ring);  // 提交请求到内核

        // 等待完成事件
        while (inflight >= (insize ? QD : 1)) { // 非结束时至少留 1 个 CQE
            int ret = io_uring_wait_cqe(ring, &cqe); // 阻塞等待 CQE
            if (ret < 0) {
                printf("wait cqe: %s\n", strerror(-ret));
                return 1;
            }
            if (handle_cqe(ring, cqe)) return 1; // 处理 CQE
            inflight--;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
	struct io_uring ring;
	off_t insize;
	int ret;

	if (argc < 3) {
		printf("%s: infile outfile\n", argv[0]);
		return 1;
	}

	infd = open(argv[1], O_RDONLY);
	if (infd < 0) {
		perror("open infile");
		return 1;
	}
	outfd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (outfd < 0) {
		perror("open outfile");
		return 1;
	}

	if (setup_context(QD, &ring))
		return 1;
	if (get_file_size(infd, &insize))
		return 1;

	ret = copy_file(&ring, insize);

	close(infd);
	close(outfd);
	io_uring_queue_exit(&ring);
	return ret;
}
```

[liburing](https://github.com/axboe/liburing)