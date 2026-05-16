# jq-ocaml

OCaml bindings to [libjq](https://github.com/jqlang/jq) for querying and
pretty-printing JSON. Vendors jq 1.7.1; no system libjq needed.

The public API is `Jq.Program` (compile a program, run it against `Jsonaf.t`
inputs, optionally with named arguments) and `Jq.Pretty` (render a `Jsonaf.t`
using libjq's own formatter). See `example/jq_example.ml` for a small CLI.

## What's not (yet) bound

- **Oniguruma / regex.** `match`, `test`, `sub`, etc. compile to a stub that
  returns an error message. Adding regex means vendoring oniguruma.
- **Cross-platform builds.** Vendor compile flags assume glibc + Linux.
- **`jq_set_attrs`, library loading, input callbacks.**

## License

The OCaml code is MIT-licensed. The vendored jq source under
`vendor/jq-1.7.1/` is covered by upstream's `COPYING`.
