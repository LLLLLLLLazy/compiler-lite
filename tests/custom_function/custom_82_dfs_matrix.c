int adj[5][5];
int visited[5];
void dfs(int u) {
    putint(u);
    putch(32);
    visited[u] = 1;
    for (int v = 0; v < 5; v = v + 1) {
        if (adj[u][v] && visited[v] == 0) dfs(v);
    }
}
int main(){
    adj[0][1] = 1; adj[0][2] = 1;
    adj[1][3] = 1;
    adj[2][3] = 1;
    adj[3][4] = 1;
    dfs(0);
    putch(10);
    return 0;
}
