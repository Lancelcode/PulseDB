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
# Geo
redis-cli geoadd locations -0.1276 51.5074 "london"
redis-cli geoadd locations -73.9857 40.7484 "newyork"

redis-cli geodist locations london newyork km
# Expected: ~5570 km

redis-cli geopos locations london
# Expected: -0.1276..., 51.5074...
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
- [x] Transactions (MULTI, EXEC, DISCARD)
- [x] Geo commands (GEOADD, GEODIST, GEOPOS)