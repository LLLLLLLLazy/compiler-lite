int ways[5][5];
int main(){
    for (int i = 0; i < 5; i = i + 1) ways[i][0] = 1;
    for (int j = 0; j < 5; j = j + 1) ways[0][j] = 1;
    for (int i = 1; i < 5; i = i + 1)
        for (int j = 1; j < 5; j = j + 1)
            ways[i][j] = ways[i - 1][j] + ways[i][j - 1];
    putint(ways[4][4]); putch(10);
    putint(ways[2][3]); putch(10);
    return 0;
}
