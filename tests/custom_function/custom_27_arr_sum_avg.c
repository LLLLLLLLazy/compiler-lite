int main(){
    int a[8] = {1, 3, 5, 7, 9, 11, 13, 15};
    int s = 0;
    for (int i = 0; i < 8; i = i + 1) s = s + a[i];
    putint(s); putch(10);
    putint(s / 8); putch(10);
    putint(s % 8); putch(10);
    return 0;
}
