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
- [x] Streams (XADD, XRANGE, XREAD)
- [ ] Replication
- [ ] Transactions
- [ ] Geo commands