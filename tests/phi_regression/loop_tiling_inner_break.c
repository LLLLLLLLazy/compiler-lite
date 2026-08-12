int output[96][96];
int input[96][96];

void kernel(int target[][96], int source[][96], int stop) {
	if (stop < 0) {
		kernel(target, source, stop + 1);
		return;
	}
	int row = 0;
	while (row < 96) {
		int column = 0;
		while (column < 96) {
			if (column == stop) {
				break;
			}
			int row_index = row * row % 96;
			int column_index = column * column % 96;
			target[row_index][column_index] = source[row_index][column_index] + 1;
			column = column + 1;
		}
		row = row + 1;
	}
}

int main() {
	int stop = getint();
	int row = 0;
	while (row < 96) {
		int column = 0;
		while (column < 96) {
			input[row][column] = row + column;
			column = column + 1;
		}
		row = row + 1;
	}
	kernel(output, input, stop);
	putint(output[0][0]);
	putch(32);
	putint(output[0][64]);
	putch(32);
	putint(output[49][64]);
	putch(10);
	return 0;
}
