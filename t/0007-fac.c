unsigned fac(unsigned n) {
    if(n == 0)
        return 1;
    return n * fac(n - 1);
}

int main(void) {
    int z;

    z = fac(5) == 120;

    if(z)
        return 0;
    return 1;
}
