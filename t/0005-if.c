long f(void) {
    return 10;
}

int main(void) {
    long x;
    long *ptr;
    long **pptr;

    x = f();
    ptr = &x;
    pptr = &ptr;

    if(*ptr > 100)
        return 1;
    if(**pptr <= -100)
        return 1;

    **pptr = f() + *ptr;

    if(x == 20)
        return 0;
    else
        return 1;
}
