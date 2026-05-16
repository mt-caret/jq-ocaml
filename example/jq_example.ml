(* A miniature [jq] CLI demonstrating the [jq] library. Reads one JSON value
   from stdin, applies the program supplied as an anonymous argument, and
   writes each output value to stdout. *)

open! Core

let or_die = function
  | Ok v -> v
  | Error e ->
    Out_channel.output_string stderr [%string "error: %{Error.to_string_hum e}\n"];
    exit 1
;;

(* Mimics jq's [--arg NAME VALUE] but with a single [NAME=VALUE] token so we
   don't have to teach Command to consume two args per flag. *)
let name_value_arg =
  Command.Arg_type.create (fun s ->
    match String.lsplit2 s ~on:'=' with
    | Some (name, value) -> name, value
    | None -> failwithf "expected NAME=VALUE, got %S" s ())
;;

let command =
  Command.basic
    ~summary:"Apply a jq program to a single JSON value read from stdin"
    (let%map_open.Command program = anon ("PROGRAM" %: string)
     and indent =
       flag
         "-indent"
         (optional_with_default 2 int)
         ~doc:"N spaces of indentation (default 2; 0 for compact)"
     and sort_keys = flag "-S" no_arg ~doc:" sort object keys"
     and string_args =
       flag "-arg" (listed name_value_arg) ~doc:"NAME=VALUE bind $NAME to a string"
     and json_args =
       flag
         "-argjson"
         (listed name_value_arg)
         ~doc:"NAME=VALUE bind $NAME to a JSON-parsed value"
     in
     fun () ->
       let args =
         List.concat
           [ List.map string_args ~f:(fun (n, v) -> n, `String v)
           ; List.map json_args ~f:(fun (n, v) -> n, Jsonaf.parse v |> or_die)
           ]
       in
       let input = Jsonaf.parse (In_channel.input_all In_channel.stdin) |> or_die in
       let p = Jq.Program.compile ~args program |> or_die in
       Jq.Program.run_string ~indent ~sort_keys p input
       |> or_die
       |> List.iter ~f:print_endline)
;;

let () = Command_unix.run command
