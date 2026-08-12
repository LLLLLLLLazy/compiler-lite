int sumArr(int a[], int n) {
    int s = 0;
    for (int i = 0; i < n; i = i + 1) s = s + a[i];
    return s;
}
int main(){
    int a[5] = {1, 2, 3, 4, 5};
    int b[3] = {10, 20, 30};
    putint(sumArr(a, 5)); putch(10);
    putint(sumArr(b, 3)); putch(10);
    return 0;
}
