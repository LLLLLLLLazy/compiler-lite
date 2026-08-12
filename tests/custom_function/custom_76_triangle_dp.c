int tri[4][4];
int dp[4][4];
int main(){
    tri[0][0] = 5;
    tri[1][0] = 1; tri[1][1] = 2;
    tri[2][0] = 7; tri[2][1] = 3; tri[2][2] = 4;
    tri[3][0] = 9; tri[3][1] = 1; tri[3][2] = 8; tri[3][3] = 6;
    for (int j = 0; j < 4; j = j + 1) dp[3][j] = tri[3][j];
    for (int i = 2; i >= 0; i = i - 1) {
        for (int j = 0; j <= i; j = j + 1) {
            int a = dp[i + 1][j];
            int b = dp[i + 1][j + 1];
            int best = a;
            if (b > best) best = b;
            dp[i][j] = best + tri[i][j];
        }
    }
    putint(dp[0][0]); putch(10);
    return 0;
}
