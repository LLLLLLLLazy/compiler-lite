/* Kruskal MST: 边表 + 选择排序 + 并查集(路径压缩 + 按秩合并)。
   覆盖结构数组模拟(平行数组)、递归 find 与联合操作。 */
int eu[12];
int ev[12];
int ew[12];
int parent[8];
int rankd[8];
int find(int x) {
    if (parent[x] != x) parent[x] = find(parent[x]);
    return parent[x];
}
void unite(int a, int b) {
    int ra = find(a);
    int rb = find(b);
    if (ra == rb) return;
    if (rankd[ra] < rankd[rb]) parent[ra] = rb;
    else {
        parent[rb] = ra;
        if (rankd[ra] == rankd[rb]) rankd[ra] = rankd[ra] + 1;
    }
}
int kruskal(int m, int n) {
    for (int i = 0; i < n; i = i + 1) {
        parent[i] = i;
        rankd[i] = 0;
    }
    for (int i = 0; i < m - 1; i = i + 1)
        for (int j = i + 1; j < m; j = j + 1) {
            if (ew[j] < ew[i]) {
                int tu = eu[i]; eu[i] = eu[j]; eu[j] = tu;
                int tv = ev[i]; ev[i] = ev[j]; ev[j] = tv;
                int tw = ew[i]; ew[i] = ew[j]; ew[j] = tw;
            }
        }
    int total = 0;
    for (int e = 0; e < m; e = e + 1) {
        if (find(eu[e]) != find(ev[e])) {
            unite(eu[e], ev[e]);
            total = total + ew[e];
        }
    }
    return total;
}
int main(){
    int m = 7;
    eu[0] = 0; ev[0] = 1; ew[0] = 2;
    eu[1] = 0; ev[1] = 2; ew[1] = 3;
    eu[2] = 1; ev[2] = 2; ew[2] = 1;
    eu[3] = 1; ev[3] = 3; ew[3] = 4;
    eu[4] = 2; ev[4] = 3; ew[4] = 5;
    eu[5] = 1; ev[5] = 4; ew[5] = 7;
    eu[6] = 3; ev[6] = 4; ew[6] = 6;
    putint(kruskal(m, 5)); putch(10);
    m = 6;
    eu[0] = 0; ev[0] = 1; ew[0] = 10;
    eu[1] = 0; ev[1] = 2; ew[1] = 6;
    eu[2] = 0; ev[2] = 3; ew[2] = 5;
    eu[3] = 1; ev[3] = 3; ew[3] = 15;
    eu[4] = 2; ev[4] = 3; ew[4] = 4;
    eu[5] = 1; ev[5] = 4; ew[5] = 8;
    putint(kruskal(m, 5)); putch(10);
    return 0;
}
