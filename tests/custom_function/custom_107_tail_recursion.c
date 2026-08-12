/* 尾递归形态(TailRecursionElim 压力): 尾递归 gcd/求和/幂/数组倒序复制,
   尾调用位于 if/else 两分支末尾, 与循环实现对照输出。 */
int tgcd(int a, int b) {
    if (b == 0) return a;
    return tgcd(b, a % b);
}
int tsum(int n, int acc) {
    if (n == 0) return acc;
    return tsum(n - 1, acc + n);
}
int tpow(int base, int exp, int acc) {
    if (exp == 0) return acc;
    return tpow(base, exp - 1, acc * base);
}
int trev(int a[], int n, int i, int b[]) {
    if (i == n) return 0;
    b[n - 1 - i] = a[i];
    return trev(a, n, i + 1, b);
}
int tdigit(int n, int acc) {
    if (n == 0) return acc;
    return tdigit(n / 10, acc * 10 + n % 10);
}
int main(){
    putint(tgcd(48, 36)); putch(32);
    putint(tgcd(1071, 462)); putch(32);
    putint(tgcd(17, 5)); putch(10);
    putint(tsum(100, 0)); putch(32);
    putint(tsum(1, 0)); putch(32);
    putint(tsum(0, 0)); putch(10);
    putint(tpow(2, 10, 1)); putch(32);
    putint(tpow(3, 7, 1)); putch(32);
    putint(tpow(-2, 9, 1)); putch(10);
    int a[8];
    int b[8];
    for (int i = 0; i < 8; i = i + 1) a[i] = i * 3 + 1;
    trev(a, 8, 0, b);
    for (int i = 0; i < 8; i = i + 1) {
        putint(b[i]);
        putch(32);
    }
    putch(10);
    putint(tdigit(123456789, 0)); putch(10);
    return 0;
}
