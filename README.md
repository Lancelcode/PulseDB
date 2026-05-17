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

# SET with expiry
redis-cli set temp "bye" EX 5
redis-cli ttl temp
# Expected: 5 (or less)

# DEL single key
redis-cli del name
# Expected: 1

redis-cli get name
# Expected: (nil)

# DEL multiple keys
redis-cli set a 1
redis-cli set b 2
redis-cli del a b missing
# Expected: 2

# EXISTS
redis-cli set foo "bar"
redis-cli exists foo
# Expected: 1

redis-cli exists missing
# Expected: 0

# EXISTS multiple keys
redis-cli exists foo foo missing
# Expected: 2

# TYPE
redis-cli set mykey "hello"
redis-cli type mykey
# Expected: string

redis-cli type missing
# Expected: none
```

## Features

- [x] TCP server
- [x] RESP protocol parser
- [x] PING / ECHO
- [x] SET / GET
- [x] Expiry (EX, PX, TTL, PTTL)
- [x] DEL, EXISTS, TYPE
- [ ] INCR / DECR
- [ ] Lists
- [ ] Hashes
- [ ] Sorted Sets
- [ ] RDB persistence
- [ ] Streams
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands