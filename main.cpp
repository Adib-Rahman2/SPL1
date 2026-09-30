#include <iostream>
#include <string>
#include <vector>

using namespace std;

struct AnalysisResult {
    string functionName;
    string timeComplexity;
    string explanation;
};

void showMenu() {
    cout << "\n========================================\n";
    cout << "             CODECOMPLEX\n";
    cout << "========================================\n";
    cout << "1. Analyze C++ File\n";
    cout << "2. Load Example\n";
    cout << "3. Show Sample Result\n";
    cout << "4. Exit\n";
    cout << "Enter choice: ";
}

AnalysisResult sampleAnalysis() {
    AnalysisResult result;
    result.functionName = "findCommon";
    result.timeComplexity = "O(n^2)";
    result.explanation = "Two input-dependent nested loops were detected.";
    return result;
}

int main() {
    int choice;

    do {
        showMenu();
        cin >> choice;

        switch (choice) {
            case 1:
                cout << "\nFile analysis module will be implemented here.\n";
                break;

            case 2:
                cout << "\nExample loader will be implemented here.\n";
                break;

            case 3: {
                AnalysisResult result = sampleAnalysis();

                cout << "\n----------- SAMPLE ANALYSIS -----------\n";
                cout << "Function: " << result.functionName << '\n';
                cout << "Time Complexity: " << result.timeComplexity << '\n';
                cout << "Explanation: " << result.explanation << '\n';
                break;
            }

            case 4:
                cout << "\nExiting CodeComplex...\n";
                break;

            default:
                cout << "\nInvalid choice.\n";
        }
    } while (choice != 4);

    return 0;
}
