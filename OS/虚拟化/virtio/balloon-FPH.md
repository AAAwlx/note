# Free Page Hinting

对，**Free Page Hinting 就是通过 `alloc_pages()` 将空闲页块申请出来，暂时占住这些页面**。

```text
alloc_pages(..., MAX_ORDER)
  → 分配器取出连续空闲页块，其他分配请求不能再使用
  → 将页块地址和长度提交到 free_page_vq
  → 放入 free_page_list，由驱动持有
  → 收到 host DONE，或内存压力触发 shrinker
  → free_pages(..., MAX_ORDER)，归还分配器
```

所以“空闲页”指的是**申请前没有被使用、可以提供给 host 作提示的页面**；申请成功后，从 guest 分配器角度看，它们已经是被驱动占用的页。

还有两个关键点：

- **队列 ACK 不会自动释放页块**，页面仍保留在 `free_page_list`。
- **shrinker 可以在 host 尚未处理完时提前释放页块**，这是 hinting 允许的行为。