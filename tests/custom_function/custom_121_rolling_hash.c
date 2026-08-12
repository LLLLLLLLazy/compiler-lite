/* 滚动哈希: 前缀哈希(小模数防溢出)与 O(1) 子串比较, 模运算含负数修正。
   校验若干子串相等性与两处修改后的哈希差。 */
int MOD;
int arr[32];
int hashp[33];
int powb[33];
void build(int n) {
    powb[0] = 1;
    for (int i = 1; i <= n; i = i + 1) powb[i] = powb[i - 1] * 31 % MOD;
    hashp[0] = 0;
    for (int i = 0; i < n; i = i + 1)
        hashp[i + 1] = (hashp[i] * 31 + arr[i]) % MOD;
}
int subhash(int l, int r) {
    int h = hashp[r + 1] - hashp[l] * powb[r - l + 1] % MOD;
    h = h % MOD;
    if (h < 0) h = h + MOD;
    return h;
}
int main(){
    MOD = 99991;
    int n = 12;
    arr[0] = 3; arr[1] = 1; arr[2] = 4; arr[3] = 1; arr[4] = 5; arr[5] = 9;
    arr[6] = 2; arr[7] = 6; arr[8] = 5; arr[9] = 3; arr[10] = 5; arr[11] = 8;
    build(n);
    putint(hashp[n]); putch(10);
    putint(subhash(0, 5)); putch(32);
    putint(subhash(6, 11)); putch(10);
    putint(subhash(1, 4)); putch(32);
    putint(subhash(2, 5)); putch(10);
    int eq = 0;
    if (subhash(0, 2) == subhash(6, 8)) eq = 1;
    putint(eq); putch(10);
    arr[3] = 2;
    build(n);
    putint(subhash(1, 4)); putch(32);
    putint(hashp[n]); putch(10);
    return 0;
}
