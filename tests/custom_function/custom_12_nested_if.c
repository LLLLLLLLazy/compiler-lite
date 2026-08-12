int main(){
    int x = 7;
    if (x > 0) {
        if (x > 1) {
            if (x > 2) {
                if (x > 3) putint(4);
                else putint(3);
            } else putint(2);
        } else putint(1);
    } else putint(0);
    putch(10);
    if (x < 0) putint(-1);
    else if (x < 5) putint(5);
    else if (x < 10) putint(10);
    else putint(99);
    putch(10);
    return 0;
}
