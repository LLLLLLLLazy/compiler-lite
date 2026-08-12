int read_slots(int data[][3][4], int plane)
{
    return data[plane][1][1] * 3 + data[plane][0][1] * 5;
}

int mutate_slots(int data[][3][4], int salt)
{
    int first = data[1][1][1];
    int second = data[0][0][1];
    data[0][0][1] = (first * 4 + salt + 8) % 1009;
    if ((first + second + salt) % 2 == 0) {
        data[1][1][1] = (second * 3 - salt - 8) % 1009;
    } else {
        data[1][1][1] = (first - second * 3 + salt) % 1009;
    }
    return first * 7 + second * 11;
}

int loop_mix(int data[][3][4], int salt)
{
    int plane = 0;
    int result = salt;
    while (plane < 2) {
        int row = 0;
        while (row < 3) {
            int column = 0;
            while (column < 4) {
                int old = data[plane][row][column];
                if ((plane + row + column + salt) % 3 == 0) {
                    data[plane][row][column] = (old * 3 + result + 8) % 1009;
                } else {
                    data[plane][row][column] = (old - result - 8) % 1009;
                }
                result = (result * 3 + data[plane][row][column] + old) % 1009;
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
    int data[2][3][4] = {
        {{3, -15, -18, 14}, {-2, -19, 0, 18}, {-19, -7, 10, 16}},
        {{-17, -4, -5, 18}, {0, 0, 6, -11}, {-11, -17, 8, -16}}
    };
    int before_first = read_slots(data, 1);
    int before_second = read_slots(data, 0);
    int mutation = mutate_slots(data, -13);
    int after_first = read_slots(data, 1);
    int after_second = read_slots(data, 0);
    int looped = loop_mix(data, mutation % 29);
    int result = (before_first * 3 + before_second * 5 + mutation * 7 +
                  after_first * 11 + after_second * 13 + looped * 17 +
                  data[1][1][3]) % 1009;
    putint(result);
    putch(32);
    putint(data[1][1][1]);
    putch(32);
    putint(data[0][0][1]);
    putch(10);
    return (result % 251 + 251) % 251;
}
