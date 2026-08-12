int w[4];
int v[4];
int dp[11];
int main(){
    w[0] = 2; w[1] = 3; w[2] = 4; w[3] = 5;
    v[0] = 3; v[1] = 4; v[2] = 5; v[3] = 6;
    for (int j = 0; j <= 10; j = j + 1) dp[j] = 0;
    for (int j = 0; j <= 10; j = j + 1) {
        for (int i = 0; i < 4; i = i + 1) {
            if (j >= w[i]) {
                int cand = dp[j - w[i]] + v[i];
                if (cand > dp[j]) dp[j] = cand;
            }
        }
    }
    putint(dp[10]); putch(10);
    putint(dp[6]); putch(10);
    return 0;
}
