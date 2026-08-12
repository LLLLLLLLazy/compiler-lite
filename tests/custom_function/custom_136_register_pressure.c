/* 寄存器压力: 循环内 14 个跨调用存活局部量(必然溢出)、
   长表达式树、12 参调用前后 caller-saved 活跃, 压测 RA 溢出与调度正确性。 */
int work(int x) {
    return x * 3 + 7;
}
int bigExpr(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10;
}
int main(){
    int v0 = 1;
    int v1 = 2;
    int v2 = 3;
    int v3 = 4;
    int v4 = 5;
    int v5 = 6;
    int v6 = 7;
    int v7 = 8;
    int v8 = 9;
    int v9 = 10;
    int v10 = 11;
    int v11 = 12;
    int v12 = 13;
    int v13 = 14;
    for (int i = 0; i < 8; i = i + 1) {
        v0 = v0 + work(v1 + i);
        v1 = v1 + work(v2 + i);
        v2 = v2 + work(v3 + i);
        v3 = v3 + work(v4 + i);
        v4 = v4 + work(v5 + i);
        v5 = v5 + work(v6 + i);
        v6 = v6 + work(v7 + i);
        v7 = v7 + work(v8 + i);
        v8 = v8 + work(v9 + i);
        v9 = v9 + work(v10 + i);
        v10 = v10 + work(v11 + i);
        v11 = v11 + work(v12 + i);
        v12 = v12 + work(v13 + i);
        v13 = v13 + work(v0 + i);
    }
    putint(v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12 + v13); putch(10);
    int t = bigExpr(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9);
    putint(t); putch(10);
    int e = v0 * 3 + v1 * 4 + v2 * 5 + v3 * 6 + v4 * 7 + v5 * 8 + v6 * 9 + v7 * 10
          + v8 * 11 + v9 * 12 + v10 * 13 + v11 * 14 + v12 * 15 + v13 * 16 + v0 * v1
          + v2 * v3 + v4 * v5 + v6 * v7 + v8 * v9 + v10 * v11;
    putint(e); putch(10);
    int keep = v0 + v2 + v4 + v6 + v8;
    int r = 0;
    for (int i = 0; i < 10; i = i + 1) {
        r = r + bigExpr(i, v1, v3, v5, v7, v9, v11, v13, keep, i * 2);
        r = r - keep;
    }
    putint(r); putch(10);
    return 0;
}
