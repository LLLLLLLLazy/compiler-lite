/* Prim MST: 邻接矩阵 + 最小未访问键值选择, 输出 MST 总权与父节点序列。
   与 Dijkstra 结构相似但松弛语义不同, 校验两者可区分。 */
int adj[6][6];
int key[6];
int inMST[6];
int par[6];
int prim(int n) {
    for (int i = 0; i < n; i = i + 1) {
        key[i] = 100000;
        inMST[i] = 0;
        par[i] = -1;
    }
    key[0] = 0;
    int total = 0;
    for (int k = 0; k < n; k = k + 1) {
        int u = -1;
        int best = 100000;
        for (int i = 0; i < n; i = i + 1) {
            if (inMST[i] == 0 && key[i] < best) {
                best = key[i];
                u = i;
            }
        }
        if (u < 0) break;
        inMST[u] = 1;
        total = total + best;
        for (int v = 0; v < n; v = v + 1) {
            if (adj[u][v] > 0 && inMST[v] == 0) {
                if (adj[u][v] < key[v]) {
                    key[v] = adj[u][v];
                    par[v] = u;
                }
            }
        }
    }
    return total;
}
int main(){
    for (int i = 0; i < 6; i = i + 1)
        for (int j = 0; j < 6; j = j + 1)
            adj[i][j] = 0;
    adj[0][1] = 2; adj[1][0] = 2;
    adj[0][3] = 6; adj[3][0] = 6;
    adj[1][2] = 3; adj[2][1] = 3;
    adj[1][3] = 8; adj[3][1] = 8;
    adj[1][4] = 5; adj[4][1] = 5;
    adj[2][4] = 7; adj[4][2] = 7;
    adj[3][4] = 9; adj[4][3] = 9;
    putint(prim(5)); putch(10);
    for (int i = 1; i < 5; i = i + 1) {
        putint(par[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
