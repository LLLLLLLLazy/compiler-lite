float sqrtf_new(float x) {
    if (x <= 0.0) return 0.0;
    float guess = x;
    int i = 0;
    while (i < 20) {
        guess = (guess + x / guess) / 2.0;
        i = i + 1;
    }
    return guess;
}
int main(){
    putfloat(sqrtf_new(2.0)); putch(10);
    putfloat(sqrtf_new(4.0)); putch(10);
    putfloat(sqrtf_new(9.0)); putch(10);
    return 0;
}
