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
# Sorted Sets
redis-cli zadd scores 1.0 alice 2.0 bob 3.0 charlie
# Expected: 3

redis-cli zrange scores 0 -1
# Expected: alice, bob, charlie

redis-cli zrange scores 0 -1 WITHSCORES
# Expected: alice 1, bob 2, charlie 3

redis-cli zrank scores bob
# Expected: 1

redis-cli zcard scores
# Expected: 3

redis-cli zscore scores alice
# Expected: 1

redis-cli zrangebyscore scores 1 2
# Expected: alice, bob
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
- [ ] CONFIG GET, KEYS
- [ ] RDB persistence
- [ ] Streams
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands