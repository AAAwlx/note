# slab 分配器

slab分配器有以下三个基本目标：

- 减少伙伴算法在分配小块连续内存时所产生的内部碎片
- 将频繁使用的对象缓存起来，减少分配、初始化和释放对象的时间开销
- 通过着色技术调整对象以更好的使用硬件高速缓存

## slab 分配器中用到的结构体

### 核心数据结构kmem_cache

slba分配器中的kmem_cache

```c
struct kmem_cache {
    /* 1) 每个CPU的缓存数据，在每次分配或释放时都会被访问 */
    struct array_cache *array[NR_CPUS]; 
    // 每个CPU对应的数组缓存，用于高速分配和释放对象。

    /* 2) Cache的可调参数，受 cache_chain_mutex 保护 */
    unsigned int batchcount;  // 每次从slab中分配/释放的对象批次大小。
    unsigned int limit;       // 每个CPU缓存中的最大对象数量。
    unsigned int shared;      // 每个CPU缓存间共享的对象数量限制。

    unsigned int buffer_size;            // 对象的大小，单位是字节。
    u32 reciprocal_buffer_size;          // 优化除法计算的值，跟 buffer_size 相关。

    /* 3) 在分配和释放时由后端频繁访问 */
    unsigned int flags;       // 常量标志位，定义了该缓存的特性，如是否调试、追踪等。
    unsigned int num;         // 每个 slab 中包含的对象数量。

    /* 4) cache_grow/shrink 调整缓存大小时使用的参数 */
    unsigned int gfporder;    // 每个 slab 占用的页数，以 2 的幂次方形式存储。

    // 强制指定的 GFP 标志（用于内存分配），如 GFP_DMA，控制分配区域等。
    gfp_t gfpflags;

    size_t colour;            // cache 的着色范围，用于减少 CPU 缓存冲突。
    unsigned int colour_off;  // 着色偏移量，保证不同对象的起始位置不同。
    struct kmem_cache *slabp_cache;  // 指向管理 slab 描述符的缓存。
    unsigned int slab_size;   // slab 的总大小，包括对象和管理数据。
    unsigned int dflags;      // 动态标志位，用于控制 slab 缓存的行为。

    // 对象的构造函数指针，分配对象时会调用，用于初始化对象。
    void (*ctor)(void *obj);

    /* 5) cache 的创建/移除信息 */
    const char *name;         // 缓存的名称，用于调试和识别。
    struct list_head next;    // 链表节点，用于将缓存链接到全局缓存链表中。

    /*
     * nodelists[] 位于 kmem_cache 结构的末尾，因为我们希望根据 nr_node_ids
     * 来动态调整这个数组的大小，而不是使用 MAX_NUMNODES（详见 kmem_cache_init()）。
     * 仍然使用 [MAX_NUMNODES] 是因为 cache_cache 是静态定义的，因此我们为最大数量的节点保留空间。
     */
    struct kmem_list3 *nodelists[MAX_NUMNODES]; // 每个节点的列表，管理缓存 slab 的分配和释放。

};
```

在一个 `kmem_cache` 结构中，有两个较为重要的成员，分别为cpu的缓存信息 `array` 与 slab 节点 `nodelists`

### 记录cpu缓存信息的结构array_cache

struct array_cache *array[NR_CPUS]：

```c
struct array_cache {
    //当前CPU上该slab可用对象数量
	unsigned int avail;
    //最大对象数目，和kmem_cache中一样
	unsigned int limit;
    /*同kmem_cache，batchcount指定了在per-CPU列表为空的情况下，从缓存的slab中获取对象的数目。它还表示在
     *缓存增长时,若entry数组中obj数量超过limit限制后，需要迁移entry中obj的数量（到本地节点cpu共享空闲链
     *表中）。
     */
	unsigned int batchcount;
	/*在从缓存移除一个对象时，将touched设置为1，而缓存收缩时, 则将touched设置为0。这使得内核能够确认在缓
	 * 存上一次收缩之后是否被访问过，也是缓存重要性的一个标志。
	 */
    unsigned int touched;
    /*最后一个成员entry是一个伪数组, 其中并没有数组项, 只是为了便于访问内存中array_cache实例之后缓存中的
     *各个对象而已
     */
	void *entry[];
};
```

- array_cache中的entry空数组，就是用于保存本地cpu刚释放的obj，所以该数组初始化时为空，只有本地cpu释放slab cache的obj后才会将此obj装入到entry数组。array_cache的entry成员数组中保存的obj数量是由成员limit控制的，超过该限制后会将entry数组中的batchcount个obj迁移到对应节点cpu共享的空闲对象链表中。

- entry数组的访问机制是LIFO（last in fist out），此种设计非常巧妙，能保证本地cpu最近释放该slab cache的obj立马被下一个slab内存申请者获取到(有很大概率此obj仍然在本地cpu的硬件高速缓存中)。

### kmem_list3不同node下的slab块

struct kmem_list3 *nodelists[MAX_NUMNODES]：

在每一个slab分配器中的页可能来自不同的 node 节点，slab 缓存会为不同的节点维护一个自己的 slab 链表， `kmem_cache` 结构中的成员 `nodelist` 记录了该 kmem_cache 下的所有 slab 链表。

```c
//有时也叫 kmem_cache_node
struct kmem_list3 {
	struct list_head slabs_partial;	/* partial list first, better asm code */
	struct list_head slabs_full;
	struct list_head slabs_free;
	unsigned long free_objects;//当前缓存中可供分配的空闲对象的总数，用于快速判断是否有可分配的对象
	unsigned int free_limit;//控制空闲对象的最大数量。用于限制在此列表中缓存的空闲对象的数量，以便控制内存使用
	unsigned int colour_next;	/* 每个节点的缓存着色信息 */
	spinlock_t list_lock;
	struct array_cache *shared;	/* 共享缓存，在多个 CPU 之间共享的 slab 缓存。这允许多个处理器快速访问共享的 slab 对象 */
	struct array_cache **alien;	/*用于其他 NUMA 节点的 slab 缓存。此字段允许跨 NUMA 节点访问 slab，减少分配器对远程节点的访问延迟 */
	unsigned long next_reap;	/* updated without locking */
	int free_touched;		/* 用于标记最近是否有空闲 slab 被使用过 */
};
```

kmem_list3 记录了3种slab：

* slabs_full ：已经完全分配的 slab
* slabs_partial： 部分分配的slab
* slabs_free：空slab，或者没有对象被分配

该结构体用于管理每个 NUMA 节点的 slab 列表

### slab描述符struct page

page中与slab分配器相关的地方：

为了提高linux内存利用率，slab描述符结合在页描述符struct page中,当页描述符成员flage标志设置了PG_SLAB后，struct page中的部分成员可以用来描述slab对象。

一个页，要么位于空闲链表中，要么用于作为映射页，要么在slab分配器中。

```c
//为了节省内存，struct page中许多结构体会通过union联合体进行封装，此处做了省略
struct page {
    //当页描述符成员flage标志设置了PG_SLAB后,struct page中对应的slab成员才有效
	unsigned long flags;
	void *s_mem;
    //freelist实际上是一个数组，数组中存放slab中未使用的obj的index值
	void *freelist;		/* sl[aou]b first free object */
		/* page_deferred_list().prev	-- second tail page */
	//当前slab已经使用的obj数量
	unsigned int active;		/* SLAB */
    
    /* 通过该成员将链到slab cache对应节点kmem_cache_node所管理的3个链表上*/
	struct list_head lru;
    //指向slab的slab cache描述符kmem_cache
	struct kmem_cache *slab_cache;	/* SL[AU]B: Pointer to slab */
    
    //slab首地址，待验证
    void *virtual;
}
```

freelist指向的是slab空闲obj的索引数组，该数组要和slab描述符中的active成员搭配使用。freelist指向的区域是一个连续地址的数组，它保存的地方有两种情况：一种情况是放在slab自身所在的内存区域，另外一种情况是保存在slab所在内存区域的外部(若保存在slab外部，则保存在slab对应slab cache的kmem_cache结构体中freelist_cache成员指针指向的链表).

