/* libc */
int  atoi(void *nptr);
void exit(int status);
void *calloc(unsigned long n, unsigned long size);
void *memset(void *p, int c, unsigned long n);
int putchar(int c);

/* program */
int N;
int **queens;

void init(int argc, void **argv) {
    if(argc >= 2) {
        N = atoi(argv[1]);
        if(N <= 0)
            exit(1);
    } else {
        N = 8;
    }
    
    int x;
    queens = calloc(N, 8);
    for(x = 0; x < N; x = x + 1) {
        queens[x] = calloc(N, 4);
        memset(queens[x], 0, N * 4);
    }
}

int safe(int x, int y) {
    int r;

    for(r = 1; r < N; r = r + 1) {
        if(y - r >= 0) {
            if(queens[x][y - r])
                return 1;
        }
        if(y + r < N) {
            if(queens[x][y + r])
                return 1;
        }

        if(x - r >= 0) {
            if(queens[x - r][y])
                return 1;
        }
        if(x + r < N) {
            if(queens[x + r][y])
                return 1;
        }

        if((x + r < N) & (y + r < N)) {
            if(queens[x + r][y + r])
                return 1;
        }
        if((x - r >= 0) & (y - r >= 0)) {
            if(queens[x - r][y - r])
                return 1;
        }
        if((x + r < N) & (y - r >= 0)) {
            if(queens[x + r][y - r])
                return 1;
        }
        if((x - r >= 0) & (y + r < N)) {
            if(queens[x - r][y + r])
                return 1;
        }
    }

    return 0;
}

void board();

int place(int y) {
    if(y == N)
        return 1;

    int x;
    for(x = 0; x < N; x = x + 1) {
        if(!safe(x, y)) {
            queens[x][y] = 1;

            if(place(y + 1))
                return 1;

            queens[x][y] = 0;
        }
    }

    return 0;
}

void board() {
    int x, y;

    for(x = 0; x < N; x = x + 1) {
        for(y = 0; y < N; y = y + 1) {
            if(queens[x][y])
                putchar(0x51); /* 'Q' */
            else
                putchar(0x2e); /* '.' */
        }
        putchar(0x0a); /* '\n' */
    }
}


int main(int argc, void **argv) {
    init(argc, argv);

    int ret;

    ret = place(0);

    if(ret) {
        board();
        return 0;
    }

    return 1;
}
