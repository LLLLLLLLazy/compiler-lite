/* 循环队列: % 容量回绕, 空满判定, 交错入队出队。
   模运算与环形索引边界(空满区分), 覆盖 RangeModSimplify 类模式。 */
int cq[8];
int head;
int tail;
int size;
int enqueue(int v) {
    if (size == 8) return 0;
    cq[tail] = v;
    tail = (tail + 1) % 8;
    size = size + 1;
    return 1;
}
int dequeue() {
    if (size == 0) return -999;
    int v = cq[head];
    head = (head + 1) % 8;
    size = size - 1;
    return v;
}
int main(){
    head = 0;
    tail = 0;
    size = 0;
    for (int i = 1; i <= 5; i = i + 1) {
        putint(enqueue(i * 10));
        putch(32);
    }
    putch(10);
    putint(dequeue()); putch(32);
    putint(dequeue()); putch(10);
    for (int i = 6; i <= 10; i = i + 1) {
        putint(enqueue(i * 10));
        putch(32);
    }
    putch(10);
    putint(enqueue(111)); putch(10);
    while (size > 0) {
        putint(dequeue());
        putch(32);
    }
    putch(10);
    putint(dequeue()); putch(10);
    return 0;
}
