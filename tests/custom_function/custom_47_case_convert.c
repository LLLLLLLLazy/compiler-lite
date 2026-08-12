int main(){
    int c;
    c = getch();
    while (c != 10) {
        if (c >= 97 && c <= 122) putch(c - 32);
        else if (c >= 65 && c <= 90) putch(c + 32);
        else putch(c);
        c = getch();
    }
    putch(10);
    return 0;
}
