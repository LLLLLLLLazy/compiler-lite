int main(){
    int x = 1;
    int y = 0;
    if (x) y = 5; else y = 6;
    putint(y); putch(10);
    if (y > 10) y = 1;
    putint(y); putch(10);
    if (y) y = y + 1;
    putint(y); putch(10);
    return 0;
}
