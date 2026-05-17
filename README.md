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

# SET and GET
redis-cli set name "pulsedb"
redis-cli get name
# Expected: pulsedb

# INCR / DECR
redis-cli incr counter
# Expected: 1
redis-cli incrby counter 10
# Expected: 11

# LPUSH and RPUSH
redis-cli rpush mylist a b c
# Expected: 3

# LRANGE — full list
redis-cli lrange mylist 0 -1
# Expected: a, b, c

# LRANGE — last two elements
redis-cli lrange mylist -2 -1
# Expected: b, c

# LLEN
redis-cli llen mylist
# Expected: 3

# LPOP and RPOP
redis-cli lpop mylist
# Expected: a

redis-cli rpop mylist
# Expected: c

# BLPOP with timeout
redis-cli blpop mylist 2
# Expected: mylist, b (pops immediately since list has data)

# BLPOP on empty list — waits then returns nil
redis-cli blpop emptylist 1
# Expected: (nil) after 1 second
```

## Features

- [x] TCP server
- [x] RESP protocol parser
- [x] PING / ECHO
- [x] SET / GET
- [x] Expiry (EX, PX, TTL, PTTL)
- [x] DEL, EXISTS, TYPE
- [x] INCR / DECR
- [x] Lists (LPUSH, RPUSH, LPOP, RPOP, LRANGE, LLEN, BLPOP)
- [ ] Hashes
- [ ] Sorted Sets
- [ ] RDB persistence
- [ ] Streams
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands