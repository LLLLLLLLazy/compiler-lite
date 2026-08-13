int affine_sum(int bound)
{
	int counter = 2;
	int index = 5;
	int sum = 7;
	while (counter < bound) {
		sum = sum + (2 * index + 3);
		index = index + 2;
		counter = counter + 1;
	}
	return sum;
}

int triangular_sum(int bound)
{
	int index = 0;
	int sum = 0;
	while (index < bound) {
		sum = sum + index;
		index = index + 1;
	}
	return sum;
}

int modular_add(int count)
{
	int index = 0;
	int value = 9;
	while (index < count) {
		value = (value + -3) % 5;
		index = index + 1;
	}
	return value;
}

int divide_pow2(int count)
{
	int index = 0;
	int value = -1;
	while (index < count) {
		value = value / 4;
		index = index + 1;
	}
	return value;
}

int main()
{
	putint(affine_sum(getint()));
	putch(32);
	putint(affine_sum(getint()));
	putch(32);
	putint(triangular_sum(getint()));
	putch(32);
	putint(modular_add(getint()));
	putch(32);
	putint(divide_pow2(getint()));
	putch(32);
	putint(divide_pow2(getint()));
	putch(32);
	putint(divide_pow2(getint()));
	putch(10);
	return 0;
}
