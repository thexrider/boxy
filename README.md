# Boxy

A friendly little programming language for kids.

Boxy starts with `say "hello"` and ends with two helpers chatting over a real
TCP socket. You make boxes, borrow boxes, point arrows at boxes, and write
tiny programs that do real things. Errors are gentle and try to suggest a
fix — Boxy never shouts.

Aimed at kids around 7 to 10. Their grown-up reading along will recognise
variables, the stack and the heap, pointers, threads, mutexes, file handles
and TCP sockets — all hiding behind boxes, helpers and doors.

## Install

```
git clone <this-repo> boxy
cd boxy
cc boxy.c -o boxy -pthread -lm
```

Boxy is one C file. No libraries to install. Works on macOS and Linux.

## Run

```
./boxy run examples/hello.bx        # run a program
./boxy lesson 1                     # run lesson 1
./boxy --help                       # the cheat sheet
```

Useful flags:

- `--quiet` — skip the welcome line
- `--strict` — turn forgotten borrowed boxes / open files into a non-zero exit code
- `--visual` — when you `say` a box, draw it as a little ASCII box

## The 12 lessons

Run any lesson with `./boxy lesson N`.

| #  | Lesson         | Teaches                              |
|----|----------------|--------------------------------------|
| 1  | hello          | your first program                   |
| 2  | boxes          | making and using a box               |
| 3  | asking         | reading the user's answer            |
| 4  | math           | plus, minus, times, divided by, …    |
| 5  | choosing       | `if` / `otherwise`                   |
| 6  | loops          | `repeat`, `count from`, `while`      |
| 7  | functions      | `teach`, `give`, `call`              |
| 8  | lists          | many things in a row                 |
| 9  | files          | a tiny diary                         |
| 10 | borrowed boxes | the box you must return              |
| 11 | helpers        | two workers, one shared counter      |
| 12 | doors          | a tiny chat over a TCP socket        |

Each lesson fits on one screen and adds one new idea.

## The whole language, on one page

### Saying things and asking

```
say "hello"
say "hi, ", name
ask "what is your name?" save name
```

### Boxes

```
make a box called age
put 7 in age
say age

# short form
age is 7
```

### Borrowed boxes (you must return them)

```
borrow a box called score
put 100 in score
return score
```

Forget to `return` and Boxy tells you, by name and line number, when the
program ends.

### Math

`plus`, `minus`, `times`, `divided by`, `mod`, `to` (power), `root of`,
`random between A and B`.

### Constants

```
remember PI as 3.14
```

Trying to change `PI` is a friendly error.

### Comparing

`is`, `is not`, `is at least`, `is at most`, `is greater than`,
`is less than`, `starts with`, `ends with`, `contains`.

### Choosing

```
if age is at least 18 {
    say "grown up"
} otherwise {
    say "kid"
}
```

### Loops

```
repeat 3 times { say "hi" }
count from 1 to 10 as i { say i }
while x is less than 10 { x is x plus 1 }
```

`stop` leaves the loop. `skip` goes to the next round.

### Functions

```
teach square with n {
    give n times n
}
say call square with 5
```

### Lists

```
my friends are ["Ana", "Bea", "Caio"]
add "Diogo" to my friends
take "Ana" from my friends
show my friends
how many my friends
```

### Arrows (pointers)

```
make a box called target
put 100 in target

make an arrow called ptr to target
say inside ptr             # 100
put 999 in ptr's box       # writes through the arrow
```

If you `return` a borrowed box while an arrow still points at it and then
read through that arrow, Boxy stops with a clear, friendly error.

### Files

```
open file "/tmp/diary.txt" called f
write "today I tried Boxy\n" to f
close f

open file "/tmp/diary.txt" called f to read
read f save text
say text
close f
```

Modes: default = write, `to read`, `to append`. Forget `close` and Boxy
reminds you when the program ends.

### Helpers (threads) and locks

```
counter is 0

start helper called worker {
    hold counter
    counter is counter plus 1
    let go counter
}

wait for worker
say counter
```

A `helper` is a real thread (4 MB stack). `hold` / `let go` is a real
mutex, made the first time you mention it.

### Doors (TCP sockets)

A `door` is a server socket. A `knock` is an incoming connection. A
`guest` is who walks in. `call door at HOST on port N` connects out.

```
start helper called server {
    open door called srv on port 9001
    wait for knock on srv save guest
    read guest save msg
    send msg to guest
    close guest
    close srv
}

sleep 200 ms
call door at "127.0.0.1" on port 9001 save line
send "hello" to line
read line save reply
say reply
close line
wait for server
```

### Sleep

```
sleep 200          # milliseconds
sleep 1 second
```

### Comments

```
# one line

###
many lines
###
```

## Examples

| File                     | Teaches                                |
|--------------------------|----------------------------------------|
| `examples/hello.bx`      | the smallest program                   |
| `examples/name.bx`       | ask + say                              |
| `examples/adder.bx`      | functions                              |
| `examples/shopping.bx`   | lists                                  |
| `examples/diary.bx`      | files                                  |
| `examples/borrow.bx`     | borrowed boxes                         |
| `examples/arrow.bx`      | arrows / pointers                      |
| `examples/helpers.bx`    | helpers and locks                      |
| `examples/echo.bx`       | a tiny TCP echo server + client        |
| `examples/chat.bx`       | two helpers chatting over a socket     |

Run any of them with `./boxy run examples/<name>.bx`.

## What you'll have learned

By the end of the 12 lessons, a child has informally touched:

- variables, expressions, conditions, loops, functions
- the stack and the heap (kept boxes vs borrowed boxes)
- pointers, dereferencing, dangling pointers
- threads and mutexes
- file and socket handles, and the difference between them
- a real TCP byte stream

That's a lot of grown-up programming hiding inside a small, friendly
language whose hardest word is `borrow`.

## Author and license

The_X_Rider &lt;the_x_rider@proton.me&gt;

Boxy is licensed under **GPL-3.0-or-later** — see [LICENSE](LICENSE).
That means you are free to use, study, share, and modify Boxy. If you
distribute a modified version, that version must also stay free under
the same license. Boxy will always be free software.
