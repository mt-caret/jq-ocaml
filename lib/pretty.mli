(** Render a [Jsonaf.t] using libjq's own formatter — the output matches
    jq's CLI, including its number rendering and separator spacing. *)

open! Core

(** [indent] defaults to [2]; [0] is compact, single-line; values above [7]
    switch to tab indentation. *)
val to_string : ?indent:int -> ?sort_keys:bool -> Jsonaf.t -> string
