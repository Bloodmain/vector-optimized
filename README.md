# Vector with small-object and copy-on-write optimizations

*small-object* assumes that vector can store some small amount of elements without dynamic memory allocation.

*copy-on-write* assumes that copying/assignment large vectors does not copy elements by itself, but postpones copying until the moment when the object is applied modifying operation.

Due to SO and COW optimizations some operations have different time complexity and/or provide different exception guarantee:

* Copy constructor and assignment operator has `O(SMALL_SIZE)`, not `O(size)` time complexity.
* If both `a` and `b` are no larger than `SMALL_SIZE`, `swap(a, b)` 
  provides a basic exception safety guarantee, otherwise strong.
* If both `a` and `b` are no larger than `SMALL_SIZE`, `a = b` provides
  basic exception safety guarantee, otherwise strong.
* Non-const
  operations `operator[]`, `data()`, `front()`, `back()`, `pop_back()`, `begin()`,
  `end()` works with O(size) and satisfy the strong
  exception safety guarantee if copying is required for *copy-on-write*, and with
  O(1) and nothrow otherwise.
* As with the standard vector, `reserve` ensures that after
  `reserve(n)` inserts into a vector will not result in reallocations,
  while size <= `n`.