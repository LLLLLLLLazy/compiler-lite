/* 输入驱动的批量查询: 读入 n 个查询值逐个判质数。
   循环内多次调用纯函数(同参不同调用点), 触发 PureCallCSE/循环内纯度分析。
   另含 pow2 同参重复调用与纯函数循环缓存形态。 */
int isPrime(int n) {
    if (n < 2) return 0;
    if (n == 2) return 1;
    if (n % 2 == 0) return 0;
    for (int i = 3; i * i <= n; i = i + 2)
        if (n % i == 0) return 0;
    return 1;
}
int pow2(int k) {
    int r = 1;
    for (int i = 0; i < k; i = i + 1) r = r * 2;
    return r;
}
int queries[32];
int main(){
    int n = getarray(queries);
    for (int i = 0; i < n; i = i + 1) {
        putint(queries[i]);
        putch(58);
        putint(isPrime(queries[i]));
        putch(32);
    }
    putch(10);
    for (int i = 0; i < n; i = i + 1) {
        int a = pow2(i % 5);
        int b = pow2(i % 5);
        if (a != b) putint(1); else putint(0);
        putch(32);
    }
    putch(10);
    int s = 0;
    for (int k = 1; k <= 8; k = k + 1) {
        int t = isPrime(k * 7 + 1);
        s = s + t * k;
    }
    putint(s); putch(10);
    return 0;
}
