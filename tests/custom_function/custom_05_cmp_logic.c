int main(){
    int x = 5;
    int y = 10;
    if (x < y) putint(1); else putint(0); putch(10);
    if (x <= y) putint(1); else putint(0); putch(10);
    if (x > y) putint(1); else putint(0); putch(10);
    if (x >= y) putint(1); else putint(0); putch(10);
    if (x == y) putint(1); else putint(0); putch(10);
    if (x != y) putint(1); else putint(0); putch(10);
    if (x < y && y > 0) putint(1); else putint(0); putch(10);
    if (x > y || y > 0) putint(1); else putint(0); putch(10);
    if (x != y) putint(1); else putint(0); putch(10);  /* !(x==y) 等价形式 */
    if (!(!(!x))) putint(1); else putint(0); putch(10);
    return 0;
}
