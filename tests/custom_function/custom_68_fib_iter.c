int main(){
    int f0 = 0;
    int f1 = 1;
    int i = 2;
    while (i <= 30) {
        int t = f0 + f1;
        f0 = f1;
        f1 = t;
        i = i + 1;
    }
    putint(f1); putch(10);
    return 0;
}
