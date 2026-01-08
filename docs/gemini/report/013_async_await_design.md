# Cm言語におけるAsync/Await/Futureの詳細設計

## 1. 概要と設計思想

本ドキュメントでは、Cm言語に `async`/`await` 構文および `Future` 型を導入するための詳細設計を提案します。Cm言語の「C++ライクな構文とRustライクな安全性・ゼロコスト抽象化」という哲学に基づき、**Stackless Coroutine（ステートマシン変換）** を採用します。これにより、ガベージコレクション（GC）を必要とせず、ベアメタル環境でも動作可能な非同期処理を実現します。

### 1.1 目標
1.  **Zero-cost**: `async` を使用しないコードに対するオーバーヘッドをゼロにする。使用する場合も、手書きのステートマシンと同等の性能を目指す。
2.  **No-GC**: ヒープ割り当てを強制せず、可能な限りスタックまたは静的領域で動作可能にする。
3.  **Bare Metal Friendly**: OSのない環境（組み込み、WASM）でも、最小限のランタイム（Executor）を実装することで動作可能にする。

## 2. 言語仕様 (Syntax & Semantics)

### 2.1 `Future` インターフェース

非同期計算の核心となる `Future` トレイトを `core` ライブラリに定義します。

```cm
// lib/core/future.cm

// 実行コンテキスト（Wakerへのアクセスなどを提供）
struct Context {
    // 内部実装依存（不透明ポインタなど）
    void* raw_waker;
    void (*wake_fn)(void*);
}

// ポーリング結果
enum Poll<T> {
    Ready(T),
    Pending
}

// Futureインターフェース
interface Future<T> {
    Poll<T> poll(Context* cx);
}
```

### 2.2 `async fn` 構文

関数定義に `async` キーワードを付与することで、その関数は即座に実行されず、`Future` を実装した匿名の構造体を返します。

```cm
// 定義
async int fetch_data(string url) {
    // ...
    return 42;
}

// 呼び出し（実行はされない）
let future = fetch_data("https://example.com"); 
// future の型は impl Future<int>
```

### 2.3 `await` 構文

`Future` の完了を待ち、結果を取り出すための構文です。`async` 関数またはブロック内でのみ使用可能です。

```cm
async void process() {
    int data = fetch_data("...").await;
    println("Data: {}", data);
}
```

### 2.4 `async` ブロック

関数全体ではなく、コードの一部を非同期タスクとして定義します。

```cm
let task = async {
    let x = 10;
    something_async().await;
    return x;
};
```

## 3. コンパイラ実装 (Implementation)

### 3.1 ステートマシン変換 (State Machine Transformation)

コンパイラ（MIR Loweringフェーズ）は、`async` 関数を「状態を持つ構造体（ステートマシン）」と「`poll` メソッドの実装」に変換します。

#### 変換前
```cm
async int example(int arg) {
    int x = arg * 2;
    step1().await;
    int y = x + 1;
    step2(y).await;
    return y;
}
```

#### 変換後（擬似コード）
コンパイラは以下のような構造体と実装を自動生成します。

```cm
// 生成されるステートマシン構造体
struct ExampleFuture {
    // 状態管理
    int __state; // 0: 初期, 1: step1待機中, 2: step2待機中, 3: 完了

    // 引数とローカル変数の保存領域
    int arg;
    int x;     // awaitを跨いで生存するためフィールドに昇格
    int y;     // 同上

    // 子Futureの保存
    Step1Future __sub_future1;
    Step2Future __sub_future2;
}

impl ExampleFuture for Future<int> {
    Poll<int> poll(Context* cx) {
        switch (self.__state) {
            case 0: {
                self.x = self.arg * 2;
                self.__sub_future1 = step1(); // 子Future生成
                self.__state = 1;
                // fallthrough to case 1
            }
            case 1: {
                // 子Futureをポーリング
                match (self.__sub_future1.poll(cx)) {
                    Poll::Ready(_) => {
                        self.y = self.x + 1;
                        self.__sub_future2 = step2(self.y);
                        self.__state = 2;
                        // goto case 2 (loop)
                    },
                    Poll::Pending => return Poll::Pending,
                }
            }
            case 2: {
                match (self.__sub_future2.poll(cx)) {
                    Poll::Ready(_) => {
                        self.__state = 3;
                        return Poll::Ready(self.y);
                    },
                    Poll::Pending => return Poll::Pending,
                }
            }
            case 3: {
                panic("Future polled after completion");
            }
        }
    }
}
```

### 3.2 メモリ配置と自己参照 (Pinning)

`async` ブロック内で参照（`&T`）を使用する場合、ステートマシンがメモリ上で移動（ムーブ）すると参照が無効になる問題（自己参照構造体問題）が発生します。

Rustでは `Pin<P>` 型でこれを解決していますが、Cmでは複雑さを避けるため、以下の制約と機能で対応します。

1.  **制約**: `await` ポイントを跨いで「自身のフィールドへの参照」を保持することを禁止する（借用チェッカーで検出）。
2.  **`fixed` キーワード（提案）**: `async` 関数内で特定のアドレスに固定が必要な変数を宣言する。Executorはこの変数をヒープに確保するか、移動しないことを保証するAPIを通じて利用する。

## 4. ベアメタル（No-std）対応

OSのスレッドやヒープアロケータがない環境でも動作させるための設計です。

### 4.1 Executorの分離

言語コアは `Future` の定義とステートマシン生成のみを担当します。タスクをスケジュールし、`poll` を呼び出す「Executor」はライブラリとして提供（またはユーザーが実装）します。

### 4.2 割り込み駆動Executor

ベアメタル環境では、ハードウェア割り込みをトリガーとして `poll` を行うシンプルなExecutorが有用です。

```cm
// 組み込み向けExecutorの例（擬似コード）
static Task* global_task = null;

// 割り込みハンドラ
extern "C" void UART_IRQHandler() {
    if (global_task != null) {
        // コンテキスト（ダミーまたはWaker付き）を作成
        Context cx = create_context();
        if (global_task->poll(&cx) == Poll::Ready) {
            global_task = null; // 完了
        }
    }
}

void run_task(Task* t) {
    global_task = t;
    // 最初のpoll（キックオフ）
    UART_IRQHandler();
    while (global_task != null) {
        wait_for_interrupt(); // 省電力待機
    }
}
```

### 4.3 静的割り当て

`async fn` が生成する構造体のサイズはコンパイル時に確定します（`sizeof(ExampleFuture)`）。これにより、ヒープを使わずにグローバル変数やスタック上にFutureを配置して実行することが可能です。

```cm
// 静的領域にFutureを確保
static ExampleFuture task_instance;

int main() {
    // 初期化（引数をセット）
    task_instance = example(42);
    
    // 実行
    block_on(&task_instance);
    return 0;
}
```

## 5. 既存機能との整合性と修正

### 5.1 所有権システムとの統合

v0.11.0で導入された所有権システムは `async` と相性が良いです。
*   `async` ブロックにキャプチャされる変数は、デフォルトで **Move（所有権移動）** されます。
*   明示的に参照をキャプチャしたい場合は、ブロックの外で参照変数を作成します。

```cm
let data = BigStruct();
let future = async {
    // data はここに Move される。
    // ステートマシン構造体のフィールドとして data が含まれる。
    process(data);
};
```

### 5.2 トレイト（インターフェース）での `async`

現在のインターフェースは `async fn` を直接サポートしません（ステートマシンの型が隠蔽されるため）。
これを解決するために、**Associated Types（関連型）** または **Type Erasure（型消去）** の導入を検討する必要があります。

*   **静的ディスパッチ（推奨）**: `impl Trait` 相当の機能で、具体的なFuture型を隠蔽しつつ静的に解決する。
*   **動的ディスパッチ**: `Box<dyn Future<T>>` のように、ヒープ割り当てと仮想関数テーブルを使って型を消去する（`std` 環境向け）。

## 6. ロードマップ

1.  **Phase 1**: `core::future` モジュールと `Future` トレイトの定義。
2.  **Phase 2**: パーサーとASTの拡張（`async`, `await`）。
3.  **Phase 3**: MIRにおけるステートマシン変換パスの実装。基本的な変数昇格の実装。
4.  **Phase 4**: 借用チェッカーの `async` 対応（`await` を跨ぐ借用のライフタイム検証）。
5.  **Phase 5**: ベアメタル向けサンプルExecutorとデモの実装。

この設計により、Cm言語はシステムプログラミング言語としての性能要件を満たしつつ、現代的な非同期プログラミングをサポート可能になります。
