void *calloc(unsigned long n, unsigned long size);
void free(void *ptr);

int main() {
    int *p, N;

    N = 5;
    p = calloc(N, 4); /* sizeof(int) = 4 */

    int i;

    for(i = 0; i < N; i = i + 1)
        p[i] = i;

    int s;
    for(i = s = 0; i < N; i = i + 1)
        s = s + p[i];

    free(p);

    if(s == 10)
        return 0;

    return 1;
}
