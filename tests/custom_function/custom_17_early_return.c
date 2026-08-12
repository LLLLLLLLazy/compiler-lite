int find(int a[], int n, int target) {
    for (int i = 0; i < n; i = i + 1)
        if (a[i] == target) return i;
    return -1;
}
int main(){
    int a[5];
    a[0] = 3; a[1] = 8; a[2] = 1; a[3] = 9; a[4] = 4;
    putint(find(a, 5, 9)); putch(10);
    putint(find(a, 5, 7)); putch(10);
    putint(find(a, 5, 3)); putch(10);
    return 0;
}
