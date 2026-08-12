int output[96][96];
int input[96][96];

void kernel(int target[][96], int source[][96], int guard) {
	if (guard < 0) {
		kernel(target, source, guard + 1);
		return;
	}
	int row = 0;
	while (96 > row) {
		int column = 0;
		while (96 > column) {
			target[row][column] = source[(row + 95) % 96][(column + 64) % 96];
			column = column + 1;
		}
		row = row + 1;
	}
}

int main() {
	int guard = getint();
	int row = 0;
	while (row < 96) {
		int column = 0;
		while (column < 96) {
			input[row][column] = row * 96 + column;
			column = column + 1;
		}
		row = row + 1;
	}
	kernel(output, input, guard);
	putint(output[1][0]);
	putch(32);
	putint(output[0][64]);
	putch(10);
	return 0;
}
