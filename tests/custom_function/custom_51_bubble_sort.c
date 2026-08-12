int main(){
    int a[10] = {9, 3, 7, 1, 8, 2, 6, 4, 5, 0};
    for (int i = 0; i < 9; i = i + 1)
        for (int j = 0; j < 9 - i; j = j + 1)
            if (a[j] > a[j + 1]) {
                int t = a[j];
                a[j] = a[j + 1];
                a[j + 1] = t;
            }
    for (int i = 0; i < 10; i = i + 1) {
        putint(a[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
