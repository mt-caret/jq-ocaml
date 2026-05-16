(** Internal: build libjq's [jv_dump_string] flag word from the user-facing
    options. *)

open! Core

val of_options : ?indent:int -> ?sort_keys:bool -> unit -> int
