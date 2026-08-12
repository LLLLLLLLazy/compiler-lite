int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main(){
    putint(fact(0)); putch(10);
    putint(fact(1)); putch(10);
    putint(fact(5)); putch(10);
    putint(fact(10)); putch(10);
    return 0;
}
