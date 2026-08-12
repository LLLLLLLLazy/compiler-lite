int main(){
    int f0 = 1;
    int f1 = 1;
    int f2 = 2;
    int i = 3;
    while (i <= 10) {
        int t = f0 + f1 + f2;
        f0 = f1;
        f1 = f2;
        f2 = t;
        i = i + 1;
    }
    putint(f2); putch(10);
    f0 = 1;
    f1 = 1;
    i = 2;
    while (i <= 10) {
        int t = f0 + f1;
        f0 = f1;
        f1 = t;
        i = i + 1;
    }
    putint(f1); putch(10);
    return 0;
}
