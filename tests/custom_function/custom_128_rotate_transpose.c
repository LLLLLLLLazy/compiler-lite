/* 矩阵 90° 旋转 + 原地转置(带守卫尾部: 内循环 j<i 上三角交换),
   再加水平翻转, 校验等价关系。守卫上三角循环是
   GuardedTailCollapse/尾部守卫优化的形态。 */
int mat[5][5];
int rot[5][5];
void transpose() {
    for (int i = 0; i < 5; i = i + 1)
        for (int j = 0; j < i; j = j + 1) {
            int t = mat[i][j];
            mat[i][j] = mat[j][i];
            mat[j][i] = t;
        }
}
void hflip() {
    for (int i = 0; i < 5; i = i + 1)
        for (int j = 0; j < 2; j = j + 1) {
            int t = mat[i][j];
            mat[i][j] = mat[i][4 - j];
            mat[i][4 - j] = t;
        }
}
int main(){
    int c = 0;
    for (int i = 0; i < 5; i = i + 1)
        for (int j = 0; j < 5; j = j + 1) {
            mat[i][j] = c;
            c = c + 1;
        }
    for (int i = 0; i < 5; i = i + 1)
        for (int j = 0; j < 5; j = j + 1)
            rot[j][4 - i] = mat[i][j];
    transpose();
    hflip();
    int diff = 0;
    for (int i = 0; i < 5; i = i + 1)
        for (int j = 0; j < 5; j = j + 1)
            if (mat[i][j] != rot[i][j]) diff = diff + 1;
    putint(diff); putch(10);
    for (int i = 0; i < 5; i = i + 1) {
        for (int j = 0; j < 5; j = j + 1) {
            putint(rot[i][j]);
            putch(32);
        }
        putch(10);
    }
    return 0;
}
