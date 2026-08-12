/* 2048 行合并模拟: 左滑合并 + 补零 + 方向翻转(反向合并),
   原地读写混叠与 while 双指针, 校验多组合并行进后的局面。 */
int row[4];
int merged[4];
void slideLeft() {
    for (int i = 0; i < 4; i = i + 1) merged[i] = 0;
    int w = 0;
    int lastMerged = -1;
    for (int i = 0; i < 4; i = i + 1) {
        if (row[i] == 0) continue;
        if (w > 0 && merged[w - 1] == row[i] && w - 1 != lastMerged) {
            merged[w - 1] = merged[w - 1] * 2;
            lastMerged = w - 1;
        } else {
            merged[w] = row[i];
            w = w + 1;
        }
    }
    for (int i = 0; i < 4; i = i + 1) row[i] = merged[i];
}
void slideRight() {
    int rev[4];
    for (int i = 0; i < 4; i = i + 1) rev[i] = row[3 - i];
    for (int i = 0; i < 4; i = i + 1) row[i] = rev[i];
    slideLeft();
    for (int i = 0; i < 4; i = i + 1) rev[i] = row[3 - i];
    for (int i = 0; i < 4; i = i + 1) row[i] = rev[i];
}
void printRow() {
    for (int i = 0; i < 4; i = i + 1) {
        putint(row[i]);
        putch(32);
    }
    putch(10);
}
int main(){
    row[0] = 2; row[1] = 2; row[2] = 2; row[3] = 2;
    slideLeft();
    printRow();
    row[0] = 4; row[1] = 2; row[2] = 2; row[3] = 4;
    slideLeft();
    printRow();
    row[0] = 2; row[1] = 2; row[2] = 4; row[3] = 4;
    slideLeft();
    printRow();
    row[0] = 2; row[1] = 2; row[2] = 4; row[3] = 4;
    slideRight();
    printRow();
    row[0] = 0; row[1] = 2; row[2] = 0; row[3] = 2;
    slideLeft();
    printRow();
    row[0] = 8; row[1] = 4; row[2] = 4; row[3] = 0;
    slideRight();
    printRow();
    row[0] = 16; row[1] = 16; row[2] = 8; row[3] = 8;
    slideLeft();
    printRow();
    return 0;
}
