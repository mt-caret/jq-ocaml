open! Core

type t

external compile_internal : string -> string -> (t, string) Result.t = "caml_jq_compile"

external run_internal
  :  t
  -> string
  -> int
  -> (string list, string) Result.t
  = "caml_jq_run"

external teardown : t -> unit = "caml_jq_teardown"

(* Serialize the bindings as a single JSON object {NAME: VALUE, ...} and let
   [jq_compile_args] consume it directly.

   libjq's [jq_compile_args] also accepts the [[{"name": ..., "value": ...},
   ...]] array shape, but in the vendored jq 1.7.1 release that path hits a
   use-after-free / double-free of "name" and "value" key jvs in [args2obj]
   (vendor/jq-1.7.1/src/execute.c). Under glibc the symptom is "malloc():
   unaligned tcache chunk detected" + SIGABRT. jq's own CLI builds args as
   one object via repeated [jv_object_set] in main.c, so the array branch was
   rarely exercised upstream.

   Fixed in jqlang/jq commit 3985b80 (GHSA-gf4g-95wj-4q4r), first released in
   jq-1.8.2rc1. Once we bump the vendored tarball to jq ≥ 1.8.2 both shapes
   will be safe and this branching can collapse to just `Object args`. *)
let args_json = function
  | [] -> ""
  | args -> `Object args |> Jsonaf.Serializer.run
;;

let compile ?(args = []) src =
  match compile_internal src (args_json args) with
  | Ok t -> Ok t
  | Error err ->
    Or_error.error_s
      [%message "jq program failed to compile" ~source:(src : string) (err : string)]
;;

let run_string ?indent ?sort_keys t input =
  let flags = Print_flags.of_options ?indent ?sort_keys () in
  match run_internal t (Jsonaf.Serializer.run input) flags with
  | Ok outputs -> Ok outputs
  | Error err -> Or_error.error_s [%message "jq program failed at runtime" (err : string)]
;;

let run t input =
  match run_internal t (Jsonaf.Serializer.run input) 0 with
  | Error err -> Or_error.error_s [%message "jq program failed at runtime" (err : string)]
  | Ok outputs ->
    Or_error.try_with (fun () -> List.map outputs ~f:(fun s -> Jsonaf.parse s |> ok_exn))
    |> Or_error.tag ~tag:"failed to parse jq output as JSON"
;;

let map_inputs_or_error inputs ~f =
  List.mapi inputs ~f:(fun i input ->
    Or_error.tag_arg (f input) "input index" i [%sexp_of: int])
  |> Or_error.combine_errors
;;

let run_many t inputs = map_inputs_or_error inputs ~f:(run t)

let run_string_many ?indent ?sort_keys t inputs =
  map_inputs_or_error inputs ~f:(run_string ?indent ?sort_keys t)
;;
