int data[96][96];

void kernel(int output[][96], int input[][96], int guard) {
	if (guard < 0) {
		kernel(output, input, guard + 1);
		return;
	}
	int row = 0;
	while (row < 96) {
		int column = 0;
		while (column < 96) {
			output[row][column] = input[(row + 95) % 96][(column + 64) % 96];
			column = column + 1;
		}
		row = row + 1;
	}
}

void wrapper(int output[][96], int input[][96], int guard) {
	if (guard < 0) {
		wrapper(output, input, guard + 1);
		return;
	}
	kernel(output, input, guard);
}

int main() {
	int guard = getint();
	int row = 0;
	while (row < 96) {
		int column = 0;
		while (column < 96) {
			data[row][column] = row * 96 + column;
			column = column + 1;
		}
		row = row + 1;
	}
	wrapper(data, data, guard);
	putint(data[1][0]);
	putch(32);
	putint(data[0][64]);
	putch(32);
	putint(data[50][5]);
	putch(32);
	putint(data[49][69]);
	putch(10);
	return 0;
}
