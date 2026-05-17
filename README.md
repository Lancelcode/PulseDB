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

## Testing the connection

In a second terminal, you can test the server is accepting connections:

```bash
redis-cli ping
```

Or with netcat:

```bash
echo "hello" | nc localhost 6379
```

You should see `client connected` printed in the server terminal.

## Features

- [x] TCP server
- [ ] RESP protocol parser
- [ ] PING / ECHO
- [ ] SET / GET
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