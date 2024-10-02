# slab 分配器

slab分配器有以下三个基本目标：

- 减少伙伴算法在分配小块连续内存时所产生的内部碎片
- 将频繁使用的对象缓存起来，减少分配、初始化和释放对象的时间开销
- 通过着色技术调整对象以更好的使用硬件高速缓存

## slab 分配器中用到的结构体

![Alt text](../image/slba分配器中的数据结构.png)

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

内核中的 `kmem_cache` 结构都被记录在 cache_chain 链表中。

```c
static struct list_head cache_chain;//用来维护所有 kmem_cache 实例（缓存池）的链表
```

在一个 `kmem_cache` 结构中，有两个较为重要的成员，分别为cpu的缓存信息 `array` 与 slab 节点 `nodelists`

### 记录cpu缓存信息的结构array_cache

为了提升效率，SLAB分配器为每一个CPU都提供了每CPU数据结构struct array_cache，该结构指向被释放的对象。当CPU需要使用申请某一个对象的内存空间时，会先检查array_cache中是否有空闲的对象，如果有的话就直接使用。如果没有空闲对象，就像SLAB分配器进行申请。

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

kmem_list3中有三个链表slabs_partial、slabs_full、slabs_free

* slabs_full ：已经完全分配的 slab
* slabs_partial： 部分分配的slab
* slabs_free：空slab，或者没有对象被分配

该结构体用于管理每个 NUMA 节点的 slab 列表

### slab描述符struct page

page中与slab分配器相关的地方：

为了提高linux内存利用率，slab描述符结合在页描述符struct page中,当页描述符成员flage标志设置了PG_SLAB后，struct page中的部分成员可以用来描述slab对象。

一个页，要么位于空闲链表中，要么用于作为映射页，要么在slab分配器中。

当 page 属于 slab 时，page->lru.next 指向 page 驻留的的缓存的管理结构，page->lru.prec 指向保存该page 的 slab 的管理结构。ji

```c
//为了节省内存，struct page中许多结构体会通过union联合体进行封装，此处做了省略
struct page {
    //当页描述符成员flage标志设置了PG_SLAB后,struct page中对应的slab成员才有效
	unsigned long flags;
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

### slab结构

freelist指向的是 slab 空闲 obj 的索引数组，该数组要和 active 成员搭配使用。freelist 指向的区域是一个连续地址的数组，它保存的地方有两种情况：一种情况是放在 slab 自身所在的内存区域，另外一种情况是保存在 slab 所在内存区域的外部(若保存在slab外部，则保存在slab对应slab cache的kmem_cache结构体中 freelist_cache 成员指针指向的链表)。在 2.6 版本中 page 结构中并无 `void *s_mem` 成员。该成员在 slab 结构中。

slab主要包含两大部分，管理性数据和obj对象，其中管理性数据包括struct slab和kmem_bufctl_t。slab有两种形式的结构，管理数据外挂式或内嵌式。如果obj比较小，那么struct slab和kmem_bufctl_t可以和obj分配在同一个物理page中，可称为内嵌式；如果obj比较大，那么管理性数据需要单独分配一块内存来存放，称之为外挂式。我们在上图中所画的slab结构为内嵌式。

```c
struct slab {
	struct list_head list;		// 满、部分满或空链表
	unsigned long colouroff;	// slab着色的偏移量
	// 在slab中的第一个对象
	void *s_mem;		/* including colour offset */		/* 包括颜色偏移的内存指针，指向 slab 的第一个对象 */
	// slab中已分配的对象数
	unsigned int inuse;	/* num of objs active in slab */
	// 第一个空闲对象（如果有的话）
	kmem_bufctl_t free;
	unsigned short nodeid;	/* 所属 NUMA 节点的 ID */
};
```

## 普通高速缓存和专用高速缓存

高速缓存被分为普通和专用两种，普通高速缓存只由 slab 分配器用于自己的目的，而专用高速缓存由内核的其余部分使用。这两种类型的 slab 分配器均在 kmem_cache_init 中被初始化。

`cache_cache` 结构用于 slab 分配器自身的内存分配。

```c
/* internal cache of cache description objs */
static struct kmem_cache cache_cache = {
	.batchcount = 1,
	.limit = BOOT_CPUCACHE_ENTRIES,
	.shared = 1,
	.buffer_size = sizeof(struct kmem_cache),
	.name = "kmem_cache",
};
```

初始化 `cache_cache`：

```c
// 初始化初始的 slab 列表
	for (i = 0; i < NUM_INIT_LISTS; i++) {
		kmem_list3_init(&initkmem_list3[i]);
		if (i < MAX_NUMNODES)
			cache_cache.nodelists[i] = NULL;
	}
	set_up_list3s(&cache_cache, CACHE_CACHE);

	// 如果总的内存超过32MB，设置slab分配器使用的页数更大，以减少碎片化
	if (totalram_pages > (32 << 20) >> PAGE_SHIFT)
		slab_break_gfp_order = BREAK_GFP_ORDER_HI;

	// 第一步: 创建缓存，用于管理 kmem_cache 结构
	node = numa_node_id();  // 获取当前 NUMA 节点的 ID

	// 初始化 cache_cache，管理所有缓存描述符
	INIT_LIST_HEAD(&cache_chain);  // 初始化 cache_chain 列表
	list_add(&cache_cache.next, &cache_chain);  // 将 cache_cache 加入链表
	cache_cache.colour_off = cache_line_size();  // 设置 cache 的颜色偏移量
	cache_cache.array[smp_processor_id()] = &initarray_cache.cache;  // 设置 per-CPU 缓存
	cache_cache.nodelists[node] = &initkmem_list3[CACHE_CACHE + node];  // 设置节点列表

	// 设置 cache_cache 的大小，确保它对齐缓存行
	cache_cache.buffer_size = offsetof(struct kmem_cache, nodelists) +
				 nr_node_ids * sizeof(struct kmem_list3 *);
	cache_cache.buffer_size = ALIGN(cache_cache.buffer_size,
					cache_line_size());
	cache_cache.reciprocal_buffer_size =
		reciprocal_value(cache_cache.buffer_size);

	// 估算缓存大小，确保有足够的空间来存放对象
	for (order = 0; order < MAX_ORDER; order++) {
		cache_estimate(order, cache_cache.buffer_size,
			cache_line_size(), 0, &left_over, &cache_cache.num);
		if (cache_cache.num)
			break;
	}
	BUG_ON(!cache_cache.num);  // 如果计算失败，触发 BUG
	cache_cache.gfporder = order;  // 设置缓存使用的页面顺序
	cache_cache.colour = left_over / cache_cache.colour_off;  // 设置缓存颜色
	cache_cache.slab_size = ALIGN(cache_cache.num * sizeof(kmem_bufctl_t) +
				      sizeof(struct slab), cache_line_size());  // 设置 slab 大小
```

内存范围一般包括 13 个几何分布的内存区，大小分别为 32、64、128、256、512、1024、2048、4096、8192、16384、32768、65536 和 131072 字节。 malloc_size 数组的元素指向 26 个高速缓存描述符，因为每个内存区都包含两个高速缓存，一个用于 ISA DMA 分配，另一个用于常规分配。

```c
struct cache_sizes malloc_sizes[] = {
#define CACHE(x) { .cs_size = (x) },
#include <linux/kmalloc_sizes.h>
	CACHE(ULONG_MAX)
#undef CACHE
};
EXPORT_SYMBOL(malloc_sizes);
```

初始化 `malloc_sizes` ：

```c
sizes[INDEX_AC].cs_cachep = kmem_cache_create(names[INDEX_AC].name,
					sizes[INDEX_AC].cs_size,
					ARCH_KMALLOC_MINALIGN,
					ARCH_KMALLOC_FLAGS|SLAB_PANIC,
					NULL);
```

## 创建一个缓存

创建缓存的入口为 `kmem_cache_create`

```c
struct kmem_cache *kmem_cache_create (const char *name, size_t size, size_t align,unsigned long flags, void (*ctor)(void *)) 
```

* 第一个参数是字符串，存放高速缓存的名字
* 第二个参数是高速缓存中每个元素的大小，
* 第三个参数是slab内第一个对象的偏移，用来确保在页内进行特定的对齐。通常0就可以满足，也就是标准对齐。
* flags参数是可选的设置项，用来控制高速缓存的行为。可以为0,表示没有特殊行为，或者与以下标志进行或运算，在slab.h中存在定义。
* 最后一个参数是函数指针，用来定义该缓存被分配时的初始化方式，类似于cpp中的构造函数。

在创建一个缓存时，首先会遍历 `cache_chain` 链表，查看是否有同名的缓存。在没有同名缓存的情况下再对各种对齐参数进行计算。

然后为缓存管理结构 kmem_cache 分配一块内存。

分配好 kmem_cache 后会使用 `calculate_slab_order` 寻找适合的 slab 页阶数

```c
static size_t calculate_slab_order(struct kmem_cache *cachep,
			size_t size, size_t align, unsigned long flags)
{
	unsigned long offslab_limit;  // 用于确定 off-slab 缓存的限制
	size_t left_over = 0;         // 剩余的碎片大小
	int gfporder;                 // 用于 slab 分配的页数的阶数

	// 循环遍历不同阶数的页分配，直到找到合适的 slab 配置
	for (gfporder = 0; gfporder <= KMALLOC_MAX_ORDER; gfporder++) {
		unsigned int num;   // 当前阶数下可分配的对象数量
		size_t remainder;   // 当前阶数下的剩余碎片空间

		// 估算当前阶数下 slab 中的对象数和剩余碎片
		cache_estimate(gfporder, size, align, flags, &remainder, &num);
		
		// 如果没有可分配的对象，继续尝试下一个阶数
		if (!num)
			continue;

		// 如果使用 off-slab 缓存方式，计算最大允许的对象数
		if (flags & CFLGS_OFF_SLAB) {
			/*
			 * 使用 off-slab slab 的缓存对象数上限。
			 * 避免在 cache_grow() 中出现无限循环的情况。
			 */
			offslab_limit = size - sizeof(struct slab);
			offslab_limit /= sizeof(kmem_bufctl_t);

			// 如果对象数超出 off-slab 限制，跳出循环
			if (num > offslab_limit)
				break;
		}

		// 找到符合要求的配置，记录可分配对象数和阶数
		cachep->num = num;
		cachep->gfporder = gfporder;
		left_over = remainder;  // 记录当前阶数下的剩余碎片

		/*
		 * 对于 VFS 可回收的 slab，大多数分配是使用 GFP_NOFS，
		 * 我们不希望在无法缩小 dcache 时分配更高阶的页。
		 */
		if (flags & SLAB_RECLAIM_ACCOUNT)
			break;

		/*
		 * 虽然对象数越多越好，但非常大的 slab 对 gfp() 调用
		 * 会带来不利影响，因此我们限制 gfporder。
		 */
		if (gfporder >= slab_break_gfp_order)
			break;

		/*
		 * 判断内部碎片是否可以接受。如果剩余碎片占的比例较小，
		 * 则认为该配置是合适的，终止循环。
		 */
		if (left_over * 8 <= (PAGE_SIZE << gfporder))
			break;
	}
	
	// 返回最后的剩余碎片大小
	return left_over;
}
```

在函数 cache_estimate 中会将一个

## 分配一个缓存

当 不足时会调用cache_grow，在该函数中会调用函数 kmem_getpages 向伙伴系统请求页面。