int main() {
    signed v;

    v = 100;
    v = v % 3;
    v = v * 3;
    v = v / 2;

    if(v == 1)
        return 0;
    return 1;
}
