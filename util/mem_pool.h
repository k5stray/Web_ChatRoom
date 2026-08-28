#ifndef MEM_POOL_H
#define MEM_POOL_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <mutex>

struct MemSpan
{
	uint32_t size;
	uint16_t nums;
};

static constexpr MemSpan BLOCKS[] = {
	{8, 64},
    {16, 64},
    {32, 64},
    {64, 64},
    {128, 32},
    {256, 32},
    {512, 32},
    {1024, 16},
    {2048, 16},
    {4096, 8},
    {8192, 8},
    {16384, 8},
    {32768, 8},  // 32k
    {65536, 8},  // 64k
    {131072, 4}, // 128k
};

class MemPool {
public:
    static constexpr size_t CLASS_COUNT = sizeof(BLOCKS) / sizeof(BLOCKS[0]);

    struct BlockHeader {
        uint32_t class_idx;
        struct BlockHeader* next;
    };

    static MemPool& get_instance() {
        static MemPool mem_pool;
        return mem_pool;
    }

    void* allocate(size_t req_size);
    void deallocate(void* user_ptr);

private:

    MemPool();
    ~MemPool();
    MemPool(const MemPool&) = delete;
    MemPool& operator=(const MemPool&) = delete;

    size_t find_index(size_t size) const;

    inline bool is_support_size(size_t size) {
        return (size <= BLOCKS[CLASS_COUNT - 1].size);
    }

    bool belong_to_pool(void *addr);
    void expand_span(uint32_t idx);

private:
    std::mutex free_list_mtx_[CLASS_COUNT]{};
    BlockHeader* free_list_[CLASS_COUNT]{};
    std::mutex span_list_mtx_;
    std::vector<std::pair<void*, uint32_t>> span_list_;
};

#define memalloc(size) MemPool::get_instance().allocate(size)
#define memfree(addr)  MemPool::get_instance().deallocate(addr)

#endif