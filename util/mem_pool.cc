#include "mem_pool.h"
#include "log_util.h"

void* MemPool::allocate(size_t req_size)
{
    if (!is_support_size(req_size)) {
        return ::operator new(req_size);
    }

    size_t idx = find_index(req_size);
    if(idx == CLASS_COUNT) {
        LOG_ERROR_("Fail to get pool index!, size:%lu\n", req_size);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(free_list_mtx_[idx]);
    BlockHeader*& head = free_list_[idx];
    if(head == nullptr) {
        expand_span(idx);
    }

    BlockHeader* hdr = head;
    head = head->next;

    uint8_t* user_ptr = reinterpret_cast<uint8_t*>(hdr) + sizeof(BlockHeader);
    return user_ptr;
}

void MemPool::deallocate(void* user_ptr)
{
    if(user_ptr == nullptr)
        return;

    if (!belong_to_pool(user_ptr)) {
        ::operator delete(user_ptr);
        return;
    }

    uint8_t* raw = static_cast<uint8_t*>(user_ptr);
    BlockHeader* hdr = reinterpret_cast<BlockHeader*>(raw - sizeof(BlockHeader));
    uint32_t idx = hdr->class_idx;
    if (idx >= CLASS_COUNT) {
        LOG_ERROR_("Invalid addr for deallocate!\n");
        return;
    }

    std::lock_guard<std::mutex> lock(free_list_mtx_[idx]);
    hdr->next = free_list_[idx];
    free_list_[idx] = hdr;
}

MemPool::MemPool()
{
    for(size_t i = 0; i < CLASS_COUNT; ++i) {
        free_list_[i] = nullptr;
        expand_span(i);
    }
    span_list_.resize(CLASS_COUNT);
}

MemPool::~MemPool()
{
    for(auto &span : span_list_) {
        ::operator delete(span.first);
    }
    span_list_.clear();
}

size_t MemPool::find_index(size_t size) const
{
    for(size_t i = 0; i < CLASS_COUNT; ++i) {
        if(BLOCKS[i].size >= size)
            return i;
    }
    return CLASS_COUNT;
}

bool MemPool::belong_to_pool(void *user_ptr)
{
    std::lock_guard<std::mutex> lock(span_list_mtx_);
    for (auto &span : span_list_) {
        if (user_ptr >= span.first && user_ptr < reinterpret_cast<uint8_t*>(span.first) + span.second) {
            return true;
        }
    }
    return false;
}

void MemPool::expand_span(uint32_t idx)
{
    MemSpan span = BLOCKS[idx];
    size_t full_block_size = sizeof(BlockHeader) + span.size;
    size_t span_size = full_block_size * span.nums;
    
    void* span_mem = ::operator new(span_size);
    BlockHeader* p = reinterpret_cast<BlockHeader*>(span_mem);
    {
        std::lock_guard<std::mutex> mtx(span_list_mtx_);
        span_list_.push_back({span_mem, span_size});
    }

    for(size_t i = 0; i < span.nums; i++) {
        uint8_t *addr = reinterpret_cast<uint8_t*>(p);
        p->class_idx = idx;
        p->next = free_list_[idx];
        free_list_[idx] = p;
        p = reinterpret_cast<BlockHeader*>(addr + full_block_size);
    }
}