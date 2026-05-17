# PulseDB

A Redis clone built from scratch in C (C99), implementing the RESP protocol
and core Redis commands.

## Build

```bash
make
```

## Run

```bash
./pulsedb
```

## Testing

Start the server, then in a second terminal:

```bash
# PING
redis-cli ping
# Expected: PONG

# ECHO
redis-cli echo "hello world"
# Expected: hello world

# SET and GET
redis-cli set name "pulsedb"
redis-cli get name
# Expected: pulsedb

# SET with expiry
redis-cli set temp "bye" EX 5
redis-cli ttl temp
# Expected: 5 (or less)

# DEL and EXISTS
redis-cli set a 1
redis-cli set b 2
redis-cli del a b missing
# Expected: 2

redis-cli exists a
# Expected: 0

# TYPE
redis-cli set mykey "hello"
redis-cli type mykey
# Expected: string

# INCR — creates key if missing, starting from 0
redis-cli incr counter
# Expected: 1

redis-cli incr counter
# Expected: 2

# DECR
redis-cli decr counter
# Expected: 1

# INCRBY
redis-cli incrby counter 10
# Expected: 11

# DECRBY
redis-cli decrby counter 5
# Expected: 6

# Error on non-integer value
redis-cli set foo "bar"
redis-cli incr foo
# Expected: (error) ERR value is not an integer or out of range
```

## Features

- [x] TCP server
- [x] RESP protocol parser
- [x] PING / ECHO
- [x] SET / GET
- [x] Expiry (EX, PX, TTL, PTTL)
- [x] DEL, EXISTS, TYPE
- [x] INCR / DECR
- [ ] Lists
- [ ] Hashes
- [ ] Sorted Sets
- [ ] RDB persistence
- [ ] Streams
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands