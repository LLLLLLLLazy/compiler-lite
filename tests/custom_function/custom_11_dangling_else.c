int main(){
    int x = 5;
    if (x > 0) if (x > 10) putint(1); else putint(2);
    putch(10);
    if (x > 0) { if (x > 10) putint(1); } else putint(3);
    putch(10);
    if (x < 0) if (x > 10) putint(4); else putint(5);
    putch(10);
    putint(0);
    return 0;
}
