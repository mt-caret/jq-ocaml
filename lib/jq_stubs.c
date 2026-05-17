#define CAML_NAME_SPACE
#include <caml/alloc.h>
#include <caml/callback.h>
#include <caml/custom.h>
#include <caml/fail.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jq.h"
#include "jv.h"

/* Tag indices for OCaml's [('a, 'b) Result.t = Ok of 'a | Error of 'b]. */
#define TAG_OK    0
#define TAG_ERROR 1

#define Jq_state_val(v) (*((jq_state **)Data_custom_val(v)))

/* ---------- growable byte buffer for accumulated error messages ----------

   Any append failure (allocation failure or size overflow) sets [oom] and
   the buffer becomes a no-op for subsequent appends. Callers should read
   [oom] before [data] so they can surface a deterministic OOM message
   rather than silently truncated output. */

typedef struct {
  char  *data;
  size_t len;
  size_t cap;
  int    oom;
} err_buf;

static void err_buf_append(err_buf *b, const char *s, size_t n) {
  if (b->oom) return;
  if (n > SIZE_MAX - 1 - b->len) {
    b->oom = 1;
    return;
  }
  size_t needed = b->len + n + 1;
  if (needed > b->cap) {
    size_t new_cap = b->cap == 0 ? 256 : b->cap;
    while (new_cap < needed) {
      if (new_cap > SIZE_MAX / 2) {
        b->oom = 1;
        return;
      }
      new_cap *= 2;
    }
    char *new_data = realloc(b->data, new_cap);
    if (new_data == NULL) {
      b->oom = 1;
      return;
    }
    b->data = new_data;
    b->cap  = new_cap;
  }
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
}

static void err_buf_append_str(err_buf *b, const char *s) {
  err_buf_append(b, s, strlen(s));
}

static void err_buf_append_jv(err_buf *b, jv msg) {
  /* [jq_format_error] consumes its argument and returns a freshly-allocated
     string jv (or null on OOM). */
  jv formatted = jq_format_error(msg);
  if (jv_get_kind(formatted) == JV_KIND_STRING) {
    if (b->len > 0) err_buf_append_str(b, "\n");
    err_buf_append_str(b, jv_string_value(formatted));
  }
  jv_free(formatted);
}

/* Caller-owned constant fallbacks read from one place so callers never
   confuse "we tried to capture an error but ran out of memory" with "no
   error was produced". */
static const char *err_buf_message(const err_buf *b, const char *fallback) {
  if (b->oom) return "jq: out of memory while capturing error";
  if (b->data != NULL && b->len > 0) return b->data;
  return fallback;
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

   [v_args_json] is either "" (no named arguments) or a serialized JSON
   *object* mapping NAME -> VALUE — see the comment in lib/program.ml for
   why we use the object shape rather than the documented
   [[{"name": ..., "value": ...}, ...]] array shape. */

CAMLprim value caml_jq_compile(value v_src, value v_args_json) {
  CAMLparam2(v_src, v_args_json);
  CAMLlocal3(result, payload, err_payload);

  /* Allocate the custom block with NULL state first. Any subsequent OCaml
     allocation that raises (Out_of_memory) will leave [payload]
     unreachable; the finalizer will run on the next GC and tear down
     whatever state is by then stored, with no leak. */
  payload = alloc_jq_state(NULL);

  jq_state *state = jq_init();
  if (state == NULL) caml_failwith("jq_init returned NULL");
  Jq_state_val(payload) = state;

  err_buf buf = {0};
  jq_set_error_cb(state, capture_error, &buf);

  jv args;
  int args_invalid = 0;
  if (caml_string_length(v_args_json) == 0) {
    args = jv_object();
  } else {
    args = jv_parse(String_val(v_args_json));
    if (!jv_is_valid(args)) {
      err_buf_append_jv(&buf, args); /* consumes [args] */
      args_invalid = 1;
    }
  }

  int ok = 0;
  if (!args_invalid) {
    ok = jq_compile_args(state, String_val(v_src), args); /* consumes [args] */
  }

  jq_set_error_cb(state, NULL, NULL);

  if (ok) {
    free(buf.data);
    result = caml_alloc(1, TAG_OK);
    Store_field(result, 0, payload);
  } else {
    /* Tear down eagerly so the failed state is released without waiting for
       a GC pass. The custom block stays around but its finalizer becomes a
       no-op once we NULL the slot. */
    jq_teardown(&state);
    Jq_state_val(payload) = NULL;
    err_payload = caml_copy_string(err_buf_message(&buf, "jq: unknown compile error"));
    free(buf.data);
    result = caml_alloc(1, TAG_ERROR);
    Store_field(result, 0, err_payload);
  }
  CAMLreturn(result);
}

/* ---------- teardown ---------- */

CAMLprim value caml_jq_teardown(value v) {
  CAMLparam1(v);
  jq_state *state = Jq_state_val(v);
  if (state != NULL) {
    jq_teardown(&state);
    Jq_state_val(v) = NULL;
  }
  CAMLreturn(Val_unit);
}

/* ---------- pretty-print ----------

   Round-trips through [jv_parse] + [jv_dump_string] so callers get jq's
   actual formatter, including its idiosyncratic spacing and number
   rendering. The input is expected to be valid JSON (typically produced by
   [Jsonaf.Serializer.run]); a parse failure here indicates a bug. */

CAMLprim value caml_jq_format(value v_input_str, value v_flags) {
  CAMLparam2(v_input_str, v_flags);
  CAMLlocal1(result);

  jv input = jv_parse(String_val(v_input_str));
  if (!jv_is_valid(input)) {
    /* Copy the message into a stack buffer before freeing jv resources so
       caml_failwith doesn't longjmp past our cleanup. */
    char msg[1024];
    jv   formatted = jq_format_error(input); /* consumes input */
    snprintf(msg, sizeof(msg), "Jq.Pretty: jv_parse failed: %s",
             jv_get_kind(formatted) == JV_KIND_STRING ? jv_string_value(formatted) : "(no message)");
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

/* ---------- run ----------

   [v_input_str] is a compact JSON serialization of the program's input. The
   output is a [(string list, string) Result.t] where each output element is
   a compact JSON serialization of one jq output value, in the order jq
   emits them. The OCaml side re-parses these into [Jsonaf.t].

   Partial output isn't surfaced on error — that matches jq's CLI, which
   prints partial output to stdout then errors to stderr. A streaming API
   would expose both. */

/* Append jq's halt-state info (from [halt] / [halt_error]) to [buf].
   Returns 1 if the halt should be reported as an error, 0 for plain [halt]
   with no exit code. */
static int append_halt_info(err_buf *buf, jq_state *state) {
  if (!jq_halted(state)) return 0;
  jv exit_code = jq_get_exit_code(state);
  if (!jv_is_valid(exit_code)) {
    /* Plain [halt] — no exit code stored. Treat as successful empty
       output, matching jq's CLI behavior. */
    jv_free(exit_code);
    jv_free(jq_get_error_message(state));
    return 0;
  }
  char header[64];
  if (jv_get_kind(exit_code) == JV_KIND_NUMBER) {
    snprintf(header, sizeof(header), "jq: halt_error: exit code %g",
             jv_number_value(exit_code));
  } else {
    snprintf(header, sizeof(header), "jq: halt_error");
  }
  jv_free(exit_code);
  if (buf->len > 0) err_buf_append_str(buf, "\n");
  err_buf_append_str(buf, header);

  jv msg = jq_get_error_message(state);
  if (jv_get_kind(msg) == JV_KIND_STRING) {
    err_buf_append_str(buf, ": ");
    err_buf_append_str(buf, jv_string_value(msg));
  } else if (jv_is_valid(msg) && jv_get_kind(msg) != JV_KIND_NULL) {
    /* Non-string, non-null message: dump as JSON, mirroring jq's CLI. */
    jv dumped = jv_dump_string(jv_copy(msg), 0);
    if (jv_get_kind(dumped) == JV_KIND_STRING) {
      err_buf_append_str(buf, ": ");
      err_buf_append_str(buf, jv_string_value(dumped));
    }
    jv_free(dumped);
  }
  jv_free(msg);
  return 1;
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
    str = caml_copy_string(err_buf_message(&buf, "jq: failed to parse input"));
    free(buf.data);
    result = caml_alloc(1, TAG_ERROR);
    Store_field(result, 0, str);
    CAMLreturn(result);
  }

  jq_start(state, input, 0); /* consumes [input] */

  /* Collect dumped outputs as C strings first; build the OCaml list in one
     pass after the loop so we don't have to manage CAMLlocal roots inside
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
      err_buf_append_str(&buf, "jq: failed to serialize output");
      had_error = 1;
      break;
    }
    if (n_outputs >= cap) {
      size_t new_cap = cap == 0 ? 8 : cap * 2;
      if (new_cap < cap || new_cap > SIZE_MAX / sizeof(char *)) {
        jv_free(dumped);
        err_buf_append_str(&buf, "jq: too many outputs");
        had_error = 1;
        break;
      }
      char **new_outputs = realloc(outputs, new_cap * sizeof(char *));
      if (new_outputs == NULL) {
        jv_free(dumped);
        err_buf_append_str(&buf, "jq: out of memory");
        had_error = 1;
        break;
      }
      outputs = new_outputs;
      cap     = new_cap;
    }
    char *copy = strdup(jv_string_value(dumped));
    jv_free(dumped);
    if (copy == NULL) {
      err_buf_append_str(&buf, "jq: out of memory");
      had_error = 1;
      break;
    }
    outputs[n_outputs++] = copy;
  }

  /* If jq halted (halt / halt_error), turn that into an error too. */
  if (append_halt_info(&buf, state)) had_error = 1;

  jq_set_error_cb(state, NULL, NULL);

  if (had_error || buf.len > 0 || buf.oom) {
    for (size_t i = 0; i < n_outputs; i++) free(outputs[i]);
    free(outputs);
    str    = caml_copy_string(err_buf_message(&buf, "jq: unknown runtime error"));
    free(buf.data);
    result = caml_alloc(1, TAG_ERROR);
    Store_field(result, 0, str);
  } else {
    free(buf.data);
    /* Build [list] head-first by iterating from the back. Free each C
       string as we consume it so a mid-build OCaml OOM only leaks the
       unprocessed tail. */
    list = Val_emptylist;
    for (size_t i = n_outputs; i-- > 0;) {
      char *output_str = outputs[i];
      outputs[i]       = NULL;
      str              = caml_copy_string(output_str);
      free(output_str);
      cons = caml_alloc(2, 0);
      Store_field(cons, 0, str);
      Store_field(cons, 1, list);
      list = cons;
    }
    free(outputs);
    result = caml_alloc(1, TAG_OK);
    Store_field(result, 0, list);
  }
  CAMLreturn(result);
}
