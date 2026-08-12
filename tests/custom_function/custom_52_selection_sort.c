int main(){
    int a[10] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
    for (int i = 0; i < 9; i = i + 1) {
        int mi = i;
        for (int j = i + 1; j < 10; j = j + 1)
            if (a[j] < a[mi]) mi = j;
        int t = a[i];
        a[i] = a[mi];
        a[mi] = t;
    }
    for (int i = 0; i < 10; i = i + 1) {
        putint(a[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
