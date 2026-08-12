int main(){
    int i;
    int sum = 0;
    for (i = 0; i < 5; i = i + 1) sum = sum + i;
    putint(sum); putch(10);
    sum = 0;
    for (i = 5; i > 0; i = i - 1) sum = sum + i;
    putint(sum); putch(10);
    sum = 0;
    for (int j = 0; j < 3; j = j + 1) sum = sum + j;
    putint(sum); putch(10);
    sum = 0;
    for (;;) { sum = sum + 1; if (sum == 4) break; }
    putint(sum); putch(10);
    sum = 0;
    for (i = 0; i < 5;) { sum = sum + i; i = i + 1; }
    putint(sum); putch(10);
    return 0;
}
