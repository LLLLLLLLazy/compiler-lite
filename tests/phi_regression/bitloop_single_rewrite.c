int bit_identity(int value) {
	int length = 32;
	int result = 0;
	int power = 1;
	while (length != 0) {
		int bit = value % 2;
		result = result + power * bit;
		value = value / 2;
		power = power * 2;
		length = length - 1;
	}
	return result;
}

int main() {
	int first = getint();
	int second = getint();
	putint(bit_identity(first));
	putch(32);
	putint(bit_identity(second));
	putch(10);
	return 0;
}
