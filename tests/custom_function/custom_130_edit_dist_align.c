/* 编辑距离 DP + 对齐回溯: 插入/删除/替换代价, 回溯路径输出操作序列
   (0=保持,1=替换,2=删除,3=插入), 两组字符串(int 数组)。 */
int a[12];
int b[12];
int dp[13][13];
int ops[24];
int opCnt;
int edist(int n, int m) {
    for (int i = 0; i <= n; i = i + 1) dp[i][0] = i;
    for (int j = 0; j <= m; j = j + 1) dp[0][j] = j;
    for (int i = 1; i <= n; i = i + 1)
        for (int j = 1; j <= m; j = j + 1) {
            int cost = 1;
            if (a[i - 1] == b[j - 1]) cost = 0;
            int ins = dp[i][j - 1] + 1;
            int del = dp[i - 1][j] + 1;
            int rep = dp[i - 1][j - 1] + cost;
            dp[i][j] = ins;
            if (del < dp[i][j]) dp[i][j] = del;
            if (rep < dp[i][j]) dp[i][j] = rep;
        }
    opCnt = 0;
    int i = n;
    int j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0) {
            if (a[i - 1] == b[j - 1]) {
                ops[opCnt] = 0;
                opCnt = opCnt + 1;
                i = i - 1;
                j = j - 1;
            } else {
                if (dp[i][j] == dp[i - 1][j - 1] + 1) {
                    ops[opCnt] = 1;
                    opCnt = opCnt + 1;
                    i = i - 1;
                    j = j - 1;
                } else {
                    if (dp[i][j] == dp[i - 1][j] + 1) {
                        ops[opCnt] = 2;
                        opCnt = opCnt + 1;
                        i = i - 1;
                    } else {
                        ops[opCnt] = 3;
                        opCnt = opCnt + 1;
                        j = j - 1;
                    }
                }
            }
        } else {
            if (i > 0) {
                ops[opCnt] = 2;
                opCnt = opCnt + 1;
                i = i - 1;
            } else {
                ops[opCnt] = 3;
                opCnt = opCnt + 1;
                j = j - 1;
            }
        }
    }
    return dp[n][m];
}
void run(int na, int ma) {
    int d = edist(na, ma);
    putint(d); putch(58);
    for (int k = opCnt - 1; k >= 0; k = k - 1) {
        putint(ops[k]);
        putch(32);
    }
    putch(10);
}
int main(){
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; a[4] = 5;
    b[0] = 1; b[1] = 2; b[2] = 4; b[3] = 6; b[4] = 5;
    run(5, 5);
    a[0] = 7; a[1] = 7; a[2] = 7; a[3] = 7;
    b[0] = 7; b[1] = 8; b[2] = 8; b[3] = 8;
    run(4, 4);
    a[0] = 3; a[1] = 3; a[2] = 3;
    b[0] = 3;
    run(3, 1);
    return 0;
}
