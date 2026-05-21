These cases are expected to fail semantic checking because static local
initializers must be compile-time constant expressions.

They live below `compile_fail/` so the ordinary positive test runner does not
try to execute them as normal runtime tests.
