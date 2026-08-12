int countInv(int a[], int n) {
    int c = 0;
    for (int i = 0; i < n; i = i + 1)
        for (int j = i + 1; j < n; j = j + 1)
            if (a[i] > a[j]) c = c + 1;
    return c;
}
int main(){
    int a[6] = {5, 4, 3, 2, 1, 0};
    int b[6] = {1, 3, 2, 5, 4, 6};
    int c[6] = {1, 2, 3, 4, 5, 6};
    putint(countInv(a, 6)); putch(10);
    putint(countInv(b, 6)); putch(10);
    putint(countInv(c, 6)); putch(10);
    return 0;
}
