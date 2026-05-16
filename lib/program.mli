(** A compiled jq program. Owns a libjq state; the GC tears it down on
    finalization, or call [teardown] to release it eagerly. *)

open! Core

type t

(** [args] binds program variables: a pair [(name, value)] makes [$name]
    available in the program, like jq's [--arg] / [--argjson]. *)
val compile : ?args:(string * Jsonaf.t) list -> string -> t Or_error.t

(** Result is a list because jq programs are generators. *)
val run : t -> Jsonaf.t -> Jsonaf.t list Or_error.t

(** Like [run], but per-input errors are aggregated rather than
    short-circuiting. *)
val run_many : t -> Jsonaf.t list -> Jsonaf.t list list Or_error.t

(** Returns formatted strings directly, skipping the output-side reparse. See
    [Pretty.to_string] for [indent] / [sort_keys]. *)
val run_string : ?indent:int -> ?sort_keys:bool -> t -> Jsonaf.t -> string list Or_error.t

val run_string_many
  :  ?indent:int
  -> ?sort_keys:bool
  -> t
  -> Jsonaf.t list
  -> string list list Or_error.t

val teardown : t -> unit
