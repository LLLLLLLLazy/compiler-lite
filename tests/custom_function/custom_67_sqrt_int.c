int isqrt(int n) {
    int l = 0;
    int r = n;
    int ans = 0;
    while (l <= r) {
        int m = (l + r) / 2;
        if (m * m <= n) {
            ans = m;
            l = m + 1;
        } else {
            r = m - 1;
        }
    }
    return ans;
}
int main(){
    for (int i = 0; i <= 20; i = i + 1) {
        putint(isqrt(i));
        putch(32);
    }
    putch(10);
    putint(isqrt(100)); putch(10);
    putint(isqrt(99)); putch(10);
    return 0;
}
