int output[96][96];

void kernel(int target[][96], int guard) {
	if (guard < 0) {
		kernel(target, guard + 1);
		return;
	}
	int row = 0;
	while (row < 96) {
		int column = 0;
		while (column < 96) {
			target[(row + column) % 96][0] = row * 1000 + column;
			column = column + 1;
		}
		row = row + 1;
	}
}

int main() {
	int guard = getint();
	kernel(output, guard);
	putint(output[32][0]);
	putch(10);
	return 0;
}
