CXX = /usr/bin/clang++
CXXFLAGS = -std=c++17 -O3 -march=native -Wall

# 個別のベンチマーク
BENCHMARKS = 01_prime 02_fibonacci_recursive 03_fibonacci_iterative 04_array_sort 05_matrix_multiply 06_prime_sieve 07_fibonacci_memoized 06_4d_array 07_struct_array

all: $(BENCHMARKS)

01_prime: 01_prime.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

02_fibonacci_recursive: 02_fibonacci_recursive.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

03_fibonacci_iterative: 03_fibonacci_iterative.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

04_array_sort: 04_array_sort.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

05_matrix_multiply: 05_matrix_multiply.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

06_prime_sieve: 06_prime_sieve.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

07_fibonacci_memoized: 07_fibonacci_memoized.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

06_4d_array: 06_4d_array.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

07_struct_array: 07_struct_array.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(BENCHMARKS) benchmark cpp_results.txt

run_all: all
	@for bench in $(BENCHMARKS); do \
		echo "Running $$bench..."; \
		./$$bench; \
		echo ""; \
	done

.PHONY: all clean run_all