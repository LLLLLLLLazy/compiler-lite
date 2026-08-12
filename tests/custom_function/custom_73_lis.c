int main(){
    int a[8] = {3, 1, 4, 1, 5, 9, 2, 6};
    int dp[8];
    int best = 1;
    for (int i = 0; i < 8; i = i + 1) dp[i] = 1;
    for (int i = 1; i < 8; i = i + 1) {
        for (int j = 0; j < i; j = j + 1) {
            if (a[j] < a[i] && dp[j] + 1 > dp[i]) dp[i] = dp[j] + 1;
        }
        if (dp[i] > best) best = dp[i];
    }
    putint(best); putch(10);
    return 0;
}
