float leading_hole[3][2] = {{}, {3.0, 4.0}, {5.0}};
float middle_hole[3][2] = {{1}, {}, {6.0, 7.0}};

int main()
{
    int row = 0;
    while (row < 3) {
        int column = 0;
        while (column < 2) {
            putint(leading_hole[row][column]);
            putch(32);
            column = column + 1;
        }
        row = row + 1;
    }
    putch(10);

    row = 0;
    while (row < 3) {
        int column = 0;
        while (column < 2) {
            putint(middle_hole[row][column]);
            putch(32);
            column = column + 1;
        }
        row = row + 1;
    }
    putch(10);
    return 0;
}
