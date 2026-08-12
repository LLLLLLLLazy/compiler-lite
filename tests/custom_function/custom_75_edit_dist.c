int dp[7][8];
int main(){
    int s1[7] = {107, 105, 116, 116, 101, 110, 0};
    int s2[8] = {115, 105, 116, 116, 105, 110, 103, 0};
    for (int i = 0; i <= 6; i = i + 1) dp[i][0] = i;
    for (int j = 0; j <= 7; j = j + 1) dp[0][j] = j;
    for (int i = 1; i <= 6; i = i + 1) {
        for (int j = 1; j <= 7; j = j + 1) {
            int cost = 1;
            if (s1[i - 1] == s2[j - 1]) cost = 0;
            int a = dp[i - 1][j] + 1;
            int b = dp[i][j - 1] + 1;
            int c = dp[i - 1][j - 1] + cost;
            int best = a;
            if (b < best) best = b;
            if (c < best) best = c;
            dp[i][j] = best;
        }
    }
    putint(dp[6][7]); putch(10);
    return 0;
}
