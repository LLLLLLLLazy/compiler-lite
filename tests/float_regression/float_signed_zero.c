float global_negative_zero = -0.0;

int is_negative_zero(float value)
{
	if (1.0 / value < 0.0) {
		return 1;
	}
	return 0;
}

float add_positive_zero(float value)
{
	return value + 0.0;
}

float negate(float value)
{
	return -value;
}

float recursive_identity(float value, int depth)
{
	if (depth <= 0) {
		return value;
	}
	return recursive_identity(value, depth - 1);
}

int main()
{
	float local_negative_zero = -0.0;
	putint(is_negative_zero(global_negative_zero));
	putch(32);
	putint(is_negative_zero(local_negative_zero));
	putch(32);
	putint(is_negative_zero(add_positive_zero(global_negative_zero)));
	putch(32);
	putint(is_negative_zero(negate(global_negative_zero)));
	putch(32);
	int count = getint();
	int index = 0;
	int loop_negative_zero = 0;
	while (index < count) {
		loop_negative_zero = loop_negative_zero +
			is_negative_zero(recursive_identity(-0.0, index));
		index = index + 1;
	}
	putint(loop_negative_zero);
	putch(10);
	return 0;
}
