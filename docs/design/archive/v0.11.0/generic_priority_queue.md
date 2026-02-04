# ジェネリック PriorityQueue 設計

## 目標
C++の`std::priority_queue`に相当するジェネリックPriorityQueueをCmで実装する。

## API設計

### インターフェース

```cm
// 比較可能な型を示すインターフェース
interface Comparable<T> {
    int compare(T other);  // < = 0, = = 0, > = 0
}

// キューのコアインターフェース
interface Queue<T> {
    void push(T value);
    T pop();
    T peek();
    int size();
    bool is_empty();
}
```

### ジェネリック構造体

```cm
// AVLノード（内部実装）
struct AVLNode<T> {
    T value;
    AVLNode<T>* left;
    AVLNode<T>* right;
    int height;
}

// PriorityQueue本体
struct PriorityQueue<T> {
    AVLNode<T>* root;
    int count;
}
```

### ジェネリック実装

```cm
impl<T> PriorityQueue<T> for Queue<T> {
    void push(T value) {
        self.root = insert_node(self.root, value);
        self.count = self.count + 1;
    }
    
    T pop() {
        // 最小要素を取得して削除
        T result;
        self.root = delete_min(self.root, &result);
        self.count = self.count - 1;
        return result;
    }
    
    T peek() {
        AVLNode<T>* min = find_min(self.root);
        return min->value;
    }
    
    int size() {
        return self.count;
    }
    
    bool is_empty() {
        return self.count == 0;
    }
}
```

## 課題

### self変更問題
現在のインタプリタでは、メソッド内での`self.field`変更が呼び出し元に反映されない問題がある。
これはMIRがselfを一時変数にコピーしてから参照を取るためである。

### 解決策の選択肢

1. **ポinter経由のメソッド呼び出し**（回避策）
   ```cm
   PriorityQueue<int>* pq = new_priority_queue<int>();
   pq->push(10);
   ```

2. **MIR生成修正**（根本解決）
   - selfを直接参照として渡す

3. **関数ベースAPI**（現行動作）
   ```cm
   PriorityQueue<int> pq;
   pqueue_push(&pq, 10);
   ```

## 使用例

```cm
int main() {
    // intのPriorityQueue
    PriorityQueue<int> int_pq;
    int_pq.push(50);
    int_pq.push(10);
    int_pq.push(30);
    
    while (!int_pq.is_empty()) {
        int val = int_pq.pop();  // 10, 30, 50の順
        println("{val}");
    }
    
    // カスタム構造体のPriorityQueue
    struct Task {
        int priority;
        string name;
    }
    
    PriorityQueue<Task> task_pq;
    task_pq.push(Task{priority: 3, name: "Low"});
    task_pq.push(Task{priority: 1, name: "High"});
}
```
