int main() {
	int bound = getint();
	int sum = 7;
	int index = 0;
	while (bound > index) {
		sum = sum + 3;
		index = index + 1;
	}
	putint(sum);
	putch(10);
	return 0;
}
