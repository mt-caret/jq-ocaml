open! Core

external format : string -> int -> string = "caml_jq_format"

let to_string ?indent ?sort_keys value =
  format (Jsonaf.Serializer.run value) (Print_flags.of_options ?indent ?sort_keys ())
;;
