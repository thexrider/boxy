/*
 * Boxy 1.0 - a small programming language for kids.
 * Copyright (C) 2026 The_X_Rider <the_x_rider@proton.me>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Build: cc boxy.c -o boxy -pthread -lm
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BOXY_VERSION "1.0"

#define MAX_LINES        4096
#define MAX_LINE         1024
#define MAX_NAME         64
#define MAX_TEXT         1024
#define MAX_LIST_ITEM    256
#define MAX_SYMBOLS      128
#define MAX_THREADS      32
#define MAX_LOCKS        64
#define MAX_BLOCK        512
#define MAX_LIST_ITEMS   32
#define MAX_FUNCTIONS    32
#define MAX_PARAMS       8
#define MAX_CONSTANTS    64
#define MAX_OPEN_FILES   16
#define MAX_SOCKETS      32
#define SOCKET_BUFFER    4096
#define THREAD_STACK     (4 * 1024 * 1024)

typedef enum {
    VALUE_NONE,
    VALUE_NUMBER,
    VALUE_TEXT,
    VALUE_BOOL,
    VALUE_LIST
} ValueType;

typedef struct {
    ValueType type;
    double number;
    char text[MAX_TEXT];
    int boolean;
    char list[MAX_LIST_ITEMS][MAX_LIST_ITEM];
    int list_count;
} Value;

typedef struct {
    int line_number;
    char text[MAX_LINE];
} Line;

typedef struct { char name[MAX_NAME]; Value value; } Var;
typedef struct { char name[MAX_NAME]; Value value; int borrowed; int alive; int line_born; } Box;
typedef struct { char name[MAX_NAME]; char target[MAX_NAME]; int line_born; } Arrow;
typedef struct { char name[MAX_NAME]; pthread_mutex_t mutex; int initialized; } NamedLock;
typedef struct { char name[MAX_NAME]; Value value; } Constant;
typedef struct {
    char name[MAX_NAME];
    char params[MAX_PARAMS][MAX_NAME];
    int param_count;
    Line body[MAX_BLOCK];
    int body_count;
} Function;
typedef struct { char alias[MAX_NAME]; FILE *fp; int open; int line_born; } OpenFile;
typedef struct { char alias[MAX_NAME]; int fd; int is_listening; int open; int line_born; } Socket;

typedef struct Interpreter Interpreter;

typedef struct {
    Interpreter *interpreter;
    Line lines[MAX_BLOCK];
    int count;
    char name[MAX_NAME];
} ThreadJob;

typedef struct {
    char name[MAX_NAME];
    pthread_t thread;
    int active;
    ThreadJob *job;
    int line_born;
} Helper;

struct Interpreter {
    Var vars[MAX_SYMBOLS];
    int var_count;

    Box boxes[MAX_SYMBOLS];
    int box_count;

    Arrow arrows[MAX_SYMBOLS];
    int arrow_count;

    NamedLock locks[MAX_LOCKS];
    int lock_count;

    Helper threads[MAX_THREADS];
    int thread_count;

    Function funcs[MAX_FUNCTIONS];
    int func_count;

    Constant constants[MAX_CONSTANTS];
    int const_count;

    OpenFile open_files[MAX_OPEN_FILES];
    int file_count;

    Socket sockets[MAX_SOCKETS];
    int socket_count;

    int returning;
    int loop_break;
    int loop_skip;
    int is_function;
    int visual;

    pthread_mutex_t state_mutex;
};

/* help text */

static const char *HELP_TEXT =
"Boxy " BOXY_VERSION "\n"
"A friendly little programming language for kids.\n\n"
"Usage:\n"
"  boxy run program.bx        run a program\n"
"  boxy lesson 1              run lesson 1 from lessons/\n"
"  boxy --help                this help\n"
"  boxy run program.bx --quiet     skip the banner\n"
"  boxy run program.bx --strict    forgotten boxes/files become an error\n"
"  boxy run program.bx --visual    draw boxes when you 'say' them\n\n"
"Saying things:\n"
"  say \"hello\"\n"
"  say \"hi, \", name\n"
"  ask \"what is your name?\" save name\n\n"
"Boxes (your variables):\n"
"  make a box called age\n"
"  put 7 in age\n"
"  say age\n"
"  age is 7                   (short form)\n\n"
"Borrowed boxes (you must return them):\n"
"  borrow a box called score\n"
"  put 100 in score\n"
"  return score               (give it back when done)\n\n"
"Math:\n"
"  x is 10 plus 5\n"
"  x is 10 minus 5\n"
"  x is 10 times 5\n"
"  x is 10 divided by 5\n"
"  x is 17 mod 5              (the remainder)\n"
"  x is 2 to 8                (2 to the power of 8)\n"
"  x is root of 25\n"
"  x is random between 1 and 10\n\n"
"Constants (cannot be changed):\n"
"  remember PI as 3.14\n\n"
"Comparisons:\n"
"  is, is not, is at least, is at most,\n"
"  is greater than, is less than,\n"
"  starts with, ends with, contains\n\n"
"If / otherwise:\n"
"  if age is at least 18 {\n"
"      say \"grown up\"\n"
"  } otherwise {\n"
"      say \"kid\"\n"
"  }\n\n"
"Loops:\n"
"  repeat 3 times { say \"hi\" }\n"
"  count from 1 to 10 as i { say i }\n"
"  while x is less than 10 { x is x plus 1 }\n"
"  stop                       (leave the loop)\n"
"  skip                       (jump to next round)\n\n"
"Functions:\n"
"  teach square with n {\n"
"      give n times n\n"
"  }\n"
"  say call square with 5\n\n"
"Lists:\n"
"  my friends are [\"Ana\", \"Bea\"]\n"
"  add \"Caio\" to my friends\n"
"  take \"Ana\" from my friends\n"
"  show my friends\n"
"  how many my friends\n\n"
"Arrows (pointers):\n"
"  make a box called target\n"
"  put 100 in target\n"
"  make an arrow called ptr to target\n"
"  say inside ptr             (read through the arrow)\n"
"  put 999 in ptr's box       (write through the arrow)\n\n"
"Files:\n"
"  open file \"diary.txt\" called f\n"
"  write \"today I learned Boxy\" to f\n"
"  close f\n"
"  open file \"diary.txt\" called f to read\n"
"  read f save text\n"
"  close f\n\n"
"Helpers (threads):\n"
"  start helper called worker {\n"
"      hold counter\n"
"      counter is counter plus 1\n"
"      let go counter\n"
"  }\n"
"  wait for worker\n\n"
"Doors (TCP sockets):\n"
"  open door called srv on port 9001\n"
"  wait for knock on srv save guest\n"
"  read guest save msg\n"
"  send msg to guest\n"
"  close guest\n"
"  close srv\n\n"
"  call door at \"127.0.0.1\" on port 9001 save line\n"
"  send \"hi\" to line\n"
"  read line save reply\n"
"  close line\n\n"
"Sleep and sizes:\n"
"  sleep 200                  (milliseconds)\n"
"  sleep 1 second\n"
"  say size of box\n\n"
"Comments:\n"
"  # one line\n"
"  ###\n"
"  many lines\n"
"  ###\n";

/* errors */

static const char *KIND_WORDS[] = { "Hmm,", "Oops,", "Wait," };

static const char *kind_word(int seed) {
    return KIND_WORDS[((unsigned)seed) % 3];
}

static int levenshtein(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la > 64) la = 64;
    if (lb > 64) lb = 64;
    int prev[66], cur[66];
    for (size_t j = 0; j <= lb; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int cost = (tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1])) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = cur[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int m = del < ins ? del : ins;
            if (sub < m) m = sub;
            cur[j] = m;
        }
        memcpy(prev, cur, sizeof(int) * (lb + 1));
    }
    return prev[lb];
}

static void bx_error(Line line, const char *msg, const char *fix) {
    fprintf(stderr, "\n%s I had to stop on line %d.\n", kind_word(line.line_number), line.line_number);
    fprintf(stderr, "    %s\n", line.text);
    fprintf(stderr, "  %s\n", msg);
    if (fix && *fix) fprintf(stderr, "  Try: %s\n", fix);
    fputc('\n', stderr);
    exit(1);
}

static void bx_error_suggest(Line line, const char *msg, const char *what,
                             const char *fix, const char **names, int n) {
    int best = -1, best_d = 1000;
    for (int i = 0; i < n; i++) {
        if (!names[i] || !names[i][0]) continue;
        int d = levenshtein(what, names[i]);
        if (d < best_d) { best_d = d; best = i; }
    }
    fprintf(stderr, "\n%s I had to stop on line %d.\n", kind_word(line.line_number), line.line_number);
    fprintf(stderr, "    %s\n", line.text);
    fprintf(stderr, "  %s\n", msg);
    if (best >= 0 && best_d > 0 && best_d <= 2)
        fprintf(stderr, "  Did you mean '%s'?\n", names[best]);
    if (fix && *fix) fprintf(stderr, "  Try: %s\n", fix);
    fputc('\n', stderr);
    exit(1);
}

/* strings */

static void trim(char *s) {
    char *start = s;
    size_t len;
    while (isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int ends_with(const char *s, const char *suffix) {
    size_t sl = strlen(s), tl = strlen(suffix);
    if (tl > sl) return 0;
    return strcmp(s + sl - tl, suffix) == 0;
}

static void make_name(const char *input, char *out) {
    int j = 0, underscore = 0;
    for (int i = 0; input[i] && j < MAX_NAME - 1; i++) {
        unsigned char ch = (unsigned char)input[i];
        if (isalnum(ch) || ch == '_') { out[j++] = (char)ch; underscore = 0; }
        else if (isspace(ch) || ch == '-' || ch == '.') {
            if (!underscore && j > 0) { out[j++] = '_'; underscore = 1; }
        }
    }
    if (j == 0) { strcpy(out, "thing"); return; }
    if (out[j - 1] == '_') j--;
    out[j] = '\0';
    if (isdigit((unsigned char)out[0])) {
        char tmp[MAX_NAME];
        snprintf(tmp, sizeof(tmp), "_%s", out);
        strcpy(out, tmp);
    }
}

/* values */

static Value none_value(void) { Value v; memset(&v, 0, sizeof(v)); v.type = VALUE_NONE; return v; }
static Value number_value(double n) { Value v; memset(&v, 0, sizeof(v)); v.type = VALUE_NUMBER; v.number = n; return v; }
static Value bool_value(int b) { Value v; memset(&v, 0, sizeof(v)); v.type = VALUE_BOOL; v.boolean = b ? 1 : 0; return v; }
static Value text_value(const char *t) { Value v; memset(&v, 0, sizeof(v)); v.type = VALUE_TEXT; strncpy(v.text, t, MAX_TEXT - 1); return v; }

static double as_number(Value v) {
    if (v.type == VALUE_NUMBER) return v.number;
    if (v.type == VALUE_BOOL)   return v.boolean ? 1.0 : 0.0;
    if (v.type == VALUE_TEXT)   return atof(v.text);
    return 0.0;
}

static int truthy(Value v) {
    if (v.type == VALUE_BOOL)   return v.boolean;
    if (v.type == VALUE_NUMBER) return fabs(v.number) > 1e-9;
    if (v.type == VALUE_TEXT)   return strlen(v.text) > 0;
    if (v.type == VALUE_LIST)   return v.list_count > 0;
    return 0;
}

static void value_to_string(Value v, char *out, size_t n) {
    if (v.type == VALUE_NUMBER) {
        if (fabs(v.number - round(v.number)) < 1e-9 && fabs(v.number) < 1e15)
            snprintf(out, n, "%.0f", v.number);
        else snprintf(out, n, "%g", v.number);
    } else if (v.type == VALUE_TEXT) snprintf(out, n, "%s", v.text);
    else if (v.type == VALUE_BOOL) snprintf(out, n, "%s", v.boolean ? "yes" : "no");
    else if (v.type == VALUE_LIST) {
        size_t pos = 0;
        for (int i = 0; i < v.list_count && pos + 1 < n; i++) {
            int w = snprintf(out + pos, n - pos, "%s%s", i ? ", " : "", v.list[i]);
            if (w < 0) break;
            pos += (size_t)w;
        }
    } else snprintf(out, n, "nothing");
}

static void print_visual_box(const char *label, const char *value) {
    char inner[256];
    if (label && *label) snprintf(inner, sizeof(inner), " %s: %s ", label, value);
    else                 snprintf(inner, sizeof(inner), " %s ", value);
    size_t len = strlen(inner);
    fputs("+", stdout);
    for (size_t i = 0; i < len; i++) fputc('-', stdout);
    fputs("+\n|", stdout);
    fputs(inner, stdout);
    fputs("|\n+", stdout);
    for (size_t i = 0; i < len; i++) fputc('-', stdout);
    fputs("+\n", stdout);
}

static void print_value(Interpreter *it, Value v, const char *label) {
    if (v.type == VALUE_LIST) {
        for (int i = 0; i < v.list_count; i++) printf("- %s\n", v.list[i]);
        return;
    }
    char buf[MAX_TEXT * 2];
    value_to_string(v, buf, sizeof(buf));
    if (it && it->visual) print_visual_box(label, buf);
    else printf("%s\n", buf);
}

/* symbol tables */

static Var *find_var_locked(Interpreter *it, const char *name) {
    for (int i = 0; i < it->var_count; i++) if (strcmp(it->vars[i].name, name) == 0) return &it->vars[i];
    return NULL;
}

static Var *find_var(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    pthread_mutex_lock(&it->state_mutex);
    Var *r = find_var_locked(it, name);
    pthread_mutex_unlock(&it->state_mutex);
    return r;
}

static Value get_var_value(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    pthread_mutex_lock(&it->state_mutex);
    Var *r = find_var_locked(it, name);
    Value v = r ? r->value : none_value();
    pthread_mutex_unlock(&it->state_mutex);
    return v;
}

static void set_var(Interpreter *it, const char *raw, Value value) {
    char name[MAX_NAME]; make_name(raw, name);
    pthread_mutex_lock(&it->state_mutex);
    Var *v = find_var_locked(it, name);
    if (!v) {
        if (it->var_count >= MAX_SYMBOLS) {
            pthread_mutex_unlock(&it->state_mutex);
            fprintf(stderr, "Hmm, too many names. Try reusing some.\n");
            exit(1);
        }
        v = &it->vars[it->var_count++];
        strcpy(v->name, name);
    }
    v->value = value;
    pthread_mutex_unlock(&it->state_mutex);
}

static Box *find_box_locked(Interpreter *it, const char *name) {
    for (int i = 0; i < it->box_count; i++) if (strcmp(it->boxes[i].name, name) == 0) return &it->boxes[i];
    return NULL;
}

static Box *find_box(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    return find_box_locked(it, name);
}

static Box *make_box_obj(Interpreter *it, const char *raw, int borrowed, int line_no) {
    char name[MAX_NAME]; make_name(raw, name);
    pthread_mutex_lock(&it->state_mutex);
    Box *b = find_box_locked(it, name);
    if (!b) {
        if (it->box_count >= MAX_SYMBOLS) {
            pthread_mutex_unlock(&it->state_mutex);
            fprintf(stderr, "Hmm, too many boxes. Try reusing some.\n");
            exit(1);
        }
        b = &it->boxes[it->box_count++];
        strcpy(b->name, name);
    }
    b->value = number_value(0);
    b->borrowed = borrowed;
    b->alive = 1;
    b->line_born = line_no;
    pthread_mutex_unlock(&it->state_mutex);
    return b;
}

static Arrow *find_arrow_locked(Interpreter *it, const char *name) {
    for (int i = 0; i < it->arrow_count; i++) if (strcmp(it->arrows[i].name, name) == 0) return &it->arrows[i];
    return NULL;
}

static Arrow *find_arrow(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    return find_arrow_locked(it, name);
}

static Arrow *make_arrow_obj(Interpreter *it, const char *raw, const char *target_raw, int line_no) {
    char name[MAX_NAME], target[MAX_NAME];
    make_name(raw, name); make_name(target_raw, target);
    pthread_mutex_lock(&it->state_mutex);
    Arrow *a = find_arrow_locked(it, name);
    if (!a) {
        if (it->arrow_count >= MAX_SYMBOLS) {
            pthread_mutex_unlock(&it->state_mutex);
            fprintf(stderr, "Hmm, too many arrows.\n");
            exit(1);
        }
        a = &it->arrows[it->arrow_count++];
        strcpy(a->name, name);
    }
    strcpy(a->target, target);
    a->line_born = line_no;
    pthread_mutex_unlock(&it->state_mutex);
    return a;
}

static Constant *find_constant(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    for (int i = 0; i < it->const_count; i++) if (!strcmp(it->constants[i].name, name)) return &it->constants[i];
    return NULL;
}

static void set_constant(Interpreter *it, const char *raw, Value value, Line line) {
    char name[MAX_NAME]; make_name(raw, name);
    pthread_mutex_lock(&it->state_mutex);
    for (int i = 0; i < it->const_count; i++) {
        if (!strcmp(it->constants[i].name, name)) {
            pthread_mutex_unlock(&it->state_mutex);
            bx_error(line,
                "You tried to change something you said to remember. Remembered things stay the same.",
                "remember PI as 3.14");
        }
    }
    if (it->const_count >= MAX_CONSTANTS) {
        pthread_mutex_unlock(&it->state_mutex);
        fprintf(stderr, "Hmm, too many remembered things.\n");
        exit(1);
    }
    Constant *c = &it->constants[it->const_count++];
    strcpy(c->name, name);
    c->value = value;
    pthread_mutex_unlock(&it->state_mutex);
}

static Function *find_func(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    for (int i = 0; i < it->func_count; i++) if (!strcmp(it->funcs[i].name, name)) return &it->funcs[i];
    return NULL;
}

static OpenFile *find_open_file(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    for (int i = 0; i < it->file_count; i++)
        if (it->open_files[i].open && !strcmp(it->open_files[i].alias, name))
            return &it->open_files[i];
    return NULL;
}

static Socket *find_socket(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    for (int i = 0; i < it->socket_count; i++)
        if (it->sockets[i].open && !strcmp(it->sockets[i].alias, name))
            return &it->sockets[i];
    return NULL;
}

static Socket *new_socket_slot(Interpreter *it) {
    for (int i = 0; i < it->socket_count; i++)
        if (!it->sockets[i].open) return &it->sockets[i];
    if (it->socket_count >= MAX_SOCKETS) return NULL;
    return &it->sockets[it->socket_count++];
}

static NamedLock *get_lock(Interpreter *it, const char *raw) {
    char name[MAX_NAME]; make_name(raw, name);
    NamedLock *lock = NULL;
    pthread_mutex_lock(&it->state_mutex);
    for (int i = 0; i < it->lock_count; i++) {
        if (strcmp(it->locks[i].name, name) == 0) { lock = &it->locks[i]; break; }
    }
    if (!lock) {
        if (it->lock_count >= MAX_LOCKS) {
            pthread_mutex_unlock(&it->state_mutex);
            fprintf(stderr, "Hmm, too many locks.\n");
            exit(1);
        }
        lock = &it->locks[it->lock_count++];
        strcpy(lock->name, name);
        pthread_mutex_init(&lock->mutex, NULL);
        lock->initialized = 1;
    }
    pthread_mutex_unlock(&it->state_mutex);
    return lock;
}

static int collect_names(Interpreter *it, const char *names[], int max) {
    int n = 0;
    for (int i = 0; i < it->box_count && n < max; i++)   names[n++] = it->boxes[i].name;
    for (int i = 0; i < it->var_count && n < max; i++)   names[n++] = it->vars[i].name;
    for (int i = 0; i < it->const_count && n < max; i++) names[n++] = it->constants[i].name;
    for (int i = 0; i < it->arrow_count && n < max; i++) names[n++] = it->arrows[i].name;
    return n;
}

/* string splitting */

static int split_around(const char *input, const char *middle, char *left, char *right) {
    const char *pos = strstr(input, middle);
    if (!pos) return 0;
    size_t len = (size_t)(pos - input);
    if (len >= MAX_LINE) len = MAX_LINE - 1;
    memcpy(left, input, len); left[len] = '\0';
    const char *r = pos + strlen(middle);
    size_t rlen = strlen(r);
    if (rlen >= MAX_LINE) rlen = MAX_LINE - 1;
    memcpy(right, r, rlen); right[rlen] = '\0';
    trim(left); trim(right);
    return 1;
}

static int split_around_outside_quotes(const char *input, const char *middle,
                                       char *left, char *right) {
    int in_q = 0;
    size_t mlen = strlen(middle);
    for (const char *p = input; *p; p++) {
        if (*p == '"') in_q = !in_q;
        if (!in_q && strncmp(p, middle, mlen) == 0) {
            size_t len = (size_t)(p - input);
            if (len >= MAX_LINE) len = MAX_LINE - 1;
            memcpy(left, input, len); left[len] = '\0';
            const char *r = p + mlen;
            size_t rlen = strlen(r);
            if (rlen >= MAX_LINE) rlen = MAX_LINE - 1;
            memcpy(right, r, rlen); right[rlen] = '\0';
            trim(left); trim(right);
            return 1;
        }
    }
    return 0;
}

static int is_quoted(const char *s) {
    size_t n = strlen(s);
    return n >= 2 && s[0] == '"' && s[n - 1] == '"';
}

static void unquote(const char *s, char *out) {
    size_t n = strlen(s);
    const char *src; size_t len;
    if (is_quoted(s)) { src = s + 1; len = n - 2; }
    else              { src = s;     len = n;     }
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < MAX_TEXT; i++) {
        if (src[i] == '\\' && i + 1 < len) {
            char c = src[i + 1];
            switch (c) {
                case 'n':  out[o++] = '\n'; i++; continue;
                case 'r':  out[o++] = '\r'; i++; continue;
                case 't':  out[o++] = '\t'; i++; continue;
                case '\\': out[o++] = '\\'; i++; continue;
                case '"':  out[o++] = '"';  i++; continue;
                case '0':  out[o++] = '\0'; i++; continue;
            }
        }
        out[o++] = src[i];
    }
    out[o] = '\0';
}

/* expression evaluator */

static Value eval_expr(Interpreter *it, const char *raw, Line line);
static void run_block(Interpreter *it, Line *lines, int start, int end);

static Value parse_list(Interpreter *it, const char *expr, Line line) {
    Value v; memset(&v, 0, sizeof(v)); v.type = VALUE_LIST;
    char tmp[MAX_LINE]; strncpy(tmp, expr, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = '\0';
    size_t l = strlen(tmp);
    if (l < 2) return v;
    tmp[l - 1] = '\0';
    memmove(tmp, tmp + 1, l);
    trim(tmp);
    if (!tmp[0]) return v;
    char *p = tmp;
    while (*p && v.list_count < MAX_LIST_ITEMS) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char tok[MAX_LINE]; int ti = 0; int in_q = 0;
        while (*p && (in_q || *p != ',')) {
            if (*p == '"') in_q = !in_q;
            tok[ti++] = *p++;
            if (ti >= MAX_LINE - 1) break;
        }
        tok[ti] = '\0';
        trim(tok);
        Value item = eval_expr(it, tok, line);
        char buf[MAX_TEXT];
        value_to_string(item, buf, sizeof(buf));
        snprintf(v.list[v.list_count++], MAX_LIST_ITEM, "%s", buf);
    }
    return v;
}

static Value eval_binary(Interpreter *it, const char *expr, const char *op, int kind, Line line) {
    char l[MAX_LINE], r[MAX_LINE];
    if (!split_around_outside_quotes(expr, op, l, r)) return none_value();
    Value a = eval_expr(it, l, line), b = eval_expr(it, r, line);
    double x = as_number(a), y = as_number(b);
    switch (kind) {
        case 1:  return number_value(x + y);
        case 2:  return number_value(x - y);
        case 3:  return number_value(x * y);
        case 4:
            if (fabs(y) < 1e-12) bx_error(line,
                "You divided by zero. That doesn't have an answer.",
                "x is 10 divided by 2");
            return number_value(x / y);
        case 12:
            if (fabs(y) < 1e-12) bx_error(line,
                "You took 'mod' by zero. Same as dividing by zero.",
                "x is 17 mod 5");
            return number_value(fmod(x, y));
        case 5:
            if (a.type == VALUE_TEXT && b.type == VALUE_TEXT) return bool_value(strcmp(a.text, b.text) == 0);
            if (a.type == VALUE_BOOL && b.type == VALUE_BOOL) return bool_value(a.boolean == b.boolean);
            return bool_value(fabs(x - y) < 1e-9);
        case 6:
            if (a.type == VALUE_TEXT && b.type == VALUE_TEXT) return bool_value(strcmp(a.text, b.text) != 0);
            return bool_value(fabs(x - y) >= 1e-9);
        case 7:  return bool_value(x >= y);
        case 8:  return bool_value(x <= y);
        case 9:  return bool_value(x > y);
        case 10: return bool_value(x < y);
        case 11: return number_value(pow(x, y));
        case 13: {
            char ab[MAX_TEXT], bb[MAX_TEXT];
            value_to_string(a, ab, sizeof(ab));
            value_to_string(b, bb, sizeof(bb));
            size_t la = strlen(ab), lb = strlen(bb);
            return bool_value(la >= lb && memcmp(ab, bb, lb) == 0);
        }
        case 14: {
            char ab[MAX_TEXT], bb[MAX_TEXT];
            value_to_string(a, ab, sizeof(ab));
            value_to_string(b, bb, sizeof(bb));
            size_t la = strlen(ab), lb = strlen(bb);
            return bool_value(la >= lb && memcmp(ab + la - lb, bb, lb) == 0);
        }
        case 15: {
            char ab[MAX_TEXT], bb[MAX_TEXT];
            value_to_string(a, ab, sizeof(ab));
            value_to_string(b, bb, sizeof(bb));
            return bool_value(strstr(ab, bb) != NULL);
        }
        case 16: { /* and */
            return bool_value(truthy(a) && truthy(b));
        }
        case 17: { /* or */
            return bool_value(truthy(a) || truthy(b));
        }
    }
    return none_value();
}

static Interpreter *make_function_scope(Interpreter *parent) {
    Interpreter *child = calloc(1, sizeof(Interpreter));
    if (!child) { fprintf(stderr, "Hmm, ran out of memory calling a function.\n"); exit(1); }
    pthread_mutex_lock(&parent->state_mutex);
    memcpy(child->vars, parent->vars, sizeof(parent->vars));
    child->var_count = parent->var_count;
    memcpy(child->boxes, parent->boxes, sizeof(parent->boxes));
    child->box_count = parent->box_count;
    memcpy(child->arrows, parent->arrows, sizeof(parent->arrows));
    child->arrow_count = parent->arrow_count;
    memcpy(child->constants, parent->constants, sizeof(parent->constants));
    child->const_count = parent->const_count;
    memcpy(child->funcs, parent->funcs, sizeof(parent->funcs));
    child->func_count = parent->func_count;
    child->visual = parent->visual;
    pthread_mutex_unlock(&parent->state_mutex);
    pthread_mutex_init(&child->state_mutex, NULL);
    child->is_function = 1;
    return child;
}

static void destroy_function_scope(Interpreter *child) {
    pthread_mutex_destroy(&child->state_mutex);
    free(child);
}

static Value call_function(Interpreter *it, const char *fname, const char *args_raw, Line line) {
    Function *fn = find_func(it, fname);
    if (!fn) {
        const char *names[MAX_FUNCTIONS];
        int n = 0;
        for (int i = 0; i < it->func_count && n < MAX_FUNCTIONS; i++)
            names[n++] = it->funcs[i].name;
        char nm[MAX_NAME]; make_name(fname, nm);
        bx_error_suggest(line,
            "I don't know that function.", nm,
            "teach square with n { give n times n }",
            names, n);
    }
    Interpreter *child = make_function_scope(it);

    Value args[MAX_PARAMS];
    int argc = 0;
    char buf[MAX_LINE]; strncpy(buf, args_raw, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *ap = buf;
    while (*ap && argc < MAX_PARAMS) {
        char one[MAX_LINE];
        char *and_pos = NULL;
        int in_q = 0;
        for (char *p = ap; *p; p++) {
            if (*p == '"') in_q = !in_q;
            if (!in_q && strncmp(p, " and ", 5) == 0) { and_pos = p; break; }
        }
        if (and_pos) { size_t l = (size_t)(and_pos - ap); strncpy(one, ap, l); one[l] = '\0'; ap = and_pos + 5; }
        else { strncpy(one, ap, sizeof(one) - 1); one[sizeof(one) - 1] = '\0'; ap += strlen(ap); }
        trim(one);
        if (*one) args[argc++] = eval_expr(it, one, line);
    }
    if (argc != fn->param_count) {
        char msg[256];
        snprintf(msg, sizeof(msg),
            "The function '%s' wants %d input%s, but you gave %d.",
            fn->name, fn->param_count, fn->param_count == 1 ? "" : "s", argc);
        destroy_function_scope(child);
        bx_error(line, msg, "call square with 5");
    }
    for (int i = 0; i < argc; i++) set_var(child, fn->params[i], args[i]);
    set_var(child, "bx_return_value", none_value());

    child->returning = 0;
    run_block(child, fn->body, 0, fn->body_count);

    Value result = get_var_value(child, "bx_return_value");
    destroy_function_scope(child);
    return result;
}

static int looks_like_identifier(const char *s) {
    if (!s || !*s) return 0;
    if (!isalpha((unsigned char)s[0]) && s[0] != '_') return 0;
    for (const char *p = s; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == ' '))
            return 0;
    }
    return 1;
}

static Value eval_expr(Interpreter *it, const char *raw, Line line) {
    char expr[MAX_LINE], left[MAX_LINE], right[MAX_LINE], text[MAX_TEXT], tmp[MAX_LINE];
    strncpy(expr, raw, sizeof(expr) - 1); expr[sizeof(expr) - 1] = '\0'; trim(expr);
    if (!expr[0]) return none_value();
    if (is_quoted(expr)) { unquote(expr, text); return text_value(text); }
    if (expr[0] == '[' && expr[strlen(expr) - 1] == ']') return parse_list(it, expr, line);
    if (!strcmp(expr, "yes")     || !strcmp(expr, "true")  || !strcmp(expr, "on"))  return bool_value(1);
    if (!strcmp(expr, "no")      || !strcmp(expr, "false") || !strcmp(expr, "off")) return bool_value(0);
    if (!strcmp(expr, "nothing") || !strcmp(expr, "null"))                          return none_value();

    if (split_around_outside_quotes(expr, " starts with ",     left, right)) return eval_binary(it, expr, " starts with ",     13, line);
    if (split_around_outside_quotes(expr, " ends with ",       left, right)) return eval_binary(it, expr, " ends with ",       14, line);
    if (split_around_outside_quotes(expr, " contains ",        left, right)) return eval_binary(it, expr, " contains ",        15, line);
    if (split_around_outside_quotes(expr, " is at least ",     left, right)) return eval_binary(it, expr, " is at least ",      7, line);
    if (split_around_outside_quotes(expr, " is at most ",      left, right)) return eval_binary(it, expr, " is at most ",       8, line);
    if (split_around_outside_quotes(expr, " is greater than ", left, right)) return eval_binary(it, expr, " is greater than ",  9, line);
    if (split_around_outside_quotes(expr, " is more than ",    left, right)) return eval_binary(it, expr, " is more than ",     9, line);
    if (split_around_outside_quotes(expr, " is less than ",    left, right)) return eval_binary(it, expr, " is less than ",    10, line);
    if (split_around_outside_quotes(expr, " is not ",          left, right)) return eval_binary(it, expr, " is not ",           6, line);
    if (split_around_outside_quotes(expr, " equals ",          left, right)) return eval_binary(it, expr, " equals ",           5, line);
    if (split_around_outside_quotes(expr, " is ",              left, right)) return eval_binary(it, expr, " is ",               5, line);
    if (split_around_outside_quotes(expr, " plus ",            left, right)) return eval_binary(it, expr, " plus ",             1, line);
    if (split_around_outside_quotes(expr, " minus ",           left, right)) return eval_binary(it, expr, " minus ",            2, line);
    if (split_around_outside_quotes(expr, " times ",           left, right)) return eval_binary(it, expr, " times ",            3, line);
    if (split_around_outside_quotes(expr, " divided by ",      left, right)) return eval_binary(it, expr, " divided by ",       4, line);
    if (split_around_outside_quotes(expr, " mod ",             left, right)) return eval_binary(it, expr, " mod ",             12, line);
    if (split_around_outside_quotes(expr, " modulo ",          left, right)) return eval_binary(it, expr, " modulo ",          12, line);
    if (split_around_outside_quotes(expr, " ^ ",               left, right)) return eval_binary(it, expr, " ^ ",               11, line);
    if (split_around_outside_quotes(expr, " to the ",          left, right)) return eval_binary(it, expr, " to the ",           11, line);
    if (split_around_outside_quotes(expr, " to ",              left, right)) return eval_binary(it, expr, " to ",               11, line);

    if (starts_with(expr, "root of "))                               return number_value(sqrt(as_number(eval_expr(it, expr + 8, line))));
    if (starts_with(expr, "square root of "))                        return number_value(sqrt(as_number(eval_expr(it, expr + 15, line))));
    if (starts_with(expr, "cube root of "))                          return number_value(pow(as_number(eval_expr(it, expr + 13, line)), 1.0 / 3.0));
    if (starts_with(expr, "root ") && split_around(expr + 5, " of ", left, right))
        return number_value(pow(as_number(eval_expr(it, right, line)), 1.0 / as_number(eval_expr(it, left, line))));
    if (ends_with(expr, " root")) {
        strncpy(tmp, expr, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = '\0';
        tmp[strlen(tmp) - 5] = '\0'; trim(tmp);
        return number_value(sqrt(as_number(eval_expr(it, tmp, line))));
    }
    if (split_around(expr, " percent of ", left, right))
        return number_value((as_number(eval_expr(it, left, line)) / 100.0) * as_number(eval_expr(it, right, line)));
    if (starts_with(expr, "random between ") && split_around(expr + 15, " and ", left, right)) {
        int a = (int)as_number(eval_expr(it, left, line)), b = (int)as_number(eval_expr(it, right, line));
        if (b < a) { int s = a; a = b; b = s; }
        return number_value(a + rand() % (b - a + 1));
    }

    if (starts_with(expr, "inside ")) {
        char name[MAX_NAME]; strncpy(name, expr + 7, sizeof(name) - 1); name[sizeof(name) - 1] = '\0'; trim(name);
        Arrow *a = find_arrow(it, name);
        if (!a) {
            const char *names[MAX_SYMBOLS]; int n = 0;
            for (int i = 0; i < it->arrow_count && n < MAX_SYMBOLS; i++) names[n++] = it->arrows[i].name;
            char nm[MAX_NAME]; make_name(name, nm);
            bx_error_suggest(line,
                "I don't see an arrow with that name.", nm,
                "make an arrow called ptr to age",
                names, n);
        }
        Box *b = find_box(it, a->target);
        if (!b) bx_error(line,
            "Your arrow is pointing at a box that does not exist.",
            "make a box called age   then   make an arrow called ptr to age");
        if (!b->alive) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                "Your arrow '%s' is pointing at a box that was already returned. Don't peek into a box that was given back.",
                a->name);
            bx_error(line, msg,
                "don't return a box while an arrow still points at it");
        }
        return b->value;
    }

    if (!strcmp(expr, "size of box")     || !strcmp(expr, "size of number")) return number_value((double)sizeof(double));
    if (!strcmp(expr, "size of word")    || !strcmp(expr, "size of letter")) return number_value((double)sizeof(char));
    if (!strcmp(expr, "size of arrow")   || !strcmp(expr, "size of pointer"))return number_value((double)sizeof(void *));

    char *end = NULL;
    double n = strtod(expr, &end);
    if (end != expr && *end == '\0') return number_value(n);

    if (starts_with(expr, "call ")) {
        char rest[MAX_LINE]; strncpy(rest, expr + 5, sizeof(rest) - 1); rest[sizeof(rest) - 1] = '\0'; trim(rest);
        char fname[MAX_NAME]; char args_raw[MAX_LINE] = "";
        char *with = strstr(rest, " with ");
        if (with) {
            size_t nl = (size_t)(with - rest);
            if (nl >= sizeof(fname)) nl = sizeof(fname) - 1;
            strncpy(fname, rest, nl); fname[nl] = '\0';
            strncpy(args_raw, with + 6, sizeof(args_raw) - 1); args_raw[sizeof(args_raw) - 1] = '\0';
        } else {
            strncpy(fname, rest, sizeof(fname) - 1); fname[sizeof(fname) - 1] = '\0';
        }
        trim(fname); trim(args_raw);
        return call_function(it, fname, args_raw, line);
    }

    if (starts_with(expr, "how many ")) {
        Value v = eval_expr(it, expr + 9, line);
        if (v.type == VALUE_LIST) return number_value((double)v.list_count);
        if (v.type == VALUE_TEXT) return number_value((double)strlen(v.text));
        return number_value(0);
    }

    {
        char nm[MAX_NAME]; make_name(expr, nm);
        Box *b = find_box(it, expr);
        if (b) {
            if (!b->alive) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "The box '%s' was already returned. You can't peek inside after giving it back.",
                    b->name);
                bx_error(line, msg, "make sure you read the box before 'return'-ing it");
            }
            return b->value;
        }
        Var *v = find_var(it, expr);
        if (v) return v->value;
        Constant *c = find_constant(it, expr);
        if (c) return c->value;
    }

    if (looks_like_identifier(expr)) {
        const char *names[MAX_SYMBOLS * 4]; int n = collect_names(it, names, MAX_SYMBOLS * 4);
        char nm[MAX_NAME]; make_name(expr, nm);
        char msg[256];
        snprintf(msg, sizeof(msg),
            "I don't see a box, name, or remembered thing called '%s'.", nm);
        bx_error_suggest(line, msg, nm,
            "make a box called age   /   age is 7   /   say \"hi\"",
            names, n);
    }

    return text_value(expr);
}

/* control flow */

static int parse_assignment(const char *text, char *name, char *expr) {
    if (split_around_outside_quotes(text, " is ",  name, expr)) return 1;
    if (split_around_outside_quotes(text, " are ", name, expr)) return 1;
    return 0;
}

static int collect_block(Line *lines, int count, int start, int *body_start, int *body_end) {
    int depth = 0;
    *body_start = start + 1;
    for (int i = start + 1; i < count; i++) {
        char t[MAX_LINE]; strcpy(t, lines[i].text); trim(t);
        if (t[0] == '}') {
            if (depth == 0) {
                *body_end = i;
                return starts_with(t, "} otherwise") ? i : i + 1;
            }
            if (starts_with(t, "} otherwise")) continue;
            depth--;
            continue;
        }
        if (ends_with(t, "{")) depth++;
    }
    return -1;
}

static int find_otherwise(Line *lines, int end, int index, int *body_start, int *body_end) {
    if (index >= end) return 0;
    char t[MAX_LINE]; strcpy(t, lines[index].text); trim(t);
    if (!strcmp(t, "otherwise {") || starts_with(t, "} otherwise")) {
        return collect_block(lines, end, index, body_start, body_end);
    }
    return 0;
}

static void *thread_runner(void *arg) {
    ThreadJob *job = (ThreadJob *)arg;
    run_block(job->interpreter, job->lines, 0, job->count);
    job->interpreter->returning = 0;
    return NULL;
}

static void start_helper(Interpreter *it, Line *lines, int index, int body_start, int body_end) {
    char raw[MAX_LINE], name[MAX_NAME] = "helper";
    strcpy(raw, lines[index].text);
    raw[strlen(raw) - 1] = '\0'; trim(raw);
    char *called = strstr(raw, " called ");
    if (called) { strcpy(name, called + 8); trim(name); }
    else snprintf(name, sizeof(name), "helper_%d", it->thread_count + 1);
    if (it->thread_count >= MAX_THREADS) bx_error(lines[index],
        "Too many helpers running at once. Wait for one before starting another.",
        "wait for worker");

    Helper *om = &it->threads[it->thread_count++];
    make_name(name, om->name);
    om->active = 1;
    om->line_born = lines[index].line_number;
    om->job = malloc(sizeof(ThreadJob));
    if (!om->job) bx_error(lines[index],
        "Out of memory starting that helper.",
        "try fewer helpers");
    om->job->interpreter = it;
    strcpy(om->job->name, om->name);
    om->job->count = body_end - body_start;
    if (om->job->count > MAX_BLOCK) bx_error(lines[index],
        "That helper's block is too big. Try splitting it.",
        "keep helper bodies short");
    for (int i = 0; i < om->job->count; i++) om->job->lines[i] = lines[body_start + i];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREAD_STACK);
    int rc = pthread_create(&om->thread, &attr, thread_runner, om->job);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        bx_error(lines[index],
            "The computer wouldn't start that helper. Try fewer at once.",
            "wait for an existing helper first");
    }
}

static void execute_line(Interpreter *it, Line line);

static void run_block(Interpreter *it, Line *lines, int start, int end) {
    for (int i = start; i < end; i++) {
        char t[MAX_LINE]; strcpy(t, lines[i].text); trim(t);
        if (!t[0] || t[0] == '#') continue;
        if (t[0] == '}') continue;

        if (starts_with(t, "if ") && ends_with(t, "{")) {
            int bs, be, next = collect_block(lines, end, i, &bs, &be);
            if (next < 0) bx_error(lines[i],
                "You opened 'if' with '{' but I never found the matching '}'.",
                "} otherwise { ... }   or just  }");
            t[strlen(t) - 1] = '\0'; trim(t);
            if (truthy(eval_expr(it, t + 3, lines[i]))) {
                run_block(it, lines, bs, be);
                if (it->returning || it->loop_break || it->loop_skip) return;
                int ebs, ebe, after = find_otherwise(lines, end, next, &ebs, &ebe);
                i = after ? after - 1 : next - 1;
            } else {
                int ebs, ebe, after = find_otherwise(lines, end, next, &ebs, &ebe);
                if (after) {
                    run_block(it, lines, ebs, ebe);
                    if (it->returning || it->loop_break || it->loop_skip) return;
                    i = after - 1;
                }
                else i = next - 1;
            }
            continue;
        }

        if (starts_with(t, "repeat ") && ends_with(t, "{")) {
            int bs, be, next = collect_block(lines, end, i, &bs, &be);
            if (next < 0) bx_error(lines[i],
                "You opened 'repeat' with '{' but I never found the matching '}'.",
                "}");
            t[strlen(t) - 1] = '\0'; trim(t);
            char c[MAX_LINE]; strcpy(c, t + 7); trim(c);
            char *times = strstr(c, " times"); if (times) *times = '\0'; trim(c);
            int count = (int)as_number(eval_expr(it, c, lines[i]));
            for (int r = 0; r < count; r++) {
                run_block(it, lines, bs, be);
                if (it->returning) return;
                if (it->loop_break) { it->loop_break = 0; break; }
                if (it->loop_skip)  { it->loop_skip  = 0; continue; }
            }
            i = next - 1;
            continue;
        }

        if (starts_with(t, "count from ") && ends_with(t, "{")) {
            int bs, be, next = collect_block(lines, end, i, &bs, &be);
            if (next < 0) bx_error(lines[i],
                "You opened 'count from' with '{' but I never found the matching '}'.",
                "}");
            t[strlen(t) - 1] = '\0'; trim(t);
            char inner[MAX_LINE]; strcpy(inner, t + 11);
            char from_s[MAX_LINE], rest2[MAX_LINE], to_s[MAX_LINE], var_s[MAX_NAME];
            if (!split_around(inner, " to ", from_s, rest2)) bx_error(lines[i],
                "'count from' needs a 'to'. Like: count from 0 to 9 as i.",
                "count from 0 to 9 as i {");
            char *as_part = strstr(rest2, " as ");
            if (as_part) { *as_part = '\0'; trim(rest2); strcpy(to_s, rest2); strcpy(var_s, as_part + 4); trim(var_s); }
            else { strcpy(to_s, rest2); trim(to_s); strcpy(var_s, "i"); }
            int from_v = (int)as_number(eval_expr(it, from_s, lines[i]));
            int to_v   = (int)as_number(eval_expr(it, to_s,   lines[i]));
            int step   = from_v <= to_v ? 1 : -1;
            for (int ii = from_v; step > 0 ? ii <= to_v : ii >= to_v; ii += step) {
                set_var(it, var_s, number_value(ii));
                run_block(it, lines, bs, be);
                if (it->returning) return;
                if (it->loop_break) { it->loop_break = 0; break; }
                if (it->loop_skip)  { it->loop_skip  = 0; continue; }
            }
            i = next - 1;
            continue;
        }

        if (starts_with(t, "while ") && ends_with(t, "{")) {
            int bs, be, next = collect_block(lines, end, i, &bs, &be);
            if (next < 0) bx_error(lines[i],
                "You opened 'while' with '{' but I never found the matching '}'.",
                "}");
            char cond[MAX_LINE]; strcpy(cond, t + 6);
            cond[strlen(cond) - 1] = '\0'; trim(cond);
            int safety = 0;
            while (truthy(eval_expr(it, cond, lines[i]))) {
                run_block(it, lines, bs, be);
                if (it->returning) return;
                if (it->loop_break) { it->loop_break = 0; break; }
                if (it->loop_skip)  { it->loop_skip  = 0; }
                if (++safety > 10000000) bx_error(lines[i],
                    "Your 'while' has been running for ten million rounds. Something inside it should change the answer.",
                    "make sure something inside changes the condition");
            }
            i = next - 1;
            continue;
        }

        if ((starts_with(t, "teach ") || starts_with(t, "fn ")) && ends_with(t, "{")) {
            int bs, be, next = collect_block(lines, end, i, &bs, &be);
            if (next < 0) bx_error(lines[i],
                "You opened a function with '{' but I never found the matching '}'.",
                "}");
            char def[MAX_LINE]; strcpy(def, starts_with(t, "fn ") ? t + 3 : t + 6);
            def[strlen(def) - 1] = '\0'; trim(def);
            char fname[MAX_NAME]; char params_raw[MAX_LINE] = "";
            char *with = strstr(def, " with ");
            if (with) {
                size_t nl = (size_t)(with - def);
                if (nl >= sizeof(fname)) nl = sizeof(fname) - 1;
                strncpy(fname, def, nl); fname[nl] = '\0';
                strncpy(params_raw, with + 6, sizeof(params_raw) - 1); params_raw[sizeof(params_raw) - 1] = '\0';
            } else strcpy(fname, def);
            trim(fname);
            if (it->func_count >= MAX_FUNCTIONS) bx_error(lines[i],
                "Too many functions. Try teaching fewer of them.",
                "teach add with a and b {");
            Function *fn = &it->funcs[it->func_count++];
            make_name(fname, fn->name);
            fn->param_count = 0;
            char *ap = params_raw;
            while (*ap && fn->param_count < MAX_PARAMS) {
                char one[MAX_LINE]; char *and_pos = strstr(ap, " and ");
                if (and_pos) { size_t l = (size_t)(and_pos - ap); strncpy(one, ap, l); one[l] = '\0'; ap = and_pos + 5; }
                else { strcpy(one, ap); ap += strlen(ap); }
                trim(one);
                if (*one) make_name(one, fn->params[fn->param_count++]);
            }
            fn->body_count = be - bs;
            if (fn->body_count > MAX_BLOCK) bx_error(lines[i],
                "That function is huge. Try splitting it up.",
                "keep functions short");
            for (int bi = 0; bi < fn->body_count; bi++) fn->body[bi] = lines[bs + bi];
            i = next - 1;
            continue;
        }

        if (starts_with(t, "start helper") && ends_with(t, "{")) {
            int bs, be, next = collect_block(lines, end, i, &bs, &be);
            if (next < 0) bx_error(lines[i],
                "Your helper has no closing '}'.",
                "}");
            start_helper(it, lines, i, bs, be);
            i = next - 1;
            continue;
        }

        execute_line(it, lines[i]);
        if (it->returning || it->loop_break || it->loop_skip) return;
    }
}

/* statements */

static void say_expression(Interpreter *it, const char *expr_in, Line line) {
    char expr[MAX_LINE]; strncpy(expr, expr_in, sizeof(expr) - 1); expr[sizeof(expr) - 1] = '\0'; trim(expr);

    int has_comma = 0; int in_q = 0;
    for (const char *p = expr; *p; p++) { if (*p == '"') in_q = !in_q; if (!in_q && *p == ',') { has_comma = 1; break; } }
    if (has_comma) {
        char concat[MAX_TEXT * 4] = "";
        char tmp[MAX_LINE]; strcpy(tmp, expr);
        char *p = tmp;
        while (*p) {
            char tok[MAX_LINE]; int ti = 0; in_q = 0;
            while (*p == ' ') p++;
            while (*p && (in_q || *p != ',')) {
                if (*p == '"') in_q = !in_q;
                tok[ti++] = *p++;
                if (ti >= MAX_LINE - 1) break;
            }
            tok[ti] = '\0'; trim(tok);
            if (*p == ',') p++;
            if (*tok) {
                Value tv = eval_expr(it, tok, line);
                char piece[MAX_TEXT];
                value_to_string(tv, piece, sizeof(piece));
                strncat(concat, piece, sizeof(concat) - strlen(concat) - 1);
            }
        }
        if (it->visual) print_visual_box(NULL, concat);
        else printf("%s\n", concat);
        return;
    }
    Value v = eval_expr(it, expr, line);
    char label[MAX_NAME] = "";
    if (it->visual && v.type != VALUE_LIST && find_box(it, expr)) {
        char nm[MAX_NAME]; make_name(expr, nm);
        snprintf(label, sizeof(label), "%s", nm);
    }
    print_value(it, v, label[0] ? label : NULL);
}

static void execute_line(Interpreter *it, Line line) {
    char t[MAX_LINE], a[MAX_LINE], b[MAX_LINE];
    strncpy(t, line.text, sizeof(t) - 1); t[sizeof(t) - 1] = '\0'; trim(t);
    if (!t[0] || t[0] == '#') return;

    if (starts_with(t, "say ") || starts_with(t, "print ") || starts_with(t, "tell ")) {
        char *sp = strchr(t, ' ');
        say_expression(it, sp ? sp + 1 : "", line);
        return;
    }
    if (starts_with(t, "show ")) {
        Value v = eval_expr(it, t + 5, line);
        print_value(it, v, NULL);
        return;
    }

    if (starts_with(t, "ask ")) {
        char question[MAX_LINE], var[MAX_NAME], input[MAX_TEXT];
        char *q1 = strchr(t, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        if (q1 && q2) {
            size_t len = (size_t)(q2 - q1 - 1);
            if (len >= sizeof(question)) len = sizeof(question) - 1;
            strncpy(question, q1 + 1, len); question[len] = '\0';
            char *save = strstr(q2 + 1, " save ");
            if (!save) save = strstr(q2 + 1, " as ");
            if (save) {
                if (starts_with(save, " save ")) strcpy(var, save + 6);
                else                              strcpy(var, save + 4);
                trim(var);
                printf("%s ", question); fflush(stdout);
                if (!fgets(input, sizeof(input), stdin)) strcpy(input, "");
                trim(input);
                char *endp = NULL; double n = strtod(input, &endp);
                if (*input && endp != input && *endp == '\0') set_var(it, var, number_value(n));
                else                                          set_var(it, var, text_value(input));
                return;
            }
        }
        bx_error(line,
            "You asked a question but didn't say where the answer goes.",
            "ask \"what is your name?\" save name");
    }

    if (starts_with(t, "make a box called ")  ||
        starts_with(t, "make an box called ")) {
        const char *name = strstr(t, " called ") + 8;
        make_box_obj(it, name, 0, line.line_number);
        return;
    }

    if (starts_with(t, "borrow a box called ")  ||
        starts_with(t, "borrow an box called ")) {
        const char *name = strstr(t, " called ") + 8;
        make_box_obj(it, name, 1, line.line_number);
        return;
    }

    if (starts_with(t, "make a arrow called ")    ||
        starts_with(t, "make an arrow called ")   ||
        starts_with(t, "make a pointer called ")  ||
        starts_with(t, "make an pointer called ") ||
        starts_with(t, "make arrow called ")      ||
        starts_with(t, "make pointer called ")) {
        char *called = strstr(t, " called "), *to = strstr(t, " to ");
        if (!called || !to) bx_error(line,
            "An arrow needs a name and something to point at.",
            "make an arrow called ptr to age");
        char an[MAX_NAME], tn[MAX_NAME];
        strcpy(tn, to + 4);
        *to = '\0';
        strcpy(an, called + 8);
        trim(an); trim(tn);
        Box *target = find_box(it, tn);
        if (!target) {
            const char *names[MAX_SYMBOLS]; int n = 0;
            for (int i = 0; i < it->box_count && n < MAX_SYMBOLS; i++) names[n++] = it->boxes[i].name;
            char nm[MAX_NAME]; make_name(tn, nm);
            bx_error_suggest(line,
                "Your arrow points at a box that does not exist.", nm,
                "make a box called age   then   make an arrow called ptr to age",
                names, n);
        }
        if (!target->alive) bx_error(line,
            "You're pointing at a box that was already returned.",
            "point arrows only at boxes that are still around");
        make_arrow_obj(it, an, tn, line.line_number);
        return;
    }

    if (starts_with(t, "return ")) {
        char name[MAX_NAME]; strcpy(name, t + 7); trim(name);
        Box *bx = find_box(it, name);
        if (!bx) {
            const char *names[MAX_SYMBOLS]; int n = 0;
            for (int i = 0; i < it->box_count && n < MAX_SYMBOLS; i++) names[n++] = it->boxes[i].name;
            char nm[MAX_NAME]; make_name(name, nm);
            bx_error_suggest(line,
                "I don't see a box with that name.", nm,
                "borrow a box called score   ...   return score",
                names, n);
        }
        if (!bx->borrowed) bx_error(line,
            "You can only return a borrowed box. Kept boxes clean themselves up.",
            "borrow a box called score");
        if (!bx->alive) bx_error(line,
            "That box was already returned. You only need to return it once.",
            "return it once");
        pthread_mutex_lock(&it->state_mutex);
        bx->alive = 0; bx->value = none_value();
        pthread_mutex_unlock(&it->state_mutex);
        return;
    }

    if (starts_with(t, "hold "))   { pthread_mutex_lock(&get_lock(it, t + 5)->mutex); return; }
    if (starts_with(t, "let go ")) {
        NamedLock *l = get_lock(it, t + 7);
        if (pthread_mutex_unlock(&l->mutex) != 0) {
            bx_error(line,
                "You let go of a lock you weren't holding.",
                "hold counter   ...   let go counter");
        }
        return;
    }

    if (starts_with(t, "give ")) {
        if (!it->is_function) bx_error(line,
            "'give' only works inside a function.",
            "teach square with n { give n times n }");
        char expr[MAX_LINE]; strcpy(expr, t + 5); trim(expr);
        Value gv = eval_expr(it, expr, line);
        set_var(it, "bx_return_value", gv);
        it->returning = 1;
        return;
    }

    if (starts_with(t, "remember ")) {
        char rest[MAX_LINE]; strcpy(rest, t + 9);
        char cname[MAX_NAME], cval[MAX_LINE];
        if (!split_around(rest, " as ", cname, cval)) bx_error(line,
            "'remember' needs 'as'. Like: remember PI as 3.14.",
            "remember PI as 3.14");
        trim(cname); trim(cval);
        set_constant(it, cname, eval_expr(it, cval, line), line);
        return;
    }

    if (starts_with(t, "sleep ")) {
        char rest[MAX_LINE]; strcpy(rest, t + 6); trim(rest);
        double mult = 1.0;
        char *unit = strstr(rest, " seconds");
        if (!unit) unit = strstr(rest, " second");
        if (unit) { *unit = '\0'; mult = 1000.0; }
        else if ((unit = strstr(rest, " milliseconds"))) { *unit = '\0'; mult = 1.0; }
        else if ((unit = strstr(rest, " ms")))           { *unit = '\0'; mult = 1.0; }
        trim(rest);
        double ms = as_number(eval_expr(it, rest, line)) * mult;
        if (ms < 0) ms = 0;
        if (ms > 0) usleep((useconds_t)(ms * 1000.0));
        return;
    }

    if (!strcmp(t, "stop")) { it->loop_break = 1; return; }
    if (!strcmp(t, "skip")) { it->loop_skip  = 1; return; }

    if (starts_with(t, "open door called ")) {
        char rest[MAX_LINE]; strcpy(rest, t + 17); trim(rest);
        char alias[MAX_NAME], port_s[MAX_LINE];
        if (!split_around(rest, " on port ", alias, port_s)) bx_error(line,
            "'open door' needs a name and a port number.",
            "open door called srv on port 9001");
        int port = (int)as_number(eval_expr(it, port_s, line));
        if (port <= 0 || port > 65535) bx_error(line,
            "Port number is out of range. Pick a number between 1 and 65535.",
            "open door called srv on port 9001");

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) bx_error(line,
            "I couldn't open a socket. The computer said no.",
            "try again, or pick a different port");

        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons((uint16_t)port);

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            char msg[MAX_TEXT];
            snprintf(msg, sizeof(msg),
                "I couldn't open port %d (%s). Something else might be using it.",
                port, strerror(errno));
            close(fd);
            bx_error(line, msg, "pick a different port (try a number above 1024)");
        }
        if (listen(fd, 16) < 0) {
            close(fd);
            bx_error(line,
                "I couldn't start listening on that door.",
                "open door called srv on port 9001");
        }

        Socket *s = new_socket_slot(it);
        if (!s) { close(fd); bx_error(line,
            "Too many doors and connections open. Close some first.",
            "close srv"); }
        make_name(alias, s->alias);
        s->fd = fd; s->is_listening = 1; s->open = 1; s->line_born = line.line_number;
        return;
    }

    if (starts_with(t, "wait for knock on ")) {
        char rest[MAX_LINE]; strcpy(rest, t + 18); trim(rest);
        char door_alias[MAX_NAME], guest_alias[MAX_NAME];
        if (!split_around(rest, " save ", door_alias, guest_alias) &&
            !split_around(rest, " called ", door_alias, guest_alias))
            bx_error(line,
                "'wait for knock' needs a door, and a name for the guest.",
                "wait for knock on srv save guest");
        Socket *door = find_socket(it, door_alias);
        if (!door) bx_error(line,
            "I don't see a door with that name.",
            "open door called srv on port 9001");
        if (!door->is_listening) bx_error(line,
            "That's a connection, not a door. You can only get knocks on doors.",
            "open door called srv on port 9001");

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int cfd = accept(door->fd, (struct sockaddr *)&client_addr, &client_len);
        if (cfd < 0) {
            char msg[MAX_TEXT];
            snprintf(msg, sizeof(msg), "Couldn't accept the knock (%s).", strerror(errno));
            bx_error(line, msg, "make sure the door is still open");
        }
        Socket *g = new_socket_slot(it);
        if (!g) { close(cfd); bx_error(line,
            "Too many sockets open. Close some first.",
            "close guest"); }
        make_name(guest_alias, g->alias);
        g->fd = cfd; g->is_listening = 0; g->open = 1; g->line_born = line.line_number;
        return;
    }

    if (starts_with(t, "call door ")) {
        char *body = t + 10;
        while (*body == ' ') body++;
        if (!starts_with(body, "at ")) bx_error(line,
            "'call door' wants 'at \"host\"'.",
            "call door at \"127.0.0.1\" on port 9001 save line");
        char *host_start = strchr(body, '"');
        char *host_end   = host_start ? strchr(host_start + 1, '"') : NULL;
        if (!host_start || !host_end) bx_error(line,
            "The host needs to be in quotes.",
            "call door at \"127.0.0.1\" on port 9001 save line");
        char host[MAX_TEXT];
        size_t hl = (size_t)(host_end - host_start - 1);
        if (hl >= sizeof(host)) hl = sizeof(host) - 1;
        strncpy(host, host_start + 1, hl); host[hl] = '\0';

        char *after = host_end + 1;
        char *on_port = strstr(after, "on port ");
        if (!on_port) bx_error(line,
            "Missing 'on port N'.",
            "call door at \"127.0.0.1\" on port 9001 save line");
        char tail[MAX_LINE]; strcpy(tail, on_port + 8); trim(tail);
        char port_s[MAX_LINE], alias_buf[MAX_LINE];
        if (!split_around(tail, " save ", port_s, alias_buf) &&
            !split_around(tail, " called ", port_s, alias_buf))
            bx_error(line,
                "Missing 'save NAME' for the connection.",
                "call door at \"127.0.0.1\" on port 9001 save line");
        char alias[MAX_NAME];
        strncpy(alias, alias_buf, sizeof(alias) - 1); alias[sizeof(alias) - 1] = '\0';
        int port = (int)as_number(eval_expr(it, port_s, line));
        if (port <= 0 || port > 65535) bx_error(line,
            "Port out of range.",
            "pick a port between 1 and 65535");

        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[16]; snprintf(port_str, sizeof(port_str), "%d", port);
        if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
            char msg[MAX_TEXT];
            snprintf(msg, sizeof(msg), "I couldn't find host '%s'.", host);
            if (res) freeaddrinfo(res);
            bx_error(line, msg, "use \"127.0.0.1\" or a real hostname");
        }
        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) { freeaddrinfo(res); bx_error(line,
            "I couldn't open a socket.",
            "try again"); }
        if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
            char msg[MAX_TEXT];
            snprintf(msg, sizeof(msg),
                "I couldn't reach %s on port %d (%s). Is anything listening over there?",
                host, port, strerror(errno));
            close(fd); freeaddrinfo(res);
            bx_error(line, msg, "open door called srv on port 9001  (on the other side first)");
        }
        freeaddrinfo(res);

        Socket *s = new_socket_slot(it);
        if (!s) { close(fd); bx_error(line,
            "Too many sockets open. Close some first.",
            "close line"); }
        make_name(alias, s->alias);
        s->fd = fd; s->is_listening = 0; s->open = 1; s->line_born = line.line_number;
        return;
    }

    if (starts_with(t, "send ") && strstr(t, " to ")) {
        char what[MAX_LINE], alias[MAX_LINE];
        split_around_outside_quotes(t + 5, " to ", what, alias);
        trim(what); trim(alias);
        Socket *s = find_socket(it, alias);
        if (!s) bx_error(line,
            "I don't see a socket with that name.",
            "send \"hello\" to line");
        if (s->is_listening) bx_error(line,
            "You can't 'send' to a door. Send to a guest or a connection.",
            "wait for knock on srv save guest   then   send \"hi\" to guest");
        Value v = eval_expr(it, what, line);
        char buf[SOCKET_BUFFER];
        value_to_string(v, buf, sizeof(buf));
        size_t len = strlen(buf);
        ssize_t sent = send(s->fd, buf, len, 0);
        if (sent < 0) {
            char msg[MAX_TEXT];
            snprintf(msg, sizeof(msg), "Send didn't work (%s).", strerror(errno));
            bx_error(line, msg, "the other side may have hung up");
        }
        return;
    }

    if (starts_with(t, "open file ")) {
        char *q1 = strchr(t, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        if (!q1 || !q2) bx_error(line,
            "'open file' needs a path in quotes.",
            "open file \"diary.txt\" called f");
        char path[MAX_TEXT];
        size_t pl = (size_t)(q2 - q1 - 1);
        if (pl >= sizeof(path)) pl = sizeof(path) - 1;
        strncpy(path, q1 + 1, pl); path[pl] = '\0';
        char *called = strstr(q2 + 1, " called ");
        if (!called) bx_error(line,
            "'open file' needs 'called NAME'.",
            "open file \"diary.txt\" called f");
        char alias[MAX_NAME]; strcpy(alias, called + 8); trim(alias);
        char *toread  = strstr(alias, " to read");     if (toread)  *toread  = '\0';
        char *forread = strstr(alias, " for reading"); if (forread) *forread = '\0';
        char *append  = strstr(alias, " to append");   if (append)  *append  = '\0';
        trim(alias);
        const char *mode = "w";
        if (strstr(t, " to read") || strstr(t, " for reading")) mode = "r";
        else if (strstr(t, " to append")) mode = "a";
        FILE *fp = fopen(path, mode);
        if (!fp) {
            char msg[MAX_TEXT];
            snprintf(msg, sizeof(msg),
                "I couldn't open '%s'. The path may be wrong, or you may not have permission.",
                path);
            bx_error(line, msg, "double-check the path");
        }
        OpenFile *of = NULL;
        for (int fi = 0; fi < it->file_count; fi++)
            if (!it->open_files[fi].open) { of = &it->open_files[fi]; break; }
        if (!of) {
            if (it->file_count >= MAX_OPEN_FILES) {
                fclose(fp);
                bx_error(line, "Too many files open. Close some first.", "close f");
            }
            of = &it->open_files[it->file_count++];
        }
        make_name(alias, of->alias); of->fp = fp; of->open = 1; of->line_born = line.line_number;
        return;
    }

    if (starts_with(t, "write ") && strstr(t, " to ")) {
        char what[MAX_LINE], alias[MAX_LINE];
        split_around_outside_quotes(t + 6, " to ", what, alias);
        trim(what); trim(alias);
        OpenFile *of = find_open_file(it, alias);
        if (!of) bx_error(line,
            "That file isn't open. Open it first.",
            "open file \"diary.txt\" called f");
        Value v = eval_expr(it, what, line);
        char buf[MAX_TEXT];
        value_to_string(v, buf, sizeof(buf));
        fputs(buf, of->fp);
        return;
    }

    if (starts_with(t, "read ") && strstr(t, " save ")) {
        char alias[MAX_LINE], varname[MAX_NAME];
        split_around(t + 5, " save ", alias, varname);
        trim(alias); trim(varname);

        Socket *sock = find_socket(it, alias);
        if (sock) {
            if (sock->is_listening) bx_error(line,
                "You can't 'read' from a door. Wait for a knock first.",
                "wait for knock on srv save guest   then   read guest save msg");
            char buf[SOCKET_BUFFER];
            ssize_t n = recv(sock->fd, buf, sizeof(buf) - 1, 0);
            if (n < 0) {
                char msg[MAX_TEXT];
                snprintf(msg, sizeof(msg), "Read didn't work (%s).", strerror(errno));
                bx_error(line, msg, "the connection may have closed");
            }
            buf[n] = '\0';
            set_var(it, varname, text_value(buf));
            return;
        }

        OpenFile *of = find_open_file(it, alias);
        if (!of) bx_error(line,
            "Nothing is open with that name (file or socket).",
            "open file \"diary.txt\" called f to read");
        char buf[MAX_TEXT * 16]; int len = 0; int c2;
        while ((c2 = fgetc(of->fp)) != EOF && len < (int)sizeof(buf) - 1) buf[len++] = (char)c2;
        buf[len] = '\0';
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';
        set_var(it, varname, text_value(buf));
        return;
    }

    if (starts_with(t, "close ")) {
        char alias[MAX_NAME]; strcpy(alias, t + 6); trim(alias);
        Socket *sock = find_socket(it, alias);
        if (sock) {
            close(sock->fd);
            sock->open = 0;
            return;
        }
        OpenFile *of = find_open_file(it, alias);
        if (!of) bx_error(line,
            "Nothing is open with that name.",
            "close f   /   close guest");
        fclose(of->fp); of->open = 0;
        return;
    }

    if (starts_with(t, "call ")) { eval_expr(it, t, line); return; }

    if (starts_with(t, "wait for ") || starts_with(t, "wait ")) {
        char name[MAX_NAME];
        if (starts_with(t, "wait for ")) strcpy(name, t + 9);
        else                              strcpy(name, t + 5);
        trim(name);
        char safe[MAX_NAME]; make_name(name, safe);
        for (int i = 0; i < it->thread_count; i++) {
            if (!strcmp(it->threads[i].name, safe) && it->threads[i].active) {
                pthread_join(it->threads[i].thread, NULL);
                it->threads[i].active = 0;
                free(it->threads[i].job); it->threads[i].job = NULL;
                return;
            }
        }
        const char *names[MAX_THREADS]; int n = 0;
        for (int i = 0; i < it->thread_count && n < MAX_THREADS; i++)
            if (it->threads[i].active) names[n++] = it->threads[i].name;
        bx_error_suggest(line,
            "I don't see a helper with that name.", safe,
            "wait for worker",
            names, n);
    }

    if (starts_with(t, "how many ")) {
        Value v = eval_expr(it, t + 9, line);
        if (v.type == VALUE_LIST)      printf("%d\n", v.list_count);
        else if (v.type == VALUE_TEXT) printf("%zu\n", strlen(v.text));
        else                            printf("0\n");
        return;
    }

    if (starts_with(t, "add ") && strstr(t, " to ")) {
        char item[MAX_LINE], list_name[MAX_LINE];
        split_around_outside_quotes(t + 4, " to ", item, list_name);
        Var *v = find_var(it, list_name);
        if (!v || v->value.type != VALUE_LIST) {
            char nm[MAX_NAME]; make_name(list_name, nm);
            char msg[256];
            snprintf(msg, sizeof(msg), "There's no list called '%s'.", nm);
            bx_error(line, msg, "my friends are [\"Ana\", \"Bea\"]");
        }
        if (v->value.list_count >= MAX_LIST_ITEMS) bx_error(line,
            "That list is full. Take something out first.",
            "take \"X\" from my friends");
        Value iv = eval_expr(it, item, line);
        char buf[MAX_TEXT];
        value_to_string(iv, buf, sizeof(buf));
        pthread_mutex_lock(&it->state_mutex);
        snprintf(v->value.list[v->value.list_count++], MAX_LIST_ITEM, "%s", buf);
        pthread_mutex_unlock(&it->state_mutex);
        return;
    }

    if ((starts_with(t, "take ") || starts_with(t, "remove ")) && strstr(t, " from ")) {
        char item[MAX_LINE], list_name[MAX_LINE];
        split_around_outside_quotes(strchr(t, ' ') + 1, " from ", item, list_name);
        Var *v = find_var(it, list_name);
        if (!v || v->value.type != VALUE_LIST) bx_error(line,
            "That isn't a list, so I can't take from it.",
            "take \"Ana\" from my friends");
        Value iv = eval_expr(it, item, line);
        char wanted[MAX_TEXT];
        if (iv.type == VALUE_TEXT) snprintf(wanted, sizeof(wanted), "%s", iv.text);
        else                       value_to_string(iv, wanted, sizeof(wanted));
        pthread_mutex_lock(&it->state_mutex);
        for (int i = 0; i < v->value.list_count; i++) {
            if (!strcmp(v->value.list[i], wanted)) {
                for (int j = i; j < v->value.list_count - 1; j++)
                    strcpy(v->value.list[j], v->value.list[j + 1]);
                v->value.list_count--;
                pthread_mutex_unlock(&it->state_mutex);
                return;
            }
        }
        pthread_mutex_unlock(&it->state_mutex);
        return;
    }

    if (starts_with(t, "put ") && strstr(t, " in ")) {
        char value_text[MAX_LINE], target_text[MAX_LINE];
        split_around_outside_quotes(t + 4, " in ", value_text, target_text);

        int through_arrow = 0;
        char arrow_candidate[MAX_LINE]; strcpy(arrow_candidate, target_text);
        if (ends_with(arrow_candidate, "'s box")) {
            arrow_candidate[strlen(arrow_candidate) - 6] = '\0'; trim(arrow_candidate);
            through_arrow = 1;
            strcpy(target_text, arrow_candidate);
        } else if (ends_with(arrow_candidate, " box")) {
            char with[MAX_LINE]; strcpy(with, arrow_candidate);
            with[strlen(with) - 4] = '\0'; trim(with);
            if (find_arrow(it, with)) {
                through_arrow = 1;
                strcpy(target_text, with);
            }
        }
        Value val = eval_expr(it, value_text, line);
        pthread_mutex_lock(&it->state_mutex);
        if (through_arrow) {
            Arrow *ar = find_arrow_locked(it, (char[MAX_NAME]){0});
            char nm[MAX_NAME]; make_name(target_text, nm);
            ar = find_arrow_locked(it, nm);
            Box *bx = ar ? find_box_locked(it, ar->target) : NULL;
            if (!ar) {
                pthread_mutex_unlock(&it->state_mutex);
                bx_error(line,
                    "I don't see an arrow with that name.",
                    "make an arrow called ptr to age");
            }
            if (!bx || !bx->alive) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "Your arrow '%s' points at a box that was already returned.", ar->name);
                pthread_mutex_unlock(&it->state_mutex);
                bx_error(line, msg, "don't write through an arrow whose box was returned");
            }
            bx->value = val;
        } else {
            char nm[MAX_NAME]; make_name(target_text, nm);
            Box *bx = find_box_locked(it, nm);
            if (!bx) {
                pthread_mutex_unlock(&it->state_mutex);
                const char *names[MAX_SYMBOLS]; int n = 0;
                for (int i = 0; i < it->box_count && n < MAX_SYMBOLS; i++) names[n++] = it->boxes[i].name;
                char msg[256];
                snprintf(msg, sizeof(msg), "There is no box called '%s'.", nm);
                bx_error_suggest(line, msg, nm,
                    "make a box called age   then   put 7 in age",
                    names, n);
            }
            if (!bx->alive) {
                pthread_mutex_unlock(&it->state_mutex);
                bx_error(line,
                    "That box was already returned. You can't put things in a box you gave back.",
                    "borrow a fresh box first");
            }
            bx->value = val;
        }
        pthread_mutex_unlock(&it->state_mutex);
        return;
    }

    if (parse_assignment(t, a, b)) {
        if (find_constant(it, a)) bx_error(line,
            "You tried to change something you said to remember. Use a different name.",
            "remembered things stay the same");
        Value rv = eval_expr(it, b, line);
        char nm[MAX_NAME]; make_name(a, nm);
        Box *bx = find_box(it, a);
        if (bx) {
            if (!bx->alive) bx_error(line,
                "That box was already returned. You can't put things in it anymore.",
                "borrow a new box");
            pthread_mutex_lock(&it->state_mutex);
            bx->value = rv;
            pthread_mutex_unlock(&it->state_mutex);
        } else {
            set_var(it, a, rv);
        }
        return;
    }

    bx_error(line,
        "I didn't understand that line.",
        "say \"hello\"   /   make a box called age   /   put 7 in age");
}

/* file loading */

static void push_line(Line *lines, int *count, int number, const char *text) {
    if (*count >= MAX_LINES) {
        fprintf(stderr, "Hmm, that program has too many lines.\n");
        exit(1);
    }
    char buf[MAX_LINE]; strncpy(buf, text, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    trim(buf);
    if (!buf[0] || buf[0] == '#') return;
    lines[*count].line_number = number;
    strncpy(lines[*count].text, buf, MAX_LINE - 1);
    lines[*count].text[MAX_LINE - 1] = '\0';
    (*count)++;
}

/* Splits one-line "if X { body } otherwise { body }" forms into separate
 * logical lines so the block parser can handle them. */
static void expand_inline_braces(const char *raw, int number, Line *lines, int *count) {
    int in_q = 0;
    const char *first_brace = NULL;
    for (const char *p = raw; *p; p++) {
        if (*p == '"') in_q = !in_q;
        if (!in_q && *p == '{') { first_brace = p; break; }
    }
    if (!first_brace) { push_line(lines, count, number, raw); return; }
    {
        const char *p = first_brace + 1;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) { push_line(lines, count, number, raw); return; }
    }

    char buf[MAX_LINE]; strncpy(buf, raw, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char *cursor = buf;
    while (*cursor) {
        char *open = NULL;
        in_q = 0;
        for (char *p = cursor; *p; p++) {
            if (*p == '"') in_q = !in_q;
            if (!in_q && *p == '{') { open = p; break; }
        }
        if (!open) {
            char rest[MAX_LINE]; strcpy(rest, cursor); trim(rest);
            if (rest[0]) push_line(lines, count, number, rest);
            return;
        }
        char *close = NULL;
        int depth = 0;
        in_q = 0;
        for (char *p = open; *p; p++) {
            if (*p == '"') in_q = !in_q;
            if (in_q) continue;
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) { close = p; break; } }
        }
        if (!close) {
            push_line(lines, count, number, cursor);
            return;
        }
        char head[MAX_LINE];
        size_t hl = (size_t)(open - cursor + 1);
        if (hl >= sizeof(head)) hl = sizeof(head) - 1;
        strncpy(head, cursor, hl); head[hl] = '\0';
        trim(head);
        if (head[0]) push_line(lines, count, number, head);

        char body[MAX_LINE];
        size_t bl = (size_t)(close - open - 1);
        if (bl >= sizeof(body)) bl = sizeof(body) - 1;
        strncpy(body, open + 1, bl); body[bl] = '\0';
        trim(body);
        if (body[0]) push_line(lines, count, number, body);

        cursor = close + 1;
        char *tail = cursor;
        while (*tail && isspace((unsigned char)*tail)) tail++;
        if (!*tail) {
            push_line(lines, count, number, "}");
            return;
        }
        if (starts_with(tail, "otherwise")) {
            const char *after = tail + 9;
            while (*after && isspace((unsigned char)*after)) after++;
            if (*after == '{') {
                push_line(lines, count, number, "} otherwise {");
                cursor = (char *)(after + 1);
                continue;
            } else {
                push_line(lines, count, number, "} otherwise {");
                cursor = (char *)after;
                continue;
            }
        }
        push_line(lines, count, number, "}");
        cursor = tail;
    }
}

static int load_file(const char *path, Line *lines) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Hmm, I couldn't open '%s'. Is the path right?\n", path);
        exit(1);
    }
    char raw[MAX_LINE]; int count = 0, number = 0, block_comment = 0;
    while (fgets(raw, sizeof(raw), f)) {
        number++; trim(raw);
        if (starts_with(raw, "###")) { block_comment = !block_comment; continue; }
        if (block_comment || !raw[0] || raw[0] == '#') continue;
        expand_inline_braces(raw, number, lines, &count);
    }
    fclose(f);
    return count;
}

static void init_interpreter(Interpreter *it) {
    memset(it, 0, sizeof(*it));
    pthread_mutex_init(&it->state_mutex, NULL);
}

/* end-of-program checks for forgotten boxes, files and sockets */

static int report_postmortem(Interpreter *it, int strict) {
    int leaks = 0, files_open = 0, sockets_open = 0;
    for (int i = 0; i < it->box_count; i++)
        if (it->boxes[i].borrowed && it->boxes[i].alive) leaks++;
    for (int i = 0; i < it->file_count; i++)
        if (it->open_files[i].open) files_open++;
    for (int i = 0; i < it->socket_count; i++)
        if (it->sockets[i].open) sockets_open++;

    if (!leaks && !files_open && !sockets_open) return 0;

    fprintf(stderr, "\n--- Boxy: a friendly check-up ---\n");
    if (leaks) {
        for (int i = 0; i < it->box_count; i++) {
            if (it->boxes[i].borrowed && it->boxes[i].alive) {
                fprintf(stderr,
                    "  You borrowed a box called '%s' on line %d but never returned it. Borrowed boxes must come back!\n",
                    it->boxes[i].name, it->boxes[i].line_born);
            }
        }
    }
    if (files_open) {
        for (int i = 0; i < it->file_count; i++)
            if (it->open_files[i].open)
                fprintf(stderr,
                    "  You opened the file '%s' on line %d but never closed it.\n",
                    it->open_files[i].alias, it->open_files[i].line_born);
    }
    if (sockets_open) {
        for (int i = 0; i < it->socket_count; i++)
            if (it->sockets[i].open)
                fprintf(stderr,
                    "  You left a %s called '%s' open (from line %d).\n",
                    it->sockets[i].is_listening ? "door" : "connection",
                    it->sockets[i].alias, it->sockets[i].line_born);
    }
    fprintf(stderr, "----------------------------------\n");
    return strict ? 1 : 0;
}

/* main */

static void print_banner(void) {
    printf("Boxy %s — a programming language for kids.\n", BOXY_VERSION);
    printf("Type './boxy --help' to see what you can do.\n\n");
}

int main(int argc, char **argv) {
    srand((unsigned int)time(NULL));

    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h"))) {
        puts(HELP_TEXT);
        return 0;
    }

    if (argc >= 2 && !strcmp(argv[1], "lesson")) {
        if (argc < 3) {
            fprintf(stderr, "Boxy needs a lesson number. Try: boxy lesson 1\n");
            return 1;
        }
        int n = atoi(argv[2]);
        if (n <= 0) {
            fprintf(stderr, "Boxy lessons start at 1. Try: boxy lesson 1\n");
            return 1;
        }
        char path[256];
        snprintf(path, sizeof(path), "lessons/lesson%02d.bx", n);
        char *new_argv[16];
        int new_argc = 0;
        new_argv[new_argc++] = argv[0];
        new_argv[new_argc++] = (char *)"run";
        new_argv[new_argc++] = path;
        for (int i = 3; i < argc && new_argc < 15; i++) new_argv[new_argc++] = argv[i];
        new_argv[new_argc] = NULL;
        argv = new_argv;
        argc = new_argc;
    }

    if (argc < 3 || strcmp(argv[1], "run") != 0) {
        print_banner();
        printf("Use:  %s run program.bx\n",  argc ? argv[0] : "boxy");
        printf("Or:   %s lesson 1\n",        argc ? argv[0] : "boxy");
        printf("Help: %s --help\n",          argc ? argv[0] : "boxy");
        return 1;
    }

    int quiet = 0, strict = 0, visual = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--quiet"))       quiet = 1;
        else if (!strcmp(argv[i], "--strict")) strict = 1;
        else if (!strcmp(argv[i], "--visual")) visual = 1;
        else {
            fprintf(stderr, "Hmm, I don't know the option '%s'.\n", argv[i]);
            return 1;
        }
    }

    Line *lines = calloc(MAX_LINES, sizeof(Line));
    Interpreter *it = calloc(1, sizeof(Interpreter));
    if (!lines || !it) {
        fprintf(stderr, "Hmm, the computer ran out of memory.\n");
        free(lines); free(it);
        return 1;
    }

    int count = load_file(argv[2], lines);
    init_interpreter(it);
    it->visual = visual;

    if (!quiet) print_banner();

    run_block(it, lines, 0, count);

    for (int i = 0; i < it->thread_count; i++)
        if (it->threads[i].active) {
            pthread_join(it->threads[i].thread, NULL);
            free(it->threads[i].job);
            it->threads[i].active = 0;
        }
    int rc = report_postmortem(it, strict);
    for (int i = 0; i < it->lock_count; i++)
        if (it->locks[i].initialized) pthread_mutex_destroy(&it->locks[i].mutex);
    pthread_mutex_destroy(&it->state_mutex);
    free(lines); free(it);
    return rc;
}
