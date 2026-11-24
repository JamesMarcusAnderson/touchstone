/* Emits SPDX 2.3 output. The copyright discussion lives in the guide. */
static int count_parts(int total)
{
    int half = total / 2;
    return half + (total - half);
}

int main(void)
{
    return count_parts(4) - 4;
}
