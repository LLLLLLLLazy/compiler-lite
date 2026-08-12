void qsort(int a[], int l, int r) {
    if (l >= r) return;
    int p = a[l];
    int i = l;
    int j = r;
    while (i < j) {
        while (i < j && a[j] >= p) j = j - 1;
        a[i] = a[j];
        while (i < j && a[i] <= p) i = i + 1;
        a[j] = a[i];
    }
    a[i] = p;
    qsort(a, l, i - 1);
    qsort(a, i + 1, r);
}
int main(){
    int a[10] = {7, 2, 9, 1, 5, 6, 3, 8, 0, 4};
    qsort(a, 0, 9);
    for (int i = 0; i < 10; i = i + 1) {
        putint(a[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
