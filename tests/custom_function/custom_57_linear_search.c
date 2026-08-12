int main(){
    int a[7] = {4, 1, 7, 3, 9, 2, 6};
    int found = -1;
    for (int i = 0; i < 7; i = i + 1)
        if (a[i] == 3) found = i;
    putint(found); putch(10);
    found = -1;
    for (int i = 0; i < 7; i = i + 1)
        if (a[i] == 5) found = i;
    putint(found); putch(10);
    found = -1;
    for (int i = 0; i < 7; i = i + 1)
        if (a[i] == 6) found = i;
    putint(found); putch(10);
    return 0;
}
