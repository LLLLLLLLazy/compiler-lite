int sideEffect = 0;
int bump() { sideEffect = sideEffect + 1; return sideEffect; }
int main(){
    int r;
    if (0 && bump()) { r = 1; } else { r = 2; }
    putint(r); putch(10);
    putint(sideEffect); putch(10);
    if (1 || bump()) { r = 3; } else { r = 4; }
    putint(r); putch(10);
    putint(sideEffect); putch(10);
    if (1 == 1 && 2 != 3) { r = 5; }
    putint(r); putch(10);
    return 0;
}
