# ---- Correctness tests (Catch2) -------------------------------------------
add_executable(test_print_in_order test/correctness.cpp)
target_link_libraries(test_print_in_order PRIVATE engine Catch2::Catch2WithMain)
catch_discover_tests(test_print_in_order)

# ---- Statistical benchmark (Catch2, optimized) ----------------------------
add_executable(bench_print_in_order bench/benchmark.cpp)
target_link_libraries(bench_print_in_order PRIVATE engine Catch2::Catch2WithMain)
target_compile_options(bench_print_in_order PRIVATE -O3 -DNDEBUG)

# ---- Per-solution optimized runners ---------------------------------------
# Each solution is compiled into its OWN -O3 -march=native binary. This is the
# "engine compiles a solution optimized and runs it" piece; scripts/compare.sh
# builds all of them and runs each in isolation.
function(add_runner name header type)
  add_executable(runner_${name} runner/runner.cpp)
  target_link_libraries(runner_${name} PRIVATE engine)
  target_compile_options(runner_${name} PRIVATE -O3 -DNDEBUG -march=native)
  target_compile_definitions(runner_${name} PRIVATE
    SOLUTION_HEADER="${header}"
    SOLUTION_TYPE=${type}
    SOLUTION_NAME="${name}")
endfunction()

add_runner(condition_variable problems/print-in-order/solutions/ConditionVariable.hpp pio::FooCV)
add_runner(atomic_wait        problems/print-in-order/solutions/AtomicWait.hpp        pio::FooAtomicWait)
add_runner(spinlock           problems/print-in-order/solutions/Spinlock.hpp          pio::FooSpin)
add_runner(spinlock_pause     problems/print-in-order/solutions/SpinlockPause.hpp     pio::FooSpinPause)
add_runner(spinlock_yield     problems/print-in-order/solutions/SpinlockYield.hpp     pio::FooSpinYield)

# Convenience target that the compare script drives.
add_custom_target(runners
  DEPENDS runner_condition_variable runner_atomic_wait runner_spinlock
          runner_spinlock_pause runner_spinlock_yield)
