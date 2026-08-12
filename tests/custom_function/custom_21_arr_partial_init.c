int ga[6];
int main(){
    int b[6] = {1, 2, 3};
    int sum = 0;
    for (int i = 0; i < 6; i = i + 1) sum = sum + b[i];
    putint(sum); putch(10);
    ga[2] = 7;
    putint(ga[0]); putch(10);
    putint(ga[2]); putch(10);
    putint(ga[5]); putch(10);
    return 0;
}
