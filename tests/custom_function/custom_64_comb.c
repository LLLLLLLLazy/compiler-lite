int C[11][11];
int main(){
    for (int i = 0; i <= 10; i = i + 1) {
        C[i][0] = 1;
        C[i][i] = 1;
        for (int j = 1; j < i; j = j + 1)
            C[i][j] = C[i - 1][j - 1] + C[i - 1][j];
    }
    putint(C[10][5]); putch(10);
    putint(C[5][2]); putch(10);
    putint(C[6][3]); putch(10);
    putint(C[10][0]); putch(10);
    putint(C[10][10]); putch(10);
    return 0;
}
