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
  "A Jane-Street-style OCaml binding to libjq (vendored). Provides a thin\nwrapper around the libjq C API for compiling and running jq programs over\nJSON values.")
 (depends
  (ocaml
   (>= 5.0))
  (dune
   (>= 3.22))
  core
  core_unix
  jsonaf
  ppx_jane))
