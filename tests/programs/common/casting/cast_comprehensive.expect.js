=== Cast Comprehensive Test ===
1. Integer type casts:
  int 42 -> long: 42
  int 42 -> short: 42
  int 42 -> tiny: 42
  long 1000000 -> int: 1000000
2. Float/Int casts:
  double 3.14159 -> int: 3
  float 2.5 -> int: 2
  int 7 -> double: 7
  int 7 -> float: 7
  double 1.23456789 -> float: 1.23456789
  float 9.87 -> double: 9.87
3. Bool/Int casts:
  true -> int: 1
  false -> int: 0
  1 -> bool: true
  0 -> bool: false
4. Pointer casts:
  int* -> void* -> int*: 111
  int* -> long (address): nonzero=true
5. Unsigned casts:
  uint 100 -> int: 100
  int -1 -> uint: converted
6. Char/Int casts:
  'A' -> int: 65
  66 -> char: B
7. Chained casts:
  double 99.9 -> int -> short -> tiny: 99

=== PASS ===
