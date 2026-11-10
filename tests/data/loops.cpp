
#include <vector>

int sumArray(const std::vector<int>& v) {
    int s = 0;
    for (int i = 0; i < (int)v.size(); i++) s += v[i];
    return s;
}

int findCommon(int a[], int b[], int n) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (a[i] == b[j]) count++;
        }
    }
    return count;
}

int countHalvings(int n) {
    int steps = 0;
    while (n > 1) {
        n /= 2;
        steps++;
    }
    return steps;
}

class Stack {
public:
    void push(int x) { data[top++] = x; }
private:
    int data[100];
    int top = 0;
};
