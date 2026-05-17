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

## RDB persistence

On startup, PulseDB looks for `./dump.rdb` and loads any string keys it finds.
To test with a real RDB file, generate one from Redis:

```bash
redis-cli set foo bar
redis-cli bgsave
# copy dump.rdb to your PulseDB working directory
./pulsedb
redis-cli get foo
# Expected: bar
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
- [x] RDB persistence
- [ ] Streams
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands