/* 矩阵链乘 (2x3)*(3x4)*(4x2) + 条件归约矩阵乘:
   归约累加位于 if 内(条件归约), 是 MatMulInterchange 孤儿
   xTrueLoad 缺陷的回归形态 —— 无第二 X 游标时的占位 load。 */
int M1[2][3];
int M2[3][4];
int M3[4][2];
int T1[2][4];
int T2[2][2];
void mul1(int a[][3], int b[][4], int c[][4], int r, int k, int cc) {
    for (int i = 0; i < r; i = i + 1)
        for (int j = 0; j < cc; j = j + 1) {
            int s = 0;
            for (int t = 0; t < k; t = t + 1)
                s = s + a[i][t] * b[t][j];
            c[i][j] = s;
        }
}
void mulCond(int a[][3], int b[][4], int c[][4], int r, int k, int cc, int limit) {
    for (int i = 0; i < r; i = i + 1)
        for (int j = 0; j < cc; j = j + 1) {
            int s = 0;
            for (int t = 0; t < k; t = t + 1) {
                if (a[i][t] * b[t][j] > limit) s = s + a[i][t] * b[t][j];
            }
            c[i][j] = s;
        }
}
void mul2(int a[][4], int b[][2], int c[][2], int r, int k, int cc) {
    for (int i = 0; i < r; i = i + 1)
        for (int j = 0; j < cc; j = j + 1) {
            int s = 0;
            for (int t = 0; t < k; t = t + 1)
                s = s + a[i][t] * b[t][j];
            c[i][j] = s;
        }
}
int main(){
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 3; j = j + 1)
            M1[i][j] = i * 3 + j + 1;
    for (int i = 0; i < 3; i = i + 1)
        for (int j = 0; j < 4; j = j + 1)
            M2[i][j] = i + j;
    for (int i = 0; i < 4; i = i + 1)
        for (int j = 0; j < 2; j = j + 1)
            M3[i][j] = i * 2 - j;
    mul1(M1, M2, T1, 2, 3, 4);
    mul2(T1, M3, T2, 2, 4, 2);
    for (int i = 0; i < 2; i = i + 1) {
        for (int j = 0; j < 2; j = j + 1) {
            putint(T2[i][j]);
            putch(32);
        }
        putch(10);
    }
    mulCond(M1, M2, T1, 2, 3, 4, 4);
    for (int i = 0; i < 2; i = i + 1) {
        for (int j = 0; j < 4; j = j + 1) {
            putint(T1[i][j]);
            putch(32);
        }
        putch(10);
    }
    return 0;
}
