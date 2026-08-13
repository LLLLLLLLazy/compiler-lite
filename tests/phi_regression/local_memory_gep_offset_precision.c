int global_data[2][2][3] = {
    {{1, 0, 0}, {0, 0, 0}},
    {{2, 3, 0}, {0, 0, 0}}
};

int checksum(int data[][2][3], int planes)
{
    int plane = 0;
    int result = 0;
    while (plane < planes) {
        int row = 0;
        while (row < 2) {
            int column = 0;
            while (column < 3) {
                result = result * 5 + data[plane][row][column];
                column = column + 1;
            }
            row = row + 1;
        }
        plane = plane + 1;
    }
    return result;
}

int main()
{
    int local_data[2][2][3] = {
        {{8, 0, 0}, {0, 0, 0}},
        {{10, 14, 0}, {0, 0, 0}}
    };
    putint(checksum(global_data, 2));
    putch(32);
    putint(checksum(local_data, 2));
    putch(10);
    return 0;
}
