/* 二叉大顶堆: 插入/上浮/下沉/取顶, 用堆完成堆排序, 再验证堆性质。
   覆盖 while 上浮下沉、交换与循环内条件移动。 */
int heap[32];
int hsize;
void push(int v) {
    heap[hsize] = v;
    int i = hsize;
    hsize = hsize + 1;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (heap[p] >= heap[i]) break;
        int t = heap[p];
        heap[p] = heap[i];
        heap[i] = t;
        i = p;
    }
}
int pop() {
    int top = heap[0];
    hsize = hsize - 1;
    heap[0] = heap[hsize];
    int i = 0;
    while (1) {
        int l = i * 2 + 1;
        int r = i * 2 + 2;
        int big = i;
        if (l < hsize && heap[l] > heap[big]) big = l;
        if (r < hsize && heap[r] > heap[big]) big = r;
        if (big == i) break;
        int t = heap[i];
        heap[i] = heap[big];
        heap[big] = t;
        i = big;
    }
    return top;
}
int main(){
    hsize = 0;
    int data[10];
    data[0] = 5; data[1] = 13; data[2] = 2; data[3] = 25;
    data[4] = 7; data[5] = 17; data[6] = 20; data[7] = 8; data[8] = 4; data[9] = 11;
    for (int i = 0; i < 10; i = i + 1) push(data[i]);
    for (int i = 0; i < 10; i = i + 1) {
        putint(pop());
        putch(32);
    }
    putch(10);
    hsize = 0;
    push(42);
    push(7);
    push(-3);
    push(100);
    push(55);
    push(9);
    push(61);
    while (hsize > 0) {
        putint(pop());
        putch(32);
    }
    putch(10);
    return 0;
}
