int main(){
    int a[9] = {-2, 1, -3, 4, -1, 2, 1, -5, 4};
    int best = a[0];
    int cur = a[0];
    for (int i = 1; i < 9; i = i + 1) {
        if (cur < 0) cur = 0;
        cur = cur + a[i];
        if (cur > best) best = cur;
    }
    putint(best); putch(10);
    int b[4] = {-1, -2, -3, -4};
    best = b[0];
    cur = b[0];
    for (int i = 1; i < 4; i = i + 1) {
        if (cur < 0) cur = 0;
        cur = cur + b[i];
        if (cur > best) best = cur;
    }
    putint(best); putch(10);
    return 0;
}
