const int cA[4] = {10, 20, 30, 40};
const int cS = 7;
int main(){
    int sum = 0;
    for (int i = 0; i < 4; i = i + 1) sum = sum + cA[i];
    putint(sum); putch(10);
    putint(cS * 6); putch(10);
    return 0;
}
