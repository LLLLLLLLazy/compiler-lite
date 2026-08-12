int dist[4][4];
int main(){
    for (int i = 0; i < 4; i = i + 1)
        for (int j = 0; j < 4; j = j + 1) {
            if (i == j) dist[i][j] = 0;
            else dist[i][j] = 1000;
        }
    dist[0][1] = 2;
    dist[1][2] = 3;
    dist[0][2] = 7;
    dist[2][3] = 1;
    dist[1][3] = 10;
    for (int k = 0; k < 4; k = k + 1)
        for (int i = 0; i < 4; i = i + 1)
            for (int j = 0; j < 4; j = j + 1) {
                int via = dist[i][k] + dist[k][j];
                if (via < dist[i][j]) dist[i][j] = via;
            }
    putint(dist[0][3]); putch(10);
    putint(dist[0][2]); putch(10);
    putint(dist[1][3]); putch(10);
    return 0;
}
