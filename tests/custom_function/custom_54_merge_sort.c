int tmp[100];
void merge(int a[], int l, int m, int r) {
    int i = l;
    int j = m + 1;
    int k = l;
    while (i <= m && j <= r) {
        if (a[i] <= a[j]) {
            tmp[k] = a[i];
            i = i + 1;
        } else {
            tmp[k] = a[j];
            j = j + 1;
        }
        k = k + 1;
    }
    while (i <= m) {
        tmp[k] = a[i];
        i = i + 1;
        k = k + 1;
    }
    while (j <= r) {
        tmp[k] = a[j];
        j = j + 1;
        k = k + 1;
    }
    for (i = l; i <= r; i = i + 1) a[i] = tmp[i];
}
void msort(int a[], int l, int r) {
    if (l >= r) return;
    int m = (l + r) / 2;
    msort(a, l, m);
    msort(a, m + 1, r);
    merge(a, l, m, r);
}
int main(){
    int a[8] = {8, 3, 6, 1, 7, 2, 5, 4};
    msort(a, 0, 7);
    for (int i = 0; i < 8; i = i + 1) {
        putint(a[i]);
        putch(32);
    }
    putch(10);
    return 0;
}
