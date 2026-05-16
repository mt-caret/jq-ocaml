# AGENTS.md

## Layout

- `lib/` — the `jq` library.
  - `program.ml(i)` — compile + run.
  - `pretty.ml(i)` — render via libjq's formatter.
  - `print_flags.ml(i)` — internal: build the `jv_dump_string` flag word.
  - `jq_stubs.c` — C glue.
  - `jq.ml` — re-exports.
- `vendor/jq-1.7.1/` — the unmodified jq 1.7.1 release tarball, built as
  vendored foreign libraries via dune. No autotools/bison/flex needed because
  the tarball ships pre-generated `parser.c`/`lexer.c`.
- `vendor/jq-1.7.1/src/dune` lists the `LIBJQ_SRC` files and the long
  `-DHAVE_*` flag list (matches what autoconf would have produced).
- `example/jq_example.ml` — a small `jq`-like CLI demonstrating the library.
- `test/` — `ppx_expect` inline tests.

## Build, test, format

The project uses [dune package management](https://dune.readthedocs.io/en/stable/howto/dune-package-management.html).

```
dune pkg lock      # first time, or after dune-project deps change
dune build
dune runtest
dune tool install ocamlformat   # one-time
dune fmt
```

## Vendor flags worth knowing

- `-D_GNU_SOURCE` is required on glibc for `drem`, `exp10`, `timegm`,
  `lgamma_r`, `significand`.
- `IEEE_8087` (little-endian) is needed by `jv_dtoa.c`.
- `HAVE_LIBONIG` is intentionally **not** set — regex builtins compile to an
  error stub.
- `builtin.inc` is generated from `builtin.jq` by a small `sed` rule, the
  same transform autotools does.

## libjq quirks

- `jq_compile_args` accepts an args array, but its array path in
  `args2obj` (`vendor/jq-1.7.1/src/execute.c`) double-frees the `"name"` /
  `"value"` key jvs. Always pass args as a single object instead — see the
  comment in `lib/program.ml`. Fixed upstream in jq-1.8.2rc1
  (jqlang/jq@3985b80, GHSA-gf4g-95wj-4q4r); the workaround can be dropped
  once the vendored tarball is bumped to jq ≥ 1.8.2.
- `jq_msg_cb` callbacks take ownership of the `jv` they receive and must
  `jv_free` it. We funnel through `jq_format_error`, which consumes the input
  and returns a freshly-allocated string jv.
