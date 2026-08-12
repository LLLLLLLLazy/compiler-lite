/* Manacher 最长回文: 奇偶回文合并的镜像扩展算法,
   输出每个位置的最长回文半径与全局最长回文。
   覆盖 while 扩展、镜像对称索引与边界条件。 */
int s[32];
int d1r[32];
int d2r[32];
int main(){
    int n = 9;
    s[0] = 1; s[1] = 2; s[2] = 1; s[3] = 2; s[4] = 1;
    s[5] = 3; s[6] = 1; s[7] = 2; s[8] = 1;
    for (int i = 0; i < n; i = i + 1) {
        d1r[i] = 0;
        while (i - d1r[i] - 1 >= 0 && i + d1r[i] + 1 < n && s[i - d1r[i] - 1] == s[i + d1r[i] + 1])
            d1r[i] = d1r[i] + 1;
    }
    for (int i = 0; i < n; i = i + 1) {
        d2r[i] = 0;
        while (i - d2r[i] - 1 >= 0 && i + d2r[i] < n && s[i - d2r[i] - 1] == s[i + d2r[i]])
            d2r[i] = d2r[i] + 1;
    }
    int best = 0;
    int bl = 0;
    int br = 0;
    for (int i = 0; i < n; i = i + 1) {
        putint(d1r[i] * 2 + 1);
        putch(32);
        if (d1r[i] * 2 + 1 > best) {
            best = d1r[i] * 2 + 1;
            bl = i - d1r[i];
            br = i + d1r[i];
        }
    }
    putch(10);
    for (int i = 0; i < n; i = i + 1) {
        putint(d2r[i] * 2);
        putch(32);
        if (d2r[i] * 2 > best) {
            best = d2r[i] * 2;
            bl = i - d2r[i];
            br = i + d2r[i] - 1;
        }
    }
    putch(10);
    putint(best); putch(32);
    putint(bl); putch(32);
    putint(br); putch(10);
    for (int i = bl; i <= br; i = i + 1) {
        putint(s[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
