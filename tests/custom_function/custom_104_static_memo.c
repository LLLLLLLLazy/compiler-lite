/* 局部 static 变量: 函数内 static 标量计数器、static 数组记忆化缓存。
   校验 Mem2Reg 对 static 的逃逸判定、跨调用状态保持与纯度分析的交互。 */
int fibm(int n) {
    static int cache[40];
    if (n < 2) return n;
    if (cache[n]) return cache[n];
    int r = fibm(n - 1) + fibm(n - 2);
    cache[n] = r;
    return r;
}
int nextId() {
    static int id = 100;
    id = id + 1;
    return id;
}
int trib(int n) {
    static int seen[30];
    if (n < 3) return n / 2 + n % 2;
    if (seen[n]) return seen[n];
    int r = trib(n - 1) + trib(n - 2) + trib(n - 3);
    seen[n] = r;
    return r;
}
int main(){
    for (int i = 0; i <= 25; i = i + 1) {
        putint(fibm(i));
        putch(32);
    }
    putch(10);
    for (int i = 0; i <= 20; i = i + 1) {
        putint(trib(i));
        putch(32);
    }
    putch(10);
    for (int k = 0; k < 5; k = k + 1) {
        putint(nextId());
        putch(32);
    }
    putch(10);
    putint(fibm(30)); putch(32);
    putint(fibm(30)); putch(10);
    return 0;
}
