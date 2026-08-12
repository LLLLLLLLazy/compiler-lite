void doubleIt(int a[], int n) {
    for (int i = 0; i < n; i = i + 1) a[i] = a[i] * 2;
}
void addOne(int x) {
    x = x + 1;
}
int main(){
    int a[4] = {1, 2, 3, 4};
    doubleIt(a, 4);
    putint(a[0]); putch(10);
    putint(a[3]); putch(10);
    int v = 5;
    addOne(v);
    putint(v); putch(10);
    return 0;
}
