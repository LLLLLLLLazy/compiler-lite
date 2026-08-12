/* Bellman-Ford: 含负权边的最短路与负环检测。
   嵌套松弛循环 + 提前退出标记 + 负环判断输出。 */
int eu[10];
int ev[10];
int ew[10];
int m;
int d[6];
int bf(int src, int n) {
    for (int i = 0; i < n; i = i + 1) d[i] = 100000;
    d[src] = 0;
    for (int k = 0; k < n - 1; k = k + 1) {
        int changed = 0;
        for (int e = 0; e < m; e = e + 1) {
            if (d[eu[e]] < 100000) {
                int nd = d[eu[e]] + ew[e];
                if (nd < d[ev[e]]) {
                    d[ev[e]] = nd;
                    changed = 1;
                }
            }
        }
        if (changed == 0) return 0;
    }
    for (int e = 0; e < m; e = e + 1) {
        if (d[eu[e]] < 100000) {
            if (d[eu[e]] + ew[e] < d[ev[e]]) return 1;
        }
    }
    return 0;
}
int main(){
    m = 7;
    eu[0] = 0; ev[0] = 1; ew[0] = 6;
    eu[1] = 0; ev[1] = 2; ew[1] = 7;
    eu[2] = 1; ev[2] = 2; ew[2] = 8;
    eu[3] = 1; ev[3] = 3; ew[3] = 5;
    eu[4] = 1; ev[4] = 4; ew[4] = -4;
    eu[5] = 2; ev[5] = 3; ew[5] = -3;
    eu[6] = 2; ev[6] = 4; ew[6] = 9;
    int cycle = bf(0, 5);
    putint(cycle); putch(10);
    for (int i = 0; i < 5; i = i + 1) {
        putint(d[i]);
        putch(32);
    }
    putch(10);
    m = 3;
    eu[0] = 0; ev[0] = 1; ew[0] = 1;
    eu[1] = 1; ev[1] = 2; ew[1] = -3;
    eu[2] = 2; ev[2] = 0; ew[2] = 1;
    cycle = bf(0, 3);
    putint(cycle); putch(10);
    for (int i = 0; i < 3; i = i + 1) {
        putint(d[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
