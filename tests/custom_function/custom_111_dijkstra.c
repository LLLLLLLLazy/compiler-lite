/* Dijkstra 稠密 O(n²): 固定带权图(含不可达点), 松弛 + 未访问最小值选择,
   输出源点到所有点的距离。压测循环内条件更新与多数组活跃性。 */
int n;
int graph[8][8];
int dist[8];
int done[8];
int INF;
void dijkstra(int src) {
    for (int i = 0; i < n; i = i + 1) {
        dist[i] = INF;
        done[i] = 0;
    }
    dist[src] = 0;
    for (int k = 0; k < n; k = k + 1) {
        int u = -1;
        int best = INF;
        for (int i = 0; i < n; i = i + 1) {
            if (done[i] == 0 && dist[i] < best) {
                best = dist[i];
                u = i;
            }
        }
        if (u < 0) return;
        done[u] = 1;
        for (int v = 0; v < n; v = v + 1) {
            if (graph[u][v] > 0) {
                int nd = dist[u] + graph[u][v];
                if (nd < dist[v]) dist[v] = nd;
            }
        }
    }
}
int main(){
    n = 6;
    INF = 1000000;
    for (int i = 0; i < 8; i = i + 1)
        for (int j = 0; j < 8; j = j + 1)
            graph[i][j] = 0;
    graph[0][1] = 7; graph[0][2] = 9; graph[0][5] = 14;
    graph[1][2] = 10; graph[1][3] = 15;
    graph[2][3] = 11; graph[2][5] = 2;
    graph[3][4] = 6;
    graph[5][4] = 9;
    dijkstra(0);
    for (int i = 0; i < n; i = i + 1) {
        putint(dist[i]);
        putch(32);
    }
    putch(10);
    n = 4;
    INF = 999;
    for (int i = 0; i < 8; i = i + 1)
        for (int j = 0; j < 8; j = j + 1)
            graph[i][j] = 0;
    graph[0][1] = 4; graph[0][2] = 1;
    graph[2][1] = 2; graph[1][3] = 1; graph[2][3] = 5;
    dijkstra(0);
    for (int i = 0; i < n; i = i + 1) {
        putint(dist[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
