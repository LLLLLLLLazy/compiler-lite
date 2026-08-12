/* 二维 box blur(邻域平均): 循环内带四周边界守卫(if 越界检查),
   守卫尾部形态是 GuardedTailCollapse/守卫消除类优化的压力点。
   输出 5x5 输入的一轮模糊结果与一个 4x4 角落检查。 */
int img[5][5];
int out[5][5];
int sum9(int r, int c) {
    int s = 0;
    for (int dr = -1; dr <= 1; dr = dr + 1)
        for (int dc = -1; dc <= 1; dc = dc + 1) {
            int nr = r + dr;
            int nc = c + dc;
            if (nr >= 0 && nr < 5 && nc >= 0 && nc < 5)
                s = s + img[nr][nc];
        }
    return s;
}
int main(){
    int v = 1;
    for (int i = 0; i < 5; i = i + 1)
        for (int j = 0; j < 5; j = j + 1) {
            img[i][j] = v;
            v = v + 2;
        }
    for (int i = 0; i < 5; i = i + 1)
        for (int j = 0; j < 5; j = j + 1)
            out[i][j] = sum9(i, j);
    for (int i = 0; i < 5; i = i + 1) {
        for (int j = 0; j < 5; j = j + 1) {
            putint(out[i][j]);
            putch(32);
        }
        putch(10);
    }
    int corner = 0;
    int edge = 0;
    for (int i = 0; i < 5; i = i + 1) {
        for (int j = 0; j < 5; j = j + 1) {
            int onIBorder = 0;
            if (i == 0 || i == 4) onIBorder = 1;
            int onJBorder = 0;
            if (j == 0 || j == 4) onJBorder = 1;
            if (onIBorder && onJBorder) corner = corner + out[i][j];
            if (onIBorder || onJBorder) edge = edge + out[i][j];
        }
    }
    putint(corner); putch(32);
    putint(edge); putch(10);
    return 0;
}
