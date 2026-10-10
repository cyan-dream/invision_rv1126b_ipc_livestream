#include "circular_buffer.h"
#include <stdlib.h>
struct CircularBuffer* createBuffer(int capacity) 
{  
    struct CircularBuffer *cb = (struct CircularBuffer*)malloc(sizeof(struct CircularBuffer));  
    cb->buffer = (unsigned char*)malloc(capacity * sizeof(unsigned char));  
    cb->head = 0;  
    cb->tail = 0;  
    cb->size = 0;  
    cb->capacity = capacity;  
    return cb;  
}

void clearBuffer(struct CircularBuffer *cb) 
{  
    cb->head = 0;  
    cb->tail = 0;  
    cb->size = 0;  
    return;  
}

void destroyBuffer(struct CircularBuffer *cb) 
{  
    free(cb->buffer);  
    free(cb);  
}  

int enqueue(struct CircularBuffer *cb,unsigned char *data, int len) 
{  
    if (cb->size + len > cb->capacity) {  
        // 缓冲区溢出，这里选择丢弃新数据  
        return -1;  
    }  
    // 将数据复制到缓冲区  
    for (int i = 0; i < len; i++) {  
        cb->buffer[(cb->tail + i) % cb->capacity] = data[i];  
    }  
    // 更新尾部和大小  
    cb->tail = (cb->tail + len) % cb->capacity;  
    cb->size += len;  
    return len;  
} 

int dequeue(struct CircularBuffer *cb, unsigned char *out, int maxLen) 
{  
    if (cb->size == 0) {  
        return 0;  // 缓冲区为空  
    }  
    int len = (cb->size < maxLen) ? cb->size : maxLen;  
    for (int i = 0; i < len; i++) {  
        out[i] = cb->buffer[cb->head];  
        cb->head = (cb->head + 1) % cb->capacity;  
    }  
    cb->size -= len;  
    return len;  
}