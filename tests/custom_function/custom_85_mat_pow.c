int mat[2][2];
int res[2][2];
int tmp[2][2];
void mul(int a[][2], int b[][2]) {
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 2; j = j + 1) {
            int s = 0;
            for (int k = 0; k < 2; k = k + 1)
                s = s + a[i][k] * b[k][j];
            tmp[i][j] = s;
        }
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 2; j = j + 1)
            a[i][j] = tmp[i][j];
}
int main(){
    mat[0][0] = 1; mat[0][1] = 1;
    mat[1][0] = 1; mat[1][1] = 0;
    res[0][0] = 1; res[0][1] = 0;
    res[1][0] = 0; res[1][1] = 1;
    int n = 10;
    while (n > 0) {
        if (n % 2 == 1) mul(res, mat);
        mul(mat, mat);
        n = n / 2;
    }
    putint(res[0][1]); putch(10);
    putint(res[1][1]); putch(10);
    return 0;
}
