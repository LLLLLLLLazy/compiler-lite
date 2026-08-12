int sumMat(int m[][3], int rows) {
    int s = 0;
    for (int i = 0; i < rows; i = i + 1)
        for (int j = 0; j < 3; j = j + 1)
            s = s + m[i][j];
    return s;
}
int main(){
    int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int n[3][3] = {{1, 1, 1}, {2, 2, 2}, {3, 3, 3}};
    putint(sumMat(m, 2)); putch(10);
    putint(sumMat(n, 3)); putch(10);
    return 0;
}
