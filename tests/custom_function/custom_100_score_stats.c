int scores[8];
void sortDesc(int a[], int n) {
    for (int i = 0; i < n - 1; i = i + 1)
        for (int j = 0; j < n - 1 - i; j = j + 1)
            if (a[j] < a[j + 1]) {
                int t = a[j];
                a[j] = a[j + 1];
                a[j + 1] = t;
            }
}
int main(){
    scores[0] = 88; scores[1] = 95; scores[2] = 61; scores[3] = 77;
    scores[4] = 84; scores[5] = 93; scores[6] = 70; scores[7] = 99;
    int sum = 0;
    for (int i = 0; i < 8; i = i + 1) sum = sum + scores[i];
    putint(sum / 8); putch(10);
    sortDesc(scores, 8);
    putint(scores[0]); putch(10);
    putint(scores[7]); putch(10);
    int pass = 0;
    for (int i = 0; i < 8; i = i + 1)
        if (scores[i] >= 60) pass = pass + 1;
    putint(pass); putch(10);
    for (int i = 0; i < 8; i = i + 1) {
        putint(scores[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
