(lang dune 3.22)

(name jq)

(generate_opam_files true)

(authors "Mt <mtakeda@oeconomia.jp>")

(maintainers "Mt <mtakeda@oeconomia.jp>")

(license MIT)

(package
 (name jq)
 (synopsis "OCaml bindings to libjq for JSON querying and pretty-printing")
 (description
  "OCaml bindings to libjq (vendored). Provides a thin wrapper around the\nlibjq C API for compiling and running jq programs over JSON values.\n\nThe vendored libjq build assumes glibc + Linux for portability of the\nmath/time functions it uses.")
 (depends
  (ocaml
   (>= 5.0))
  (dune
   (>= 3.22))
  core
  core_unix
  jsonaf
  ppx_jane))
