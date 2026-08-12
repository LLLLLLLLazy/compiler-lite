int main(){
    int x = 5;
    int y;
    int z;
    y = x++;
    z = ++x;
    putint(y); putch(10);
    putint(z); putch(10);
    putint(x); putch(10);
    x--;
    --y;
    putint(x); putch(10);
    putint(y); putch(10);
    z = x--;
    putint(z); putch(10);
    putint(x); putch(10);
    return 0;
}
