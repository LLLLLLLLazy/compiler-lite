/* 拓扑排序 (Kahn 算法): 入度数组 + 数组模拟队列, 输出两种图的拓扑序。
   覆盖 % 环形队列索引、入度递减与队列头尾指针移动。 */
int gadj[8][8];
int outdeg[8];
int indeg[8];
int queue[16];
int topo(int n) {
    for (int i = 0; i < n; i = i + 1) indeg[i] = 0;
    for (int u = 0; u < n; u = u + 1)
        for (int e = 0; e < outdeg[u]; e = e + 1)
            indeg[gadj[u][e]] = indeg[gadj[u][e]] + 1;
    int head = 0;
    int tail = 0;
    for (int i = 0; i < n; i = i + 1) {
        if (indeg[i] == 0) {
            queue[tail] = i;
            tail = tail + 1;
        }
    }
    int cnt = 0;
    while (head < tail) {
        int u = queue[head];
        head = head + 1;
        putint(u);
        putch(32);
        cnt = cnt + 1;
        for (int e = 0; e < outdeg[u]; e = e + 1) {
            int v = gadj[u][e];
            indeg[v] = indeg[v] - 1;
            if (indeg[v] == 0) {
                queue[tail] = v;
                tail = tail + 1;
            }
        }
    }
    putch(10);
    return cnt;
}
int main(){
    int n = 6;
    for (int i = 0; i < n; i = i + 1) outdeg[i] = 0;
    gadj[5][0] = 0; gadj[5][1] = 2; outdeg[5] = 2;
    gadj[4][0] = 0; gadj[4][1] = 1; outdeg[4] = 2;
    gadj[2][1] = 3; outdeg[2] = 1;
    gadj[3][0] = 1; outdeg[3] = 1;
    putint(topo(n)); putch(10);
    n = 4;
    for (int i = 0; i < n; i = i + 1) outdeg[i] = 0;
    gadj[0][0] = 1; gadj[0][1] = 2; outdeg[0] = 2;
    gadj[1][0] = 3; outdeg[1] = 1;
    gadj[2][0] = 3; outdeg[2] = 1;
    putint(topo(n)); putch(10);
    return 0;
}
