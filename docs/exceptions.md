# Glaze Exceptions

If C++ exceptions are being used in your codebase, it can be cleaner to call functions that throw.

Glaze provides a header `glaze_exceptions.hpp`, which provides an API to use Glaze with exceptions. This header will be disabled if `-fno-exceptions` is specified.

> Glaze functions that throw exceptions use the namespace `glz::ex`

```c++
std::string buffer{};
glz::ex::read_json(obj, buffer); // will throw on a read error
```

KDL provides the same style through `glaze/exceptions/kdl_exceptions.hpp` or the
exceptions umbrella header:

```cpp
glz::ex::read_kdl(obj, buffer);
auto value = glz::ex::read_kdl<my_type>(buffer);
auto text = glz::ex::write_kdl(obj);
glz::ex::write_file_kdl(obj, "settings.kdl", buffer);
glz::ex::read_file_kdl(obj, "settings.kdl", buffer);
```

These functions throw `std::runtime_error` on failure. See
[KDL error handling](kdl.md#error-handling) for non-throwing `error_ctx`,
`expected` and standard error-code alternatives.

