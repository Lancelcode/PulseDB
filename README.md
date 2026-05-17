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

# SET with expiry in seconds
redis-cli set temp "bye" EX 5
# Expected: OK

redis-cli ttl temp
# Expected: 5 (or less)

redis-cli pttl temp
# Expected: ~5000 (or less)

# Wait 5 seconds, then:
redis-cli get temp
# Expected: (nil)

# Key with no expiry
redis-cli set permanent "here"
redis-cli ttl permanent
# Expected: -1

# Missing key
redis-cli ttl missing
# Expected: -2
```

## Features

- [x] TCP server
- [x] RESP protocol parser
- [x] PING / ECHO
- [x] SET / GET
- [x] Expiry (EX, PX, TTL, PTTL)
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