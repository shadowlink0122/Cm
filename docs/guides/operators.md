# 言語ガイド - 演算子オーバーロード

演算子をカスタム型に対して定義できます。

```cm
struct Complex {
    double real;
    double imag;
}

Complex operator+(Complex a, Complex b) {
    return Complex(a.real + b.real, a.imag + b.imag);
}
```
