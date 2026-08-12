/* N 皇后回溯: 逐行放皇后, 列/主副对角线冲突数组, 统计 n=4..8 解数。
   递归 + 全局数组 + 深度循环, 压测递归调用点活跃性与全局副作用分析。 */
int n;
int col[16];
int d1[32];
int d2[32];
int count;
void solve(int row) {
    if (row == n) {
        count = count + 1;
        return;
    }
    for (int c = 0; c < n; c = c + 1) {
        if (col[c] == 0 && d1[row + c] == 0 && d2[row - c + n - 1] == 0) {
            col[c] = 1;
            d1[row + c] = 1;
            d2[row - c + n - 1] = 1;
            solve(row + 1);
            col[c] = 0;
            d1[row + c] = 0;
            d2[row - c + n - 1] = 0;
        }
    }
}
int main(){
    for (int k = 4; k <= 8; k = k + 1) {
        n = k;
        count = 0;
        for (int i = 0; i < 16; i = i + 1) {
            col[i] = 0;
            d1[i] = 0;
            d2[i] = 0;
        }
        for (int i = 16; i < 32; i = i + 1) {
            d1[i] = 0;
            d2[i] = 0;
        }
        solve(0);
        putint(k); putch(58);
        putint(count); putch(32);
    }
    putch(10);
    return 0;
}
