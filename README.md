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

# Lists
redis-cli rpush mylist a b c
redis-cli lrange mylist 0 -1
# Expected: a, b, c

# HSET — single field
redis-cli hset user name "alice"
# Expected: 1

# HSET — multiple fields
redis-cli hset user age "30" city "london"
# Expected: 2

# HGET
redis-cli hget user name
# Expected: alice

# HGET missing field
redis-cli hget user missing
# Expected: (nil)

# HGETALL
redis-cli hgetall user
# Expected: name, alice, age, 30, city, london

# HMGET
redis-cli hmget user name age missing
# Expected: alice, 30, (nil)

# HLEN
redis-cli hlen user
# Expected: 3

# HDEL
redis-cli hdel user city
# Expected: 1

redis-cli hlen user
# Expected: 2

# TYPE
redis-cli type user
# Expected: hash
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
- [x] Hashes (HSET, HGET, HGETALL, HMGET, HDEL, HLEN)
- [ ] Sorted Sets
- [ ] RDB persistence
- [ ] Streams
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands