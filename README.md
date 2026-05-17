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

Start the server, then in a second terminal:

```bash
redis-cli ping
```

You should see the parsed command printed in the server terminal:
client connected
parsed command with 1 arg(s):
[0] PING

## Features

- [x] TCP server
- [x] RESP protocol parser
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