int maze[5][5];
int dist[5][5];
int qx[30];
int qy[30];
int main(){
    maze[0][0] = 1; maze[0][1] = 1; maze[0][2] = 1; maze[0][3] = 1; maze[0][4] = 1;
    maze[1][0] = 1; maze[1][1] = 0; maze[1][2] = 0; maze[1][3] = 0; maze[1][4] = 1;
    maze[2][0] = 1; maze[2][1] = 1; maze[2][2] = 1; maze[2][3] = 0; maze[2][4] = 1;
    maze[3][0] = 1; maze[3][1] = 0; maze[3][2] = 0; maze[3][3] = 0; maze[3][4] = 1;
    maze[4][0] = 1; maze[4][1] = 1; maze[4][2] = 1; maze[4][3] = 1; maze[4][4] = 1;
    for (int i = 0; i < 5; i = i + 1)
        for (int j = 0; j < 5; j = j + 1) dist[i][j] = -1;
    int head = 0;
    int tail = 0;
    qx[tail] = 0;
    qy[tail] = 0;
    tail = tail + 1;
    dist[0][0] = 0;
    while (head < tail) {
        int x = qx[head];
        int y = qy[head];
        head = head + 1;
        if (x == 4 && y == 4) break;
        if (x > 0 && maze[x - 1][y] && dist[x - 1][y] == -1) {
            dist[x - 1][y] = dist[x][y] + 1;
            qx[tail] = x - 1;
            qy[tail] = y;
            tail = tail + 1;
        }
        if (x < 4 && maze[x + 1][y] && dist[x + 1][y] == -1) {
            dist[x + 1][y] = dist[x][y] + 1;
            qx[tail] = x + 1;
            qy[tail] = y;
            tail = tail + 1;
        }
        if (y > 0 && maze[x][y - 1] && dist[x][y - 1] == -1) {
            dist[x][y - 1] = dist[x][y] + 1;
            qx[tail] = x;
            qy[tail] = y - 1;
            tail = tail + 1;
        }
        if (y < 4 && maze[x][y + 1] && dist[x][y + 1] == -1) {
            dist[x][y + 1] = dist[x][y] + 1;
            qx[tail] = x;
            qy[tail] = y + 1;
            tail = tail + 1;
        }
    }
    putint(dist[4][4]); putch(10);
    return 0;
}
