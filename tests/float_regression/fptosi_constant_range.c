int main() {
	int take_invalid_path = getint();
	if (take_invalid_path) {
		int invalid = 2147483648.0;
		putint(invalid);
		putch(10);
	}

	int upper = 2147483520.0;
	int lower = -2147483648.0;
	putint(upper);
	putch(32);
	putint(lower);
	putch(10);
	return 0;
}
