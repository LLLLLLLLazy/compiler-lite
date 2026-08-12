int A[2][3];
int B[3][2];
int C[2][2];
int main(){
    A[0][0] = 1; A[0][1] = 2; A[0][2] = 3;
    A[1][0] = 4; A[1][1] = 5; A[1][2] = 6;
    B[0][0] = 7; B[0][1] = 8;
    B[1][0] = 9; B[1][1] = 10;
    B[2][0] = 11; B[2][1] = 12;
    for (int i = 0; i < 2; i = i + 1)
        for (int j = 0; j < 2; j = j + 1) {
            int s = 0;
            for (int k = 0; k < 3; k = k + 1)
                s = s + A[i][k] * B[k][j];
            C[i][j] = s;
        }
    putint(C[0][0]); putch(10);
    putint(C[0][1]); putch(10);
    putint(C[1][0]); putch(10);
    putint(C[1][1]); putch(10);
    return 0;
}
