int main(){
    int count = 0;
    int c;
    c = getch();
    while (c != 10) {
        if (c >= 97 && c <= 122) count = count + 1;
        c = getch();
    }
    putint(count); putch(10);
    return 0;
}
