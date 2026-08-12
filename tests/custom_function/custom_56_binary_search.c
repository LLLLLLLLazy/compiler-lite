int bsearch(int a[], int n, int target) {
    int l = 0;
    int r = n - 1;
    while (l <= r) {
        int m = (l + r) / 2;
        if (a[m] == target) return m;
        if (a[m] < target) l = m + 1;
        else r = m - 1;
    }
    return -1;
}
int main(){
    int a[10] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19};
    putint(bsearch(a, 10, 13)); putch(10);
    putint(bsearch(a, 10, 14)); putch(10);
    putint(bsearch(a, 10, 1)); putch(10);
    putint(bsearch(a, 10, 19)); putch(10);
    putint(bsearch(a, 10, 0)); putch(10);
    return 0;
}
