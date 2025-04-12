int main(void) {
    int b;
    long t;

    t = 100;
    b = 1;

    while(b & (t != 0)) {
        if(t & 1)
            b = t >= -100; 
        else
            b = t <= 100;

        t = t - 1;
    }
}
