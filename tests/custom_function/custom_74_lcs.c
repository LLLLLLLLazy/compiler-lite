int lcs[9][8];
int main(){
    int s1[9] = {97, 98, 99, 98, 100, 97, 98, 0};
    int s2[8] = {98, 100, 99, 97, 98, 97, 0};
    for (int i = 0; i <= 8; i = i + 1) lcs[i][0] = 0;
    for (int j = 0; j <= 7; j = j + 1) lcs[0][j] = 0;
    for (int i = 1; i <= 8; i = i + 1) {
        for (int j = 1; j <= 7; j = j + 1) {
            if (s1[i - 1] == s2[j - 1]) lcs[i][j] = lcs[i - 1][j - 1] + 1;
            else if (lcs[i - 1][j] > lcs[i][j - 1]) lcs[i][j] = lcs[i - 1][j];
            else lcs[i][j] = lcs[i][j - 1];
        }
    }
    putint(lcs[8][7]); putch(10);
    return 0;
}
