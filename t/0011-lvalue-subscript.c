void *calloc(unsigned long n, unsigned long size);
void free(void *ptr);
int putchar(int c);

int main() {
    int *p, N;

    N = 5;
    p = calloc(N, 4); /* sizeof(int) = 4 */

    int i;

    int s;
    i = 0;
    while(i < N) {
        p[i] = i;

        i = i + 1;
    }

    i = s = 0;
    while(i < N) {
        s = s + p[i];

        i = i + 1;
    }

    free(p);

    if(s == 10)
        return 0;

    return 1;
}
