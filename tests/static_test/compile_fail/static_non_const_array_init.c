int seed = 1;

int main()
{
    static int values[2] = {1, seed};
    return values[0] + values[1];
}
