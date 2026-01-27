#include <cstddef>
#include <unistd.h>
#include <cstring>

struct MallocMetadata
{
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;

};

MallocMetadata* head = nullptr;

void* smalloc(size_t size){
    if (size == 0){
        return NULL;
    }
    if (size >= 100000000){
        return NULL;
    }
    MallocMetadata* current = head;
    MallocMetadata* last = nullptr;
    while (current != nullptr)
    {
        if (current->size >= size && current->is_free){
            current->is_free = false;
            return current + 1;
        }
        last = current;
        current = current->next;
    }
    void* p = sbrk(sizeof(MallocMetadata)+size);
    if (p == (void*)(-1)){
        return NULL;
    }
    MallocMetadata* metadata = (MallocMetadata*)p;
    metadata->is_free = false;
    metadata->size = size;
    metadata->next = nullptr;
    if (head == nullptr){
        metadata->prev = nullptr;
        head = metadata;
    }else{
        metadata->prev = last;
        last->next = metadata;
    }
    return metadata + 1;
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
    if (p != nullptr){
       ((MallocMetadata*)(p)-1)->is_free = true;
    }
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
    if (((MallocMetadata*)(oldp) -1)->size >= size){
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

size_t _num_free_blocks(){
    MallocMetadata* current = head;
    size_t size = 0;
    while (current != nullptr)
    {
        if (current->is_free){
            size ++;
        }
        current = current->next;
    }
    return size;
}

size_t _num_free_bytes(){
    MallocMetadata* current = head;
    size_t size = 0;
    while (current != nullptr)
    {
        if (current->is_free){
            size += current->size;
        }
        current = current->next;
    }
    return size;
}

size_t _num_allocated_blocks(){
    MallocMetadata* current = head;
    size_t size = 0;
    while (current != nullptr)
    {
        size ++;
        current = current->next;
    }
    return size;
}
 
size_t _num_allocated_bytes(){
    MallocMetadata* current = head;
    size_t size = 0;
    while (current != nullptr)
    {
        size += current->size;
        current = current->next;
    }
    return size;
}
 
size_t _num_meta_data_bytes(){
    return _num_allocated_blocks() * sizeof(MallocMetadata);
}
 
size_t _size_meta_data(){
    return sizeof(MallocMetadata);
}