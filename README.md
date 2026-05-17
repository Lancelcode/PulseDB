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

```bash
# CONFIG GET
redis-cli config get port
# Expected: port, 6379

redis-cli config get *
# Expected: all config key-value pairs

# KEYS
redis-cli set foo 1
redis-cli set bar 2
redis-cli set baz 3

redis-cli keys "*"
# Expected: foo, bar, baz

redis-cli keys "b*"
# Expected: bar, baz

redis-cli keys "f??"
# Expected: foo
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
- [x] Sorted Sets (ZADD, ZRANGE, ZRANK, ZCARD, ZSCORE, ZRANGEBYSCORE)
- [x] CONFIG GET, KEYS
- [ ] RDB persistence
- [ ] Streams
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands