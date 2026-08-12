/* 数独求解: 9x9 回溯 + 行列宫冲突检查, 深度递归与二维数组索引运算。
   固定谜题, 输出求解后的完整盘面。 */
int board[9][9];
int found;
int ok(int r, int c, int v) {
    for (int i = 0; i < 9; i = i + 1) {
        if (board[r][i] == v) return 0;
        if (board[i][c] == v) return 0;
    }
    int br = r / 3 * 3;
    int bc = c / 3 * 3;
    for (int i = br; i < br + 3; i = i + 1)
        for (int j = bc; j < bc + 3; j = j + 1)
            if (board[i][j] == v) return 0;
    return 1;
}
void solve2(int r, int c) {
    if (found) return;
    if (r == 9) {
        found = 1;
        return;
    }
    int nr = r;
    int nc = c + 1;
    if (nc == 9) {
        nc = 0;
        nr = r + 1;
    }
    if (board[r][c] != 0) {
        solve2(nr, nc);
        return;
    }
    for (int v = 1; v <= 9; v = v + 1) {
        if (ok(r, c, v)) {
            board[r][c] = v;
            solve2(nr, nc);
            if (found) return;
            board[r][c] = 0;
        }
    }
}
int main(){
    board[0][0] = 5; board[0][1] = 3; board[0][4] = 7;
    board[1][0] = 6; board[1][3] = 1; board[1][4] = 9; board[1][5] = 5;
    board[2][1] = 9; board[2][2] = 8; board[2][7] = 6;
    board[3][0] = 8; board[3][4] = 6; board[3][8] = 3;
    board[4][0] = 4; board[4][3] = 8; board[4][5] = 3; board[4][8] = 1;
    board[5][0] = 7; board[5][4] = 2; board[5][8] = 6;
    board[6][1] = 6; board[6][6] = 2; board[6][7] = 8;
    board[7][3] = 4; board[7][4] = 1; board[7][5] = 9; board[7][8] = 5;
    board[8][4] = 8; board[8][7] = 7; board[8][8] = 9;
    found = 0;
    solve2(0, 0);
    putint(found); putch(10);
    for (int i = 0; i < 9; i = i + 1) {
        for (int j = 0; j < 9; j = j + 1) {
            putint(board[i][j]);
            putch(32);
        }
        putch(10);
    }
    return 0;
}
