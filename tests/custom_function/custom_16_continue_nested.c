int main(){
    int sum = 0;
    for (int i = 0; i < 4; i = i + 1) {
        for (int j = 0; j < 3; j = j + 1) {
            if (j == 1) continue;
            sum = sum + 1;
        }
    }
    putint(sum); putch(10);
    sum = 0;
    for (int i = 0; i < 6; i = i + 1) {
        if (i % 2 == 0) continue;
        sum = sum + i;
    }
    putint(sum); putch(10);
    return 0;
}
