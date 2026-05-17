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
# PING with no args
redis-cli ping
# Expected: PONG

# PING with a message
redis-cli ping "hello"
# Expected: hello

# ECHO
redis-cli echo "hello world"
# Expected: hello world
```

## Features

- [x] TCP server
- [x] RESP protocol parser
- [x] PING / ECHO
- [ ] SET / GET
- [ ] Expiry (EX, PX, TTL, PTTL)
- [ ] DEL, EXISTS, TYPE
- [ ] INCR / DECR
- [ ] Lists
- [ ] Hashes
- [ ] Sorted Sets
- [ ] RDB persistence
- [ ] Streams
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands