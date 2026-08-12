int coin[3];
int dp[7];
int main(){
    coin[0] = 1;
    coin[1] = 3;
    coin[2] = 4;
    dp[0] = 0;
    for (int s = 1; s <= 6; s = s + 1) {
        dp[s] = 100;
        for (int i = 0; i < 3; i = i + 1) {
            if (s >= coin[i] && dp[s - coin[i]] + 1 < dp[s]) dp[s] = dp[s - coin[i]] + 1;
        }
    }
    putint(dp[6]); putch(10);
    putint(dp[0]); putch(10);
    putint(dp[5]); putch(10);
    return 0;
}
