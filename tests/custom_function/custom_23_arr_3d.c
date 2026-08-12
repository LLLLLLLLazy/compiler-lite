int main(){
    int t[2][2][2];
    int c = 0;
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 2; j = j + 1)
            for (int k = 0; k < 2; k = k + 1) {
                t[i][j][k] = c;
                c = c + 1;
            }
    int sum = 0;
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 2; j = j + 1)
            for (int k = 0; k < 2; k = k + 1)
                sum = sum + t[i][j][k];
    putint(sum); putch(10);
    putint(t[0][0][0]); putch(10);
    putint(t[1][1][1]); putch(10);
    return 0;
}
