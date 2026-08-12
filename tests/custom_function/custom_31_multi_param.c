int sum4(int a, int b, int c, int d) {
    return a + b + c + d;
}
int calc(int a, int b, int c, int d, int e) {
    return (a * b + c) / d - e;
}
int main(){
    putint(sum4(1, 2, 3, 4)); putch(10);
    putint(sum4(10, 20, 30, 40)); putch(10);
    putint(calc(10, 10, 5, 3, 1)); putch(10);
    putint(calc(7, 6, 8, 4, 5)); putch(10);
    return 0;
}
