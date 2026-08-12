/* 0/1 背包 + 选择回溯重构: 二维 DP 表 + 逆向回溯输出选择序列,
   两组数据校验最优值与方案唯一性。 */
int wt[6];
int val[6];
int dp[7][12];
int chosen[6];
int solve(int n, int W) {
    for (int j = 0; j <= W; j = j + 1) dp[0][j] = 0;
    for (int i = 1; i <= n; i = i + 1) {
        for (int j = 0; j <= W; j = j + 1) {
            dp[i][j] = dp[i - 1][j];
            if (wt[i - 1] <= j) {
                int take = dp[i - 1][j - wt[i - 1]] + val[i - 1];
                if (take > dp[i][j]) dp[i][j] = take;
            }
        }
    }
    int j = W;
    for (int i = n; i >= 1; i = i - 1) {
        if (dp[i][j] != dp[i - 1][j]) {
            chosen[i - 1] = 1;
            j = j - wt[i - 1];
        } else {
            chosen[i - 1] = 0;
        }
    }
    return dp[n][W];
}
int main(){
    wt[0] = 2; val[0] = 3;
    wt[1] = 3; val[1] = 4;
    wt[2] = 4; val[2] = 5;
    wt[3] = 5; val[3] = 8;
    putint(solve(4, 8)); putch(10);
    for (int i = 0; i < 4; i = i + 1) {
        putint(chosen[i]);
        putch(32);
    }
    putch(10);
    wt[0] = 1; val[0] = 1;
    wt[1] = 2; val[1] = 6;
    wt[2] = 3; val[2] = 10;
    wt[3] = 4; val[3] = 16;
    wt[4] = 5; val[4] = 22;
    putint(solve(5, 10)); putch(10);
    for (int i = 0; i < 5; i = i + 1) {
        putint(chosen[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
