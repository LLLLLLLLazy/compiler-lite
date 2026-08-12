int main(){
    int a = 0xFF;
    int b = 0x10;
    int c = 017;
    putint(a); putch(10);
    putint(b); putch(10);
    putint(c); putch(10);
    putint(0Xab); putch(10);
    putint(0x7fffffff); putch(10);
    putint(0); putch(10);
    putint(0x1f + 1); putch(10);
    return 0;
}
