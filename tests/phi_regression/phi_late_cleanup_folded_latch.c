int loop_with_common_value(int limit)
{
    int alternating = 0;
    int index = 0;
    int once = 0;
    while (index < limit) {
        once = 1;
        index = index + 1;
        if (index % 2 == 0) {
            alternating = alternating + index;
            continue;
        } else {
            alternating = alternating - index;
            continue;
        }
    }
    return index * 10 + once + alternating;
}

int main()
{
    int first = loop_with_common_value(7);
    int second = loop_with_common_value(0);
    putint(first);
    putch(32);
    putint(second);
    putch(10);
    return first + second;
}
