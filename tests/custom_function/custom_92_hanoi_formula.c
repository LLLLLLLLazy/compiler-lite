int main(){
    int r = 1;
    int i = 0;
    while (i < 10) {
        r = r * 2;
        i = i + 1;
    }
    putint(r - 1); putch(10);
    r = 1;
    i = 0;
    while (i < 5) {
        r = r * 2;
        i = i + 1;
    }
    putint(r - 1); putch(10);
    return 0;
}
