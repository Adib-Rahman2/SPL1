
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int binarySearch(const int a[], int lo, int hi, int key) {
    if (lo > hi) return -1;
    int mid = (lo + hi) / 2;
    if (a[mid] == key) return mid;
    if (a[mid] < key) return binarySearch(a, mid + 1, hi, key);
    else return binarySearch(a, lo, mid - 1, key);
}

bool isOdd(int n);
bool isEven(int n) { return n == 0 ? true : isOdd(n - 1); }
bool isOdd(int n) { return n == 0 ? false : isEven(n - 1); }
