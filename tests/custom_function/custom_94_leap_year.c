int daysOfMonth(int y, int m) {
    if (m == 2) {
        if (y % 400 == 0) return 29;
        if (y % 100 == 0) return 28;
        if (y % 4 == 0) return 29;
        return 28;
    }
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}
int main(){
    int total = 0;
    for (int y = 2000; y <= 2024; y = y + 1)
        for (int m = 1; m <= 12; m = m + 1)
            total = total + daysOfMonth(y, m);
    putint(total); putch(10);
    putint(daysOfMonth(2024, 2)); putch(10);
    putint(daysOfMonth(2023, 2)); putch(10);
    putint(daysOfMonth(2000, 2)); putch(10);
    putint(daysOfMonth(1900, 2)); putch(10);
    return 0;
}
