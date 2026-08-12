int adj[6][6];
int visited[6];
void dfsMark(int u) {
    visited[u] = 1;
    for (int v = 0; v < 6; v = v + 1) {
        if (adj[u][v] && visited[v] == 0) dfsMark(v);
    }
}
int main(){
    adj[0][1] = 1; adj[1][0] = 1;
    adj[2][3] = 1; adj[3][2] = 1;
    adj[4][5] = 1; adj[5][4] = 1;
    int comps = 0;
    for (int i = 0; i < 6; i = i + 1) {
        if (visited[i] == 0) {
            comps = comps + 1;
            dfsMark(i);
        }
    }
    putint(comps); putch(10);
    return 0;
}
