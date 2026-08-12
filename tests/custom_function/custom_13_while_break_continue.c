int main(){
    int i = 0;
    int sum = 0;
    while (1) {
        i = i + 1;
        if (i > 10) break;
        if (i % 2 == 0) continue;
        sum = sum + i;
    }
    putint(sum); putch(10);
    i = 0;
    while (i < 5) i = i + 1;
    putint(i); putch(10);
    return 0;
}
