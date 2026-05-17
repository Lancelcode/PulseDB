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
# Expected: OK

redis-cli get name
# Expected: pulsedb

# GET a missing key
redis-cli get missing
# Expected: (nil)
```

## Features

- [x] TCP server
- [x] RESP protocol parser
- [x] PING / ECHO
- [x] SET / GET
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