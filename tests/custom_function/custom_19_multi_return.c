int max3(int a, int b, int c) {
    if (a >= b && a >= c) return a;
    if (b >= c) return b;
    return c;
}
int min3(int a, int b, int c) {
    if (a <= b && a <= c) return a;
    if (b <= c) return b;
    return c;
}
int main(){
    putint(max3(1, 2, 3)); putch(10);
    putint(max3(3, 2, 1)); putch(10);
    putint(max3(2, 3, 1)); putch(10);
    putint(max3(5, 5, 4)); putch(10);
    putint(min3(9, 8, 7)); putch(10);
    putint(min3(7, 8, 9)); putch(10);
    return 0;
}
