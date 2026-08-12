/* 欧拉函数线性筛 + 约数个数调和级数筛:
   输出 1..n 的 phi 序列、若干校验值, 并验证 phi 性质。
   覆盖筛法循环、多数组递推与条件 break。 */
int phi[80];
int primes[80];
int isComp[80];
int ndiv[80];
void sieve(int n) {
    int cnt = 0;
    for (int i = 0; i <= n; i = i + 1) {
        phi[i] = 0;
        isComp[i] = 0;
        ndiv[i] = 0;
    }
    phi[1] = 1;
    for (int i = 2; i <= n; i = i + 1) {
        if (isComp[i] == 0) {
            primes[cnt] = i;
            cnt = cnt + 1;
            phi[i] = i - 1;
        }
        for (int j = 0; j < cnt; j = j + 1) {
            int p = primes[j];
            if (i * p > n) break;
            isComp[i * p] = 1;
            if (i % p == 0) {
                phi[i * p] = phi[i] * p;
                break;
            } else {
                phi[i * p] = phi[i] * (p - 1);
            }
        }
    }
    for (int d = 1; d <= n; d = d + 1)
        for (int m = d; m <= n; m = m + d)
            ndiv[m] = ndiv[m] + 1;
    putint(cnt); putch(10);
}
int main(){
    sieve(60);
    for (int i = 1; i <= 20; i = i + 1) {
        putint(phi[i]);
        putch(32);
    }
    putch(10);
    putint(phi[36]); putch(32);
    putint(phi[37]); putch(32);
    putint(phi[49]); putch(32);
    putint(phi[60]); putch(10);
    putint(ndiv[12]); putch(32);
    putint(ndiv[36]); putch(32);
    putint(ndiv[60]); putch(32);
    putint(ndiv[59]); putch(10);
    return 0;
}
