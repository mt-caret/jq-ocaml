(** A compiled jq program. Owns a libjq state; the GC tears it down on
    finalization, or call [teardown] to release it eagerly.

    A [t] wraps a single mutable libjq state. Don't share it across
    concurrent [run] calls or domains, and don't call [teardown] while a
    [run] is in flight — there's no locking inside the binding. Compile one
    [t] per concurrent use site instead. *)

open! Core

type t

(** [args] binds program variables: a pair [(name, value)] makes [$name]
    available in the program, like jq's [--arg] / [--argjson]. Note that
    [$ARGS.named] is not populated — only the individual [$name] bindings. *)
val compile : ?args:(string * Jsonaf.t) list -> string -> t Or_error.t

(** Result is a list because jq programs are generators.

    A program using the [debug] or [stderr] builtins will return the
    expected output but its side-channel write is silently dropped — we
    don't install [jq_set_debug_cb] or [jq_set_stderr_cb]. *)
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
