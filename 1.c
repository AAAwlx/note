#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

int main() {
    // 映射一页内存
    size_t page_size = sysconf(_SC_PAGESIZE);
    char *mapped = mmap(NULL, page_size, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mapped == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    // 写入两页数据，超过映射区域
    strcpy(mapped, "This is within the first page.");
    strcpy(mapped + page_size, "This is in the second page.");  // 越界写入

    // 解除映射
    munmap(mapped, page_size);
    return 0;
}
