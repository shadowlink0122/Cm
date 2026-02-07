// ベンチマーク5: 動的配列（vector）ソート
// 1,000要素のstd::vector<int>をバブルソート

#include <chrono>
#include <iostream>
#include <vector>

using namespace std;
using namespace std::chrono;

void bubble_sort(vector<int>& arr) {
    int n = arr.size();
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                swap(arr[j], arr[j + 1]);
            }
        }
    }
}

int main() {
    int n = 1000;
    vector<int> vec;

    // 逆順で初期化（最悪ケース）
    for (int i = 0; i < n; i++) {
        vec.push_back(n - i);
    }

    auto start = high_resolution_clock::now();
    bubble_sort(vec);
    auto end = high_resolution_clock::now();

    // 検証：ソートされているか確認
    bool sorted = true;
    for (int i = 1; i < n; i++) {
        if (vec[i - 1] > vec[i]) {
            sorted = false;
            break;
        }
    }

    auto duration = duration_cast<microseconds>(end - start);

    if (sorted) {
        cout << "Successfully sorted " << n << " elements using std::vector<int>" << endl;
    } else {
        cout << "Sort failed!" << endl;
    }
    cout << "Time: " << duration.count() / 1000000.0 << " seconds" << endl;

    return 0;
}
