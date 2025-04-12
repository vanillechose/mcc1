int main() {
    int x, *y;

    x = 0x80;
    y = &x;
    return y[0] & 0x7F;
}
