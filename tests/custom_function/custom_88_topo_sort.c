int edge[4][4];
int indeg[4];
int order[4];
int main(){
    edge[0][1] = 1;
    edge[0][2] = 1;
    edge[1][3] = 1;
    edge[2][3] = 1;
    for (int i = 0; i < 4; i = i + 1)
        for (int j = 0; j < 4; j = j + 1)
            if (edge[i][j]) indeg[j] = indeg[j] + 1;
    int cnt = 0;
    while (cnt < 4) {
        for (int v = 0; v < 4; v = v + 1) {
            if (indeg[v] == 0) {
                indeg[v] = -1;
                order[cnt] = v;
                cnt = cnt + 1;
                for (int u = 0; u < 4; u = u + 1)
                    if (edge[v][u]) indeg[u] = indeg[u] - 1;
                break;
            }
        }
    }
    for (int i = 0; i < 4; i = i + 1) {
        putint(order[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
