open! Core

let compile_exn src = Jq.Program.compile src |> ok_exn
let json s = Jsonaf.parse s |> ok_exn

let run_and_print program_src input_json =
  let p = compile_exn program_src in
  let result = Jq.Program.run p (json input_json) in
  Jq.Program.teardown p;
  print_s [%sexp (result : Jsonaf.t list Or_error.t)]
;;

let%expect_test "compile a trivial identity program" =
  let p = compile_exn "." in
  Jq.Program.teardown p;
  print_endline "ok";
  [%expect {| ok |}]
;;

let%expect_test "compilation errors are surfaced" =
  let result = Jq.Program.compile ". | )" in
  print_s [%sexp (result : (_, Error.t) Result.t)];
  [%expect
    {|
    (Error
     ("jq program failed to compile" (source ". | )")
      (err
        "jq: error: syntax error, unexpected INVALID_CHARACTER (Unix shell quoting issues?) at <top-level>, line 1:\
       \n. | )    \
       \njq: 1 compile error")))
    |}]
;;

let%expect_test "unknown function is reported" =
  let result = Jq.Program.compile "no_such_function" in
  print_s [%sexp (result : (_, Error.t) Result.t)];
  [%expect
    {|
    (Error
     ("jq program failed to compile" (source no_such_function)
      (err
        "jq: error: no_such_function/0 is not defined at <top-level>, line 1:\
       \nno_such_function\
       \njq: 1 compile error")))
    |}]
;;

let%expect_test "identity returns the input unchanged" =
  run_and_print "." {|{"a": 1, "b": [true, null, "x"]}|};
  [%expect {| (Ok ((Object ((a (Number 1)) (b (Array (True Null (String x)))))))) |}]
;;

let%expect_test "field access" =
  run_and_print ".b" {|{"a": 1, "b": [true, null, "x"]}|};
  [%expect {| (Ok ((Array (True Null (String x))))) |}]
;;

let%expect_test "iteration produces multiple outputs" =
  run_and_print ".[]" "[1, 2, 3]";
  [%expect {| (Ok ((Number 1) (Number 2) (Number 3))) |}]
;;

let%expect_test "pipeline with select and projection" =
  run_and_print
    ".items[] | select(.qty > 0) | {name, qty}"
    {|{"items": [
        {"name": "a", "qty": 1},
        {"name": "b", "qty": 0},
        {"name": "c", "qty": 3}
      ]}|};
  [%expect
    {|
    (Ok
     ((Object ((name (String a)) (qty (Number 1))))
      (Object ((name (String c)) (qty (Number 3))))))
    |}]
;;

let%expect_test "runtime type error" =
  run_and_print ".foo" "[1, 2, 3]";
  [%expect
    {|
    (Error
     ("jq program failed at runtime"
      (err "jq: error: Cannot index array with string \"foo\"")))
    |}]
;;

let%expect_test "explicit error() call" =
  run_and_print {|error("something went wrong")|} "null";
  [%expect
    {|
    (Error
     ("jq program failed at runtime" (err "jq: error: something went wrong")))
    |}]
;;

let%expect_test "run_string: pretty output by default" =
  let p = compile_exn ".items[] | .name" in
  let result = Jq.Program.run_string p (json {|{"items":[{"name":"a"},{"name":"b"}]}|}) in
  Jq.Program.teardown p;
  print_s [%sexp (result : string list Or_error.t)];
  [%expect {| (Ok ("\"a\"" "\"b\"")) |}]
;;

let%expect_test "run_string: compact + sorted" =
  let p = compile_exn "." in
  let result =
    Jq.Program.run_string ~indent:0 ~sort_keys:true p (json {|{"z":1,"a":2}|})
  in
  Jq.Program.teardown p;
  print_s [%sexp (result : string list Or_error.t)];
  [%expect {| (Ok ("{\"a\":2,\"z\":1}")) |}]
;;

let%expect_test "run_string: surfaces runtime errors like [run]" =
  let p = compile_exn ".foo" in
  let result = Jq.Program.run_string p (json "[1, 2, 3]") in
  Jq.Program.teardown p;
  print_s [%sexp (result : string list Or_error.t)];
  [%expect
    {|
    (Error
     ("jq program failed at runtime"
      (err "jq: error: Cannot index array with string \"foo\"")))
    |}]
;;

let%expect_test "pretty-print: default indent" =
  print_endline (Jq.Pretty.to_string (json {|{"b": 1, "a": [2, 3]}|}));
  [%expect
    {|
    {
      "b": 1,
      "a": [
        2,
        3
      ]
    }
    |}]
;;

let%expect_test "pretty-print: compact" =
  print_endline (Jq.Pretty.to_string ~indent:0 (json {|{"b": 1, "a": [2, 3]}|}));
  [%expect {| {"b":1,"a":[2,3]} |}]
;;

let%expect_test "pretty-print: sorted keys" =
  print_endline (Jq.Pretty.to_string ~sort_keys:true (json {|{"z": 1, "a": 2, "m": 3}|}));
  [%expect
    {|
    {
      "a": 2,
      "m": 3,
      "z": 1
    }
    |}]
;;

let%expect_test "pretty-print: 4-space indent" =
  print_endline (Jq.Pretty.to_string ~indent:4 (json {|[1, [2, 3]]|}));
  [%expect
    {|
    [
        1,
        [
            2,
            3
        ]
    ]
    |}]
;;

let%expect_test "named arguments: --arg-style string binding" =
  let p =
    Jq.Program.compile ~args:[ "user", `String "alice" ] {|. + {user: $user}|} |> ok_exn
  in
  let result = Jq.Program.run p (json "{}") in
  Jq.Program.teardown p;
  print_s [%sexp (result : Jsonaf.t list Or_error.t)];
  [%expect {| (Ok ((Object ((user (String alice)))))) |}]
;;

let%expect_test "named arguments: --argjson-style JSON binding" =
  let p =
    Jq.Program.compile
      ~args:[ "port", `Number "8080"; "tags", `Array [ `String "a"; `String "b" ] ]
      {|{port: $port, tags: $tags}|}
    |> ok_exn
  in
  let result = Jq.Program.run p (json "null") in
  Jq.Program.teardown p;
  print_s [%sexp (result : Jsonaf.t list Or_error.t)];
  [%expect
    {| (Ok ((Object ((port (Number 8080)) (tags (Array ((String a) (String b)))))))) |}]
;;

let%expect_test "named arguments: undeclared variable fails compile" =
  let result = Jq.Program.compile {|$missing|} in
  print_s [%sexp (result : (_, Error.t) Result.t)];
  [%expect
    {|
    (Error
     ("jq program failed to compile" (source $missing)
      (err
        "jq: error: $missing is not defined at <top-level>, line 1:\
       \n$missing\
       \njq: 1 compile error")))
    |}]
;;

let%expect_test "run_many: per-input outputs" =
  let p = compile_exn ".[]" in
  let result = Jq.Program.run_many p [ json "[1, 2]"; json "[3]"; json "[]" ] in
  Jq.Program.teardown p;
  print_s [%sexp (result : Jsonaf.t list list Or_error.t)];
  [%expect {| (Ok (((Number 1) (Number 2)) ((Number 3)) ())) |}]
;;

let%expect_test "run_many: aggregates errors from multiple inputs" =
  let p = compile_exn ".foo" in
  let result = Jq.Program.run_many p [ json {|{"foo": 1}|}; json "[1]"; json "[2]" ] in
  Jq.Program.teardown p;
  print_s [%sexp (result : Jsonaf.t list list Or_error.t)];
  [%expect
    {|
    (Error
     (("input index" 1
       ("jq program failed at runtime"
        (err "jq: error: Cannot index array with string \"foo\"")))
      ("input index" 2
       ("jq program failed at runtime"
        (err "jq: error: Cannot index array with string \"foo\"")))))
    |}]
;;

let%expect_test "run_string_many: per-input formatted outputs" =
  let p = compile_exn ".[]" in
  let result = Jq.Program.run_string_many ~indent:0 p [ json "[1, 2]"; json {|["a"]|} ] in
  Jq.Program.teardown p;
  print_s [%sexp (result : string list list Or_error.t)];
  [%expect {| (Ok ((1 2) ("\"a\""))) |}]
;;

let%expect_test "program can be re-run on different inputs" =
  let p = compile_exn ".a + .b" in
  let result1 = Jq.Program.run p (json {|{"a": 1, "b": 2}|}) in
  let result2 = Jq.Program.run p (json {|{"a": "hello ", "b": "world"}|}) in
  Jq.Program.teardown p;
  print_s [%sexp (result1 : Jsonaf.t list Or_error.t)];
  print_s [%sexp (result2 : Jsonaf.t list Or_error.t)];
  [%expect
    {|
    (Ok ((Number 3)))
    (Ok ((String "hello world")))
    |}]
;;
