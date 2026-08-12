int w[5];
int v[5];
int dp[6][9];
int main(){
    w[1] = 2; w[2] = 3; w[3] = 4; w[4] = 5;
    v[1] = 3; v[2] = 4; v[3] = 5; v[4] = 6;
    for (int i = 0; i <= 4; i = i + 1)
        for (int j = 0; j <= 8; j = j + 1) dp[i][j] = 0;
    for (int i = 1; i <= 4; i = i + 1) {
        for (int j = 0; j <= 8; j = j + 1) {
            dp[i][j] = dp[i - 1][j];
            if (j >= w[i]) {
                int cand = dp[i - 1][j - w[i]] + v[i];
                if (cand > dp[i][j]) dp[i][j] = cand;
            }
        }
    }
    putint(dp[4][8]); putch(10);
    putint(dp[4][0]); putch(10);
    return 0;
}
