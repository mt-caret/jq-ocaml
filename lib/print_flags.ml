open! Core

let pretty = 1
let sorted = 8
let tab = 64

let indent_bits = function
  | 0 -> 0
  | n when n < 0 || n > 7 -> tab lor pretty
  | n -> pretty lor (n lsl 8)
;;

let of_options ?(indent = 2) ?(sort_keys = false) () =
  indent_bits indent
  lor
  match sort_keys with
  | true -> sorted
  | false -> 0
;;
