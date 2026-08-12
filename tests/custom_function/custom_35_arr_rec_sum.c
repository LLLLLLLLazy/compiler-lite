int sumRec(int a[], int n) {
    if (n == 0) return 0;
    return a[n - 1] + sumRec(a, n - 1);
}
int main(){
    int a[5] = {2, 4, 6, 8, 10};
    int b[4] = {1, 1, 1, 1};
    putint(sumRec(a, 5)); putch(10);
    putint(sumRec(b, 4)); putch(10);
    putint(sumRec(a, 0)); putch(10);
    return 0;
}
