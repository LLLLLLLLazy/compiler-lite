/* 矩形矩阵乘法: 4x3 × 3x5 与 2x4 × 4x3, 非方阵边界是
   MatMulInterchange/循环变换的候选形态, 校验索引算术正确性。 */
int A[4][3];
int B[3][5];
int C[4][5];
void matmul(int m, int k, int n) {
    for (int i = 0; i < m; i = i + 1)
        for (int j = 0; j < n; j = j + 1) {
            int s = 0;
            for (int t = 0; t < k; t = t + 1)
                s = s + A[i][t] * B[t][j];
            C[i][j] = s;
        }
}
int main(){
    int cnt = 0;
    for (int i = 0; i < 4; i = i + 1)
        for (int j = 0; j < 3; j = j + 1) {
            A[i][j] = cnt - 5;
            cnt = cnt + 1;
        }
    for (int i = 0; i < 3; i = i + 1)
        for (int j = 0; j < 5; j = j + 1)
            B[i][j] = (i + 1) * (j - 2);
    matmul(4, 3, 5);
    for (int i = 0; i < 4; i = i + 1) {
        for (int j = 0; j < 5; j = j + 1) {
            putint(C[i][j]);
            putch(32);
        }
        putch(10);
    }
    int D[2][4];
    int E[4][3];
    int F[2][3];
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 4; j = j + 1)
            D[i][j] = i * 3 + j - 1;
    for (int i = 0; i < 4; i = i + 1)
        for (int j = 0; j < 3; j = j + 1)
            E[i][j] = i - j;
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 3; j = j + 1) {
            int s = 0;
            for (int t = 0; t < 4; t = t + 1)
                s = s + D[i][t] * E[t][j];
            F[i][j] = s;
        }
    for (int i = 0; i < 2; i = i + 1) {
        for (int j = 0; j < 3; j = j + 1) {
            putint(F[i][j]);
            putch(32);
        }
        putch(10);
    }
    return 0;
}
