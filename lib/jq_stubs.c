#define CAML_NAME_SPACE
#include <caml/alloc.h>
#include <caml/callback.h>
#include <caml/custom.h>
#include <caml/fail.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>

#include <stdlib.h>
#include <string.h>

#include "jq.h"
#include "jv.h"

/* Tag indices for OCaml's [('a, 'b) Result.t = Ok of 'a | Error of 'b]. */
#define TAG_OK    0
#define TAG_ERROR 1

#define Jq_state_val(v) (*((jq_state **)Data_custom_val(v)))

/* ---------- growable byte buffer for accumulated error messages ---------- */

typedef struct {
  char  *data;
  size_t len;
  size_t cap;
} err_buf;

static void err_buf_append(err_buf *b, const char *s, size_t n) {
  size_t needed = b->len + n + 1;
  if (needed > b->cap) {
    size_t new_cap = b->cap == 0 ? 256 : b->cap;
    while (new_cap < needed) new_cap *= 2;
    char *new_data = realloc(b->data, new_cap);
    if (new_data == NULL) return;
    b->data = new_data;
    b->cap  = new_cap;
  }
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
}

static void err_buf_append_jv(err_buf *b, jv msg) {
  /* [jq_format_error] consumes its argument and returns a freshly-allocated
     string jv (or null on OOM). */
  jv formatted = jq_format_error(msg);
  if (jv_get_kind(formatted) == JV_KIND_STRING) {
    if (b->len > 0) err_buf_append(b, "\n", 1);
    const char *s = jv_string_value(formatted);
    err_buf_append(b, s, strlen(s));
  }
  jv_free(formatted);
}

/* libjq invokes this for every parse / compile / runtime error reported via
   [jq_report_error]. It owns [err] and is required to [jv_free] it. */
static void capture_error(void *data, jv err) {
  err_buf_append_jv((err_buf *)data, err);
}

/* ---------- jq_state custom block ---------- */

static void jq_state_finalize(value v) {
  jq_state *state = Jq_state_val(v);
  if (state != NULL) jq_teardown(&state);
}

static struct custom_operations jq_state_ops = {
  .identifier   = "jq_state",
  .finalize     = jq_state_finalize,
  .compare      = custom_compare_default,
  .hash         = custom_hash_default,
  .serialize    = custom_serialize_default,
  .deserialize  = custom_deserialize_default,
  .compare_ext  = custom_compare_ext_default,
  .fixed_length = custom_fixed_length_default,
};

static value alloc_jq_state(jq_state *state) {
  value v          = caml_alloc_custom(&jq_state_ops, sizeof(jq_state *), 0, 1);
  Jq_state_val(v)  = state;
  return v;
}

/* ---------- compile ----------

   [v_args_json] is either the empty string (no named arguments — equivalent
   to the old single-argument [jq_compile]) or a serialized JSON array of
   [{"name": ..., "value": ...}] objects matching what jq's CLI builds for
   [--arg]/[--argjson]. */

CAMLprim value caml_jq_compile(value v_src, value v_args_json) {
  CAMLparam2(v_src, v_args_json);
  CAMLlocal3(result, ok_payload, err_payload);

  jq_state *state = jq_init();
  if (state == NULL) caml_failwith("jq_init returned NULL");

  err_buf buf = {0};
  jq_set_error_cb(state, capture_error, &buf);

  jv args;
  if (caml_string_length(v_args_json) == 0) {
    args = jv_object();
  } else {
    args = jv_parse(String_val(v_args_json));
    if (!jv_is_valid(args)) {
      err_buf_append_jv(&buf, args); /* consumes [args] */
      jq_set_error_cb(state, NULL, NULL);
      jq_teardown(&state);
      err_payload = caml_copy_string(buf.data != NULL ? buf.data
                                                      : "jq: invalid args JSON");
      free(buf.data);
      result = caml_alloc(1, TAG_ERROR);
      Store_field(result, 0, err_payload);
      CAMLreturn(result);
    }
  }

  int ok = jq_compile_args(state, String_val(v_src), args); /* consumes [args] */

  /* Detach the callback before [buf] goes out of scope — libjq could otherwise
     dereference it later (e.g., during teardown reporting). */
  jq_set_error_cb(state, NULL, NULL);

  if (ok) {
    free(buf.data);
    ok_payload = alloc_jq_state(state);
    result     = caml_alloc(1, TAG_OK);
    Store_field(result, 0, ok_payload);
  } else {
    jq_teardown(&state);
    err_payload = caml_copy_string(buf.data != NULL ? buf.data : "jq: unknown compile error");
    free(buf.data);
    result = caml_alloc(1, TAG_ERROR);
    Store_field(result, 0, err_payload);
  }
  CAMLreturn(result);
}

/* ---------- teardown ---------- */

/* ---------- pretty-print ----------

   Round-trips through [jv_parse] + [jv_dump_string] so callers get jq's
   actual formatter, including its idiosyncratic spacing and number rendering.
   The input is expected to be valid JSON (typically produced by
   [Jsonaf.Serializer.run]); any parse failure here indicates a bug. */

CAMLprim value caml_jq_format(value v_input_str, value v_flags) {
  CAMLparam2(v_input_str, v_flags);
  CAMLlocal1(result);

  jv input = jv_parse(String_val(v_input_str));
  if (!jv_is_valid(input)) {
    jv  formatted = jq_format_error(input);
    char msg[1024];
    snprintf(msg, sizeof(msg), "Jq.Pretty: jv_parse failed: %s",
             jv_get_kind(formatted) == JV_KIND_STRING
               ? jv_string_value(formatted) : "(no message)");
    jv_free(formatted);
    caml_failwith(msg);
  }

  jv dumped = jv_dump_string(input, Int_val(v_flags)); /* consumes [input] */
  if (jv_get_kind(dumped) != JV_KIND_STRING) {
    jv_free(dumped);
    caml_failwith("Jq.Pretty: jv_dump_string did not return a string");
  }

  result = caml_copy_string(jv_string_value(dumped));
  jv_free(dumped);
  CAMLreturn(result);
}

CAMLprim value caml_jq_teardown(value v) {
  CAMLparam1(v);
  jq_state *state = Jq_state_val(v);
  if (state != NULL) {
    jq_teardown(&state);
    Jq_state_val(v) = NULL;
  }
  CAMLreturn(Val_unit);
}

/* ---------- run ----------

   [v_input_str] is a compact JSON serialization of the program's input. The
   output is a [(string list, string) Result.t] where each output element is a
   compact JSON serialization of one jq output value, in the order jq emits
   them. The OCaml side re-parses these into [Jsonaf.t].

   We don't try to thread partial output back when an error happens midstream
   — that matches jq's own CLI, which prints partial output then errors via
   stderr; users wanting both should expose a streaming API later. */

static void free_output_buf(char **outputs, size_t n) {
  for (size_t i = 0; i < n; i++) free(outputs[i]);
  free(outputs);
}

CAMLprim value caml_jq_run(value v_state, value v_input_str, value v_flags) {
  CAMLparam3(v_state, v_input_str, v_flags);
  CAMLlocal4(result, list, cons, str);

  int dump_flags = Int_val(v_flags);

  jq_state *state = Jq_state_val(v_state);
  if (state == NULL) caml_failwith("Jq.Program: state has been torn down");

  err_buf buf = {0};
  jq_set_error_cb(state, capture_error, &buf);

  jv input = jv_parse(String_val(v_input_str));
  if (!jv_is_valid(input)) {
    err_buf_append_jv(&buf, input);
    jq_set_error_cb(state, NULL, NULL);
    str = caml_copy_string(buf.data != NULL ? buf.data
                                            : "jq: failed to parse input");
    free(buf.data);
    result = caml_alloc(1, TAG_ERROR);
    Store_field(result, 0, str);
    CAMLreturn(result);
  }

  jq_start(state, input, 0); /* consumes [input] */

  /* Collect dumped outputs as C strings first; build the OCaml list in one
     pass after the loop, so we don't have to manage CAMLlocal roots inside
     it. */
  char **outputs   = NULL;
  size_t n_outputs = 0;
  size_t cap       = 0;
  int    had_error = 0;

  while (1) {
    jv next = jq_next(state);
    if (!jv_is_valid(next)) {
      if (jv_invalid_has_msg(jv_copy(next))) {
        err_buf_append_jv(&buf, next); /* consumes [next] */
        had_error = 1;
      } else {
        jv_free(next);
      }
      break;
    }
    jv dumped = jv_dump_string(next, dump_flags); /* consumes [next] */
    if (jv_get_kind(dumped) != JV_KIND_STRING) {
      jv_free(dumped);
      err_buf_append(&buf, "jq: failed to serialize output", 30);
      had_error = 1;
      break;
    }
    if (n_outputs >= cap) {
      cap                = cap == 0 ? 8 : cap * 2;
      char **new_outputs = realloc(outputs, cap * sizeof(char *));
      if (new_outputs == NULL) {
        jv_free(dumped);
        err_buf_append(&buf, "jq: out of memory", 17);
        had_error = 1;
        break;
      }
      outputs = new_outputs;
    }
    outputs[n_outputs++] = strdup(jv_string_value(dumped));
    jv_free(dumped);
  }

  jq_set_error_cb(state, NULL, NULL);

  if (had_error || buf.len > 0) {
    free_output_buf(outputs, n_outputs);
    str    = caml_copy_string(buf.data != NULL ? buf.data
                                               : "jq: unknown runtime error");
    free(buf.data);
    result = caml_alloc(1, TAG_ERROR);
    Store_field(result, 0, str);
  } else {
    free(buf.data);
    /* Build [list] head-first by iterating from the back. */
    list = Val_emptylist;
    for (size_t i = n_outputs; i-- > 0;) {
      str  = caml_copy_string(outputs[i]);
      cons = caml_alloc(2, 0);
      Store_field(cons, 0, str);
      Store_field(cons, 1, list);
      list = cons;
    }
    free_output_buf(outputs, n_outputs);
    result = caml_alloc(1, TAG_OK);
    Store_field(result, 0, list);
  }
  CAMLreturn(result);
}
