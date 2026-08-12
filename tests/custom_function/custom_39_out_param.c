void minMax(int a[], int n, int out[]) {
    int mn = a[0];
    int mx = a[0];
    for (int i = 1; i < n; i = i + 1) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }
    out[0] = mn;
    out[1] = mx;
}
void swapArr(int a[], int b[], int n) {
    for (int i = 0; i < n; i = i + 1) {
        int t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}
int main(){
    int a[6] = {7, 2, 9, 1, 5, 3};
    int res[2];
    minMax(a, 6, res);
    putint(res[0]); putch(10);
    putint(res[1]); putch(10);
    int x[3] = {1, 2, 3};
    int y[3] = {4, 5, 6};
    swapArr(x, y, 3);
    putint(x[0]); putch(10);
    putint(y[2]); putch(10);
    return 0;
}
