int grid[8][8];
int next[8][8];
int countNeighbors(int x, int y) {
    int cnt = 0;
    for (int dx = -1; dx <= 1; dx = dx + 1)
        for (int dy = -1; dy <= 1; dy = dy + 1) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8)
                if (grid[nx][ny]) cnt = cnt + 1;
        }
    return cnt;
}
int main(){
    grid[2][3] = 1; grid[2][4] = 1; grid[3][3] = 1;
    for (int gen = 0; gen < 3; gen = gen + 1) {
        int alive = 0;
        for (int i = 0; i < 8; i = i + 1) {
            for (int j = 0; j < 8; j = j + 1) {
                int nb = countNeighbors(i, j);
                if (grid[i][j]) {
                    if (nb == 2 || nb == 3) next[i][j] = 1;
                    else next[i][j] = 0;
                } else {
                    if (nb == 3) next[i][j] = 1;
                    else next[i][j] = 0;
                }
                if (next[i][j]) alive = alive + 1;
            }
        }
        putint(alive);
        putch(32);
        for (int i = 0; i < 8; i = i + 1)
            for (int j = 0; j < 8; j = j + 1)
                grid[i][j] = next[i][j];
    }
    putch(10);
    return 0;
}
