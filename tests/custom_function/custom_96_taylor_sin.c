float powf_self(float x, int n) {
    float r = 1.0;
    int i = 0;
    while (i < n) {
        r = r * x;
        i = i + 1;
    }
    return r;
}
float sinTaylor(float x) {
    float sum = 0.0;
    int i = 0;
    while (i < 7) {
        float term = powf_self(x, 2 * i + 1);
        int fact = 1;
        for (int j = 2; j <= 2 * i + 1; j = j + 1) fact = fact * j;
        if (i % 2 == 0) sum = sum + term / fact;
        else sum = sum - term / fact;
        i = i + 1;
    }
    return sum;
}
int main(){
    putfloat(sinTaylor(0.0)); putch(10);
    putfloat(sinTaylor(0.5)); putch(10);
    putfloat(sinTaylor(1.0)); putch(10);
    return 0;
}
