int main(){
    int count = 0;
    for (int i = 0; i < 3; i = i + 1) {
        for (int j = 0; j < 3; j = j + 1) {
            if (j == 1) break;
            count = count + 1;
        }
    }
    putint(count); putch(10);
    count = 0;
    for (int i = 0; i < 4; i = i + 1) {
        for (int j = 0; j < 3; j = j + 1) {
            count = count + 1;
            if (count == 5) break;
        }
    }
    putint(count); putch(10);
    return 0;
}
