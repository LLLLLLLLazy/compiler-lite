int main(){
    int a[10] = {4, 3, 2, 1, 0, 9, 8, 7, 6, 5};
    for (int i = 1; i < 10; i = i + 1) {
        int key = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j + 1] = a[j];
            j = j - 1;
        }
        a[j + 1] = key;
    }
    for (int i = 0; i < 10; i = i + 1) {
        putint(a[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
