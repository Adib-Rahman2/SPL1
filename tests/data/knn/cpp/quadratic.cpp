int pairs(int a[], int n) {
    int c = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (a[i] == a[j]) c++;
    return c;
}
