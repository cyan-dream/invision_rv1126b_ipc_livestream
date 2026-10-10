#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H
#ifdef __cplusplus
extern "C" {
#endif
struct CircularBuffer{  
    unsigned char *buffer;  
    int head;  
    int tail;  
    int size;  
    int capacity;  
};

struct CircularBuffer* createBuffer(int capacity);
void clearBuffer(struct CircularBuffer *cb);
void destroyBuffer(struct CircularBuffer *cb);
int enqueue(struct CircularBuffer *cb,unsigned char *data, int len);
int dequeue(struct CircularBuffer *cb, unsigned char *out, int maxLen);
#ifdef __cplusplus
}
#endif
#endif 
