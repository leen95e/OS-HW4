#include <cstddef>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>

#define MAX_ORDER 11
#define MAX_BLOCK_SIZE (128*1024)
#define MIN_BLOCK_SIZE 128

struct MallocMetadata
{
    size_t size;
    bool is_free;
    bool is_mmapped;
    int order;
    MallocMetadata* next;
    MallocMetadata* prev;
};

size_t count = 0;
size_t bytes = 0;
bool start = false;
MallocMetadata* free_blocks_array[MAX_ORDER] = {nullptr};
MallocMetadata* mmap_blocks_list = nullptr;

void* heap_base_addr = nullptr;


bool initalize_heap(){
        uintptr_t base = (uintptr_t)sbrk(0);
        size_t mem_size = (32 * MAX_BLOCK_SIZE);
        size_t offset = (mem_size - (base % mem_size)) % mem_size;

        void* p = sbrk(offset + mem_size);
        if (p == (void*)(-1)){
            return 0;
        }
        heap_base_addr = p;

        MallocMetadata* last = nullptr;
        for (size_t i = 0; i < 32; i++)
        {
            MallocMetadata* metadata = (MallocMetadata*)((uintptr_t)p + (i * MAX_BLOCK_SIZE));
            metadata->size = MAX_BLOCK_SIZE;
            metadata->is_free = true;
            metadata->is_mmapped = false;
            metadata->order = MAX_ORDER-1;
            metadata->next = nullptr;
            metadata->prev = last;
            if (last == nullptr) {
                free_blocks_array[MAX_ORDER-1] = metadata;
            } else {
                last->next = metadata;
            }
            last = metadata;
        }
        start = true;
        count = 32;
        bytes = 32 * (MAX_BLOCK_SIZE - sizeof(MallocMetadata));
        return 1;
}

int get_right_order(size_t size) {
    int order = 0;
    size_t block_size = MIN_BLOCK_SIZE;
    while (block_size < size + sizeof(MallocMetadata)) {
        block_size *= 2;
        order++;
    }
    return order;
}

void remove_from_free_blocks_array(MallocMetadata* block_to_remove) {
    int order = block_to_remove->order;
    if (block_to_remove->prev) {
        block_to_remove->prev->next = block_to_remove->next;
    } else {
        free_blocks_array[order] = block_to_remove->next;
    }
    if (block_to_remove->next) {
        block_to_remove->next->prev = block_to_remove->prev;
    }
    block_to_remove->next = nullptr;
    block_to_remove->prev = nullptr;
}


void insert_to_free_blocks_array(MallocMetadata* block) {
    int order = block->order;
    MallocMetadata* curr = free_blocks_array[order];
    if (!curr || block < curr) {
        block->next = curr;
        block->prev = nullptr;
        if (curr){
            curr->prev = block;
        }
        free_blocks_array[order] = block;
        return;
    }
    while (curr->next && block > curr->next) {
        curr = curr->next;
    }
    block->next = curr->next;
    block->prev = curr;
    if (curr->next){
        curr->next->prev = block;
    }
    curr->next = block;
}


void* smalloc(size_t size){
//intializing - first time
    if (start == false){
        if (!initalize_heap()){
            return nullptr;
        }
    }
    if (size == 0){
        return NULL;
    }
    if (size >= 100000000){
        return NULL;
    }

    if (sizeof(MallocMetadata) + size > MAX_BLOCK_SIZE){
        void* p = mmap(nullptr, size + sizeof(MallocMetadata), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED){
            return NULL;
        }
        MallocMetadata* metadata = (MallocMetadata*)p;
        metadata->size = size + sizeof(MallocMetadata);
        metadata->is_free = false;
        metadata->is_mmapped = true;
        metadata->next = mmap_blocks_list;
        metadata->prev = nullptr;
        count++;
        if (mmap_blocks_list){
            mmap_blocks_list->prev = metadata;
        }
        mmap_blocks_list = metadata;
        bytes += size;
        return metadata + 1;
    }
    int order = get_right_order(size);
    int current_order = order;

    while (current_order < MAX_ORDER && free_blocks_array[current_order] == nullptr){
        current_order++;
    }

    if (current_order == MAX_ORDER){
        return nullptr;
    }

    MallocMetadata* block_to_remove = free_blocks_array[current_order];
    remove_from_free_blocks_array(block_to_remove);
    block_to_remove->is_free = false;

    while (current_order > order){
        current_order--;
        block_to_remove->order = current_order;
        block_to_remove->size = block_to_remove->size/2;

        MallocMetadata* buddy = (MallocMetadata*)((uintptr_t)block_to_remove + block_to_remove->size);
        buddy->size = block_to_remove->size;
        buddy->order = current_order;
        buddy->is_free = true;
        buddy->is_mmapped = false;
        insert_to_free_blocks_array(buddy);
        count++;
    }
    block_to_remove->next = nullptr;
    block_to_remove->prev = nullptr;
    return block_to_remove + 1;
}


void* scalloc(size_t num, size_t size){
    void* p = smalloc(size*num);
    if (p == nullptr){
        return nullptr;
    }
    std::memset(p, 0, size * num);
    return p;
}

void sfree(void* p){
    if (p == nullptr){
        return;
    }
    MallocMetadata* metadata = (MallocMetadata*)p - 1;
    if(metadata->is_free){
        return;
    }

    if (metadata->is_mmapped) {
        if (metadata->prev == nullptr){
            mmap_blocks_list = metadata->next;
        }
        else {
            metadata->prev->next = metadata->next;
        }
        if (metadata->next != nullptr){
            metadata->next->prev = metadata->prev;
        }
        bytes -= (metadata->size - sizeof(MallocMetadata));
        count--;
        munmap(metadata, metadata->size);
        return;
    }

    metadata->is_free = true;

    while(metadata->order < MAX_ORDER - 1){
        uintptr_t buddy_addr = (uintptr_t)metadata ^ metadata->size;
        MallocMetadata* buddy = (MallocMetadata*)buddy_addr;
        if (buddy->is_free == false || buddy->order != metadata->order) {
            break;
        }
        remove_from_free_blocks_array(buddy);
        count--;
        if ((uintptr_t)buddy < (uintptr_t)metadata) {
            metadata = buddy;
        }
        metadata->order++;
        metadata->size *= 2;
    }
    insert_to_free_blocks_array(metadata);
}

void* srealloc(void* oldp, size_t size){
    if (size == 0){
        return NULL;
    }
    if (size >= 100000000){
        return NULL;
    }
    if (oldp == NULL){
        return smalloc(size);
    }

    MallocMetadata* metadata = (MallocMetadata*)(oldp)-1;

    if(metadata->is_mmapped){
        if(size + sizeof(MallocMetadata) == metadata->size){
            return oldp;
        }
        void* newp = smalloc(size);
        if (newp == nullptr){
            return nullptr;
        }
        std::memmove(newp, oldp, ((MallocMetadata*)(oldp)-1)->size);
        sfree(oldp);
        return newp;
    }

    if(size + sizeof(MallocMetadata) <= metadata->size){
        return oldp;
    }

    bool is_possible = false;
    MallocMetadata* current = metadata;
    size_t current_size = current->size;

    while(current_size < size + sizeof(MallocMetadata)){
        uintptr_t buddy_addr = (uintptr_t)current ^ current->size;
        MallocMetadata* buddy = (MallocMetadata*)buddy_addr;
        if (buddy->is_free == false || buddy->size != current_size) {
            is_possible = false;
            break;
        }
        if(buddy < current){
            current = buddy;
        }
        current_size *= 2;
        if(current_size >= size + sizeof(MallocMetadata)){
            is_possible = true;
            break;
        }
    }

    if(is_possible){
        while (metadata->size < size + sizeof(MallocMetadata)) {
            uintptr_t buddy_addr = (uintptr_t)metadata ^ metadata->size;
            MallocMetadata* buddy = (MallocMetadata*)buddy_addr;

            remove_from_free_blocks_array(buddy);
            buddy->is_free = false;
            count--;

            if ((uintptr_t)buddy < (uintptr_t)metadata) {
                //left Merge
                size_t old_payload_size = metadata->size - sizeof(MallocMetadata);
                std::memmove((void*)(buddy + 1), (void*)(metadata + 1), old_payload_size);

                buddy->size = metadata->size * 2;
                buddy->order = metadata->order + 1;
                buddy->is_mmapped = false;
                metadata = buddy;
            } else {
                //right Merge
                metadata->size *= 2;
                metadata->order += 1;
            }
        }
        return (void*)(metadata + 1);
    }

    void* newp = smalloc(size);
    if (newp == nullptr){
        return nullptr;
    }
    std::memmove(newp, oldp, metadata->size - sizeof(MallocMetadata));
    sfree(oldp);
    return newp;
}

size_t _num_free_blocks(){
    size_t size = 0;
    for (int i = 0; i < MAX_ORDER; i++)
    {
        MallocMetadata* current = free_blocks_array[i];
        while (current != nullptr)
        {
            if (current->is_free){
            size ++;
            }
        current = current->next;
        }
    }
    return size;
}

size_t _num_free_bytes(){
    size_t size = 0;
    for (int i = 0; i < MAX_ORDER; i++)
    {
        MallocMetadata* current = free_blocks_array[i];
        while (current != nullptr)
        {
            if (current->is_free){
            size = size + (current->size - sizeof(MallocMetadata));
            }
        current = current->next;
        }
    }
    return size;
}

size_t _num_allocated_blocks(){
    return count;
}

size_t _num_allocated_bytes(){
    return bytes;
}

size_t _num_meta_data_bytes(){
    return _num_allocated_blocks() * sizeof(MallocMetadata);}

size_t _size_meta_data(){
    return sizeof(MallocMetadata);
}