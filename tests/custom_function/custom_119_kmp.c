/* KMP 字符串匹配(int 数组当字符): 前缀函数构造与匹配过程,
   输出所有匹配位置。覆盖 while 回溯、跨循环状态依赖。 */
int text[64];
int pat[16];
int pi[16];
void prefix(int p[], int m) {
    pi[0] = 0;
    int j = 0;
    for (int i = 1; i < m; i = i + 1) {
        while (j > 0 && p[i] != p[j]) j = pi[j - 1];
        if (p[i] == p[j]) j = j + 1;
        pi[i] = j;
    }
}
int kmp(int t[], int n, int p[], int m) {
    prefix(p, m);
    int j = 0;
    int cnt = 0;
    for (int i = 0; i < n; i = i + 1) {
        while (j > 0 && t[i] != p[j]) j = pi[j - 1];
        if (t[i] == p[j]) j = j + 1;
        if (j == m) {
            putint(i - m + 1);
            putch(32);
            cnt = cnt + 1;
            j = pi[j - 1];
        }
    }
    putch(10);
    return cnt;
}
int main(){
    int n = 17;
    int t[17];
    t[0] = 1; t[1] = 2; t[2] = 1; t[3] = 2; t[4] = 1; t[5] = 2;
    t[6] = 3; t[7] = 1; t[8] = 2; t[9] = 1; t[10] = 2; t[11] = 1;
    t[12] = 2; t[13] = 3; t[14] = 4; t[15] = 1; t[16] = 2;
    int p1[3];
    p1[0] = 1; p1[1] = 2; p1[2] = 1;
    putint(kmp(t, n, p1, 3)); putch(10);
    int p2[2];
    p2[0] = 1; p2[1] = 2;
    putint(kmp(t, n, p2, 2)); putch(10);
    int p3[1];
    p3[0] = 4;
    putint(kmp(t, n, p3, 1)); putch(10);
    int p4[2];
    p4[0] = 2; p4[1] = 3;
    putint(kmp(t, n, p4, 2)); putch(10);
    return 0;
}
