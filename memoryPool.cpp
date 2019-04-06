//内存池类似哈希的分桶，很多bucket，每个bucket是个单链表（而不是一个节点，但是看图不同bucket对应的链表可能有重合
template <bool threads, int inst>
class __default_alloc_template
{
private:
    //将bytes上调至8的倍数
    static size_t ROUND_UP(size_t bytes)
    {
        return (((bytes) + __ALIGN - 1) & ~(__ALIGN - 1));//等价于(bytes + 7) / 8
    }
    //空闲链表的节点构造
    union obj
    {
        union obj * free_list_link;
        char client_data[1];
    };
private:
    //16个空闲链表，初始化为0,即每个链表中都没有空闲数据块
    static obj * volatile free_list[__NFREELISTS];
    //根据申请数据块大小找到相应空闲链表的下标
    static  size_t FREELIST_INDEX(size_t bytes)
    {
        return (((bytes) + __ALIGN - 1)/__ALIGN - 1);
    }
    ......
}

//申请大小为n的数据块，返回该数据块的起始地址
static void * allocate(size_t n)
{
    obj * __VOLATILE * my_free_list;
    obj * __RESTRICT result;
 
    if (n > (size_t) __MAX_BYTES)//大于128字节调用第一级配置器
    {
        return(malloc_alloc::allocate(n));
    }
    my_free_list = free_list + FREELIST_INDEX(n);//根据申请空间的大小寻找相应的空闲链表（16个空闲链表中的一个）
    //即使数据变了，链表的地址也不会改变，变的是链表的内容
    result = *my_free_list;//result是node类型（有数据的话就是一个指针类型，否则是char类型
    if (result == 0)//如果该空闲链表没有空闲的数据块
    {
        void *r = refill(ROUND_UP(n));//为该空闲链表填充新的空间
        return r;
    }
    //改变的是链表节点的内容，不是地址，地址不会变（第一个空闲的数据块）
    *my_free_list = result -> free_list_link;//如果空闲链表中有空闲数据块，则取出一个，并把空闲链表的指针指向下一个数据块
    return (result);
};
//最难理解的是从内存池中去空间的过程

template <bool threads, int inst>
class __default_alloc_template
{
private:
    ......
    static char *start_free;//内存池可用空间的起始位置，初始化为0
    static char *end_free;//内存池可用空间的结束位置,初始化为0
    static size_t heap_size;//内存池的总大小
 
public:
    //申请nobjs个大小为size的数据块，返回值为真实申请到的数据块个数，放在nobjs中
    static char *chunk_alloc(size_t size, int &nobjs)
    {
        char * result;
        size_t total_bytes = size * nobjs;//需要申请空间的大小
        size_t bytes_left = end_free - start_free;//计算内存池剩余空间
 
        //如果内存池剩余空间完全满足需求量
        if (bytes_left >= total_bytes)
        {
            result = start_free;
            start_free += total_bytes;
            return(result);
        }
        //内存池剩余空间不满足需求量，但是至少能够提供一个以上数据块
        else if (bytes_left >= size)
        {
            nobjs = bytes_left / size;
            total_bytes = size * nobjs;
            result = start_free;
            start_free += total_bytes;
            return(result);
        }
        //剩余空间连一个数据块（大小为size）也无法提供
        //虽然内存池里面剩的少，但是可以从堆里面去取区
        else
        {
            size_t bytes_to_get = 2 * total_bytes + ROUND_UP(heap_size >> 4);
 
            //内存池的剩余空间分给合适的空闲链表
            if (bytes_left > 0)
            {
                obj * __VOLATILE * my_free_list = free_list + FREELIST_INDEX(bytes_left);
 
                ((obj *)start_free) -> free_list_link = *my_free_list;
                *my_free_list = (obj *)start_free;
            }//相当于头插法，把start_free插入到链表头部
            start_free = (char *)malloc(bytes_to_get);//配置heap空间，用来补充内存池
            if (0 == start_free)
            {
                int i;
                obj * __VOLATILE * my_free_list, *p;
 
                //从空闲链表中找出一个比较大的空闲数据块还给内存池（之后会将这个大的空闲数据块切成多个小的空闲数据块再次加入到空闲链表）
                for (i = size; i <= __MAX_BYTES; i += __ALIGN)
                {
                    my_free_list = free_list + FREELIST_INDEX(i);
                    p = *my_free_list;
                    if (0 != p)
                    {
                        *my_free_list = p -> free_list_link;
                        start_free = (char *)p;
                        end_free = start_free + i;//下面的递归也是为了70和77行能退出循环
                        return(chunk_alloc(size, nobjs));//递归调用自己，为了修正nobjs
                    }
                }
                end_free = 0;//99行已经malloc失败了为什么这里还要再次调用？这两个函数不一样，下面是重载的new第一种，分配失败会抛异常
                start_free = (char *)malloc_alloc::allocate(bytes_to_get);//如果连这个大的数据块都找不出来则调用第一级配置器
            }
            //如果分配成功
            heap_size += bytes_to_get;//内存池大小增加
            end_free = start_free + bytes_to_get;//修改内存池可用空间的结束位置
            return(chunk_alloc(size, nobjs));//递归调用自己，为了修正nobjs
            //这里采用递归是为了70或77行的退出条件，相当于dfs
        }
    }
};
--------------------- 
原文：https://blog.csdn.net/a987073381/article/details/52245795 
