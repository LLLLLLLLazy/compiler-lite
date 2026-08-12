int adj[5][5];
int queue[10];
int visited[5];
int main(){
    adj[0][1] = 1; adj[0][2] = 1;
    adj[1][3] = 1;
    adj[2][3] = 1;
    adj[3][4] = 1;
    int head = 0;
    int tail = 0;
    queue[tail] = 0;
    tail = tail + 1;
    visited[0] = 1;
    while (head < tail) {
        int u = queue[head];
        head = head + 1;
        putint(u);
        putch(32);
        for (int v = 0; v < 5; v = v + 1) {
            if (adj[u][v] && visited[v] == 0) {
                visited[v] = 1;
                queue[tail] = v;
                tail = tail + 1;
            }
        }
    }
    putch(10);
    return 0;
}
