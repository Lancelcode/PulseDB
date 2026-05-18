```
██████╗ ██╗   ██╗██╗     ███████╗███████╗██████╗ ██████╗
██╔══██╗██║   ██║██║     ██╔════╝██╔════╝██╔══██╗██╔══██╗
██████╔╝██║   ██║██║     ███████╗█████╗  ██║  ██║██████╔╝
██╔═══╝ ██║   ██║██║     ╚════██║██╔══╝  ██║  ██║██╔══██╗
██║     ╚██████╔╝███████╗███████║███████╗██████╔╝██████╔╝
╚═╝      ╚═════╝ ╚══════╝╚══════╝╚══════╝╚═════╝ ╚═════╝
A Redis-compatible server built from scratch in C99
```

![C](https://img.shields.io/badge/C99-00599C?style=for-the-badge&logo=c&logoColor=white)
![Make](https://img.shields.io/badge/Make-427819?style=for-the-badge&logo=gnu&logoColor=white)
![Redis](https://img.shields.io/badge/RESP_Protocol-FF4438?style=for-the-badge&logo=redis&logoColor=white)
![Platform](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)

Redis is one of the most widely used pieces of infrastructure in the world. Most engineers use it daily without knowing how it actually works inside.

PulseDB is a ground-up reimplementation of the Redis server — the wire protocol, the in-memory store, expiry, persistence, streams, transactions, and geo — written in C99 with no dependencies, to understand the layer that most engineers never touch.

---

```
$ cat problem.txt
Redis is everywhere. It is the default cache, the session store, the
pub/sub bus. Every backend engineer depends on it. Almost none of them
know how it actually works at the protocol level — how a command travels
from redis-cli to the server, how keys expire without a background thread,
how sorted sets stay sorted, how streams handle auto-sequenced IDs.

PulseDB is the answer to that question. Built line by line, decision by
decision, in C99. No libevent. No hiredis. No dependencies.
```

```
$ cat stack.txt
{
  "language"    : "C99",
  "protocol"    : "RESP (Redis Serialisation Protocol)",
  "build"       : "GNU Make + GCC",
  "persistence" : "RDB file format — loaded on startup",
  "testing"     : "redis-cli — speaks standard RESP, drop-in compatible",
  "dependencies": "none"
}
```

```
$ ls -la src/
File            Role
server.c        TCP accept loop, SO_REUSEADDR, client connection handling
resp.c          RESP protocol parser — arrays, bulk strings, integers, nulls
commands.c      All 50+ command handlers and RESP response formatting
store.c         Hash table, doubly-linked lists, sorted arrays, streams
rdb.c           RDB file loader — restores persisted string keys on startup
txn.c           MULTI/EXEC transaction queue, per-connection state
geo.c           52-bit interleaved geohash encoding, Haversine distance
config.c        Server configuration with defaults (port, hz, dir, dbfilename)
```

```
$ cat architecture.txt
Client ──TCP──▶ RESP parser ──▶ Command dispatcher ──▶ In-memory store ──▶ Response

Every connection is read into a buffer, parsed as a RESP array,
dispatched to the matching handler, and responded to before the
connection closes. One thread. No event loop library. No magic.
```

```
$ ls -la commands/
Category        Commands
Strings         SET GET INCR DECR INCRBY DECRBY
Keys            DEL EXISTS TYPE TTL PTTL KEYS
Expiry          SET EX / PX, TTL, PTTL
Lists           LPUSH RPUSH LPOP RPOP LRANGE LLEN BLPOP
Hashes          HSET HGET HGETALL HMGET HDEL HLEN
Sorted Sets     ZADD ZRANGE ZRANGEBYSCORE ZRANK ZSCORE ZCARD
Streams         XADD XRANGE XREAD
Transactions    MULTI EXEC DISCARD
Geo             GEOADD GEODIST GEOPOS
Server          PING ECHO CONFIG GET
```

```
$ cat internals.txt
The interesting parts are not the command handlers. They are the data structures.

RESP parser
  Recursive descent. A single parse_one() calls itself for array elements.
  Handles nested arrays, bulk strings, integers, null bulk strings.

Hash table
  djb2 hashing, 1024 buckets, separate chaining for collisions.
  O(1) average lookup and insert. Keys and values are heap-allocated
  and owned by the store — callers can free their buffers freely.

Lazy expiry
  Expired keys are not swept on a timer. They are evicted on the next
  access. No background thread. No lock contention. This is how Redis does it.

Sorted sets
  A heap-allocated array kept sorted by score at insertion time via
  insertion sort. ZRANGE is a direct array slice — no extra traversal needed.

Streams
  An append-only singly-linked list. Entry IDs are millisecond timestamps
  with a sequence suffix. Auto-generation, explicit, and partial IDs all work.

Geohashing
  Latitude and longitude are each quantised to 26 bits and interleaved
  into a 52-bit integer. Stored as a sorted set score. GEODIST uses
  the Haversine formula over the decoded coordinates.

Transactions
  MULTI sets a flag on the per-connection Txn struct. Subsequent commands
  are deep-copied into a fixed-size queue. EXEC replays them in order.

RDB persistence
  On startup, PulseDB reads ./dump.rdb if present. Handles auxiliary fields,
  database selectors, resize hints, millisecond and second expiry opcodes,
  and string value entries — compatible with real Redis dump files.
```

```
$ git clone && make
git clone https://github.com/Lancelcode/PulseDB.git
cd PulseDB
make
./pulsedb
# pulsedb starting...
# no RDB file found at ./dump.rdb, starting empty
# pulsedb listening on port 6379
```

```
$ redis-cli ping
PONG

$ redis-cli set name pulsedb
OK

$ redis-cli get name
"pulsedb"

$ redis-cli hset user name alice age 30 city edinburgh
(integer) 3

$ redis-cli hgetall user
1) "name"
2) "alice"
3) "age"
4) "30"
5) "city"
6) "edinburgh"

$ redis-cli zadd leaderboard 100 alice 200 bob 150 charlie
(integer) 3

$ redis-cli zrange leaderboard 0 -1 WITHSCORES
1) "alice"
2) "100"
3) "charlie"
4) "150"
5) "bob"
6) "200"

$ redis-cli xadd events '*' action login user alice
"1234567890123-0"

$ redis-cli geoadd locations -3.1883 55.9533 edinburgh
(integer) 1

$ redis-cli geodist locations edinburgh london km
"534.7412"

$ redis-cli multi
OK
$ redis-cli set balance 100
QUEUED
$ redis-cli incr balance
QUEUED
$ redis-cli exec
1) OK
2) (integer) 101
```

```
$ cat roadmap.log
[done] ████████████████████  TCP server              Accept loop, SO_REUSEADDR, BACKLOG 128
[done] ████████████████████  RESP parser             Arrays, bulk strings, integers, nulls
[done] ████████████████████  String commands         SET GET INCR DECR INCRBY DECRBY
[done] ████████████████████  Key commands            DEL EXISTS TYPE TTL PTTL KEYS
[done] ████████████████████  Expiry                  EX PX with lazy eviction on access
[done] ████████████████████  Lists                   Doubly-linked, O(1) push/pop, BLPOP
[done] ████████████████████  Hashes                  Nested hash table, O(1) field access
[done] ████████████████████  Sorted sets             Score-sorted array, ZRANGE slice
[done] ████████████████████  Streams                 XADD auto/explicit IDs, XRANGE, XREAD
[done] ████████████████████  Transactions            MULTI EXEC DISCARD with deep-copy queue
[done] ████████████████████  Geo commands            52-bit geohash, Haversine, GEODIST
[done] ████████████████████  RDB persistence         Load string keys from dump.rdb on startup
[next] ░░░░░░░░░░░░░░░░░░░░  Replication             REPLCONF PSYNC write propagation
[next] ░░░░░░░░░░░░░░░░░░░░  WATCH / UNWATCH         Optimistic locking for transactions
[next] ░░░░░░░░░░░░░░░░░░░░  Active expiry           Background sweep for expired keys
[next] ░░░░░░░░░░░░░░░░░░░░  RDB save                Persist the store to disk on shutdown
```

Part of a larger learning arc  see my [profile](https://github.com/Lancelcode) for the full picture.

---

Built by [Djiby Sow Rebollo](https://github.com/Lancelcode) Edinburgh, Scotland
