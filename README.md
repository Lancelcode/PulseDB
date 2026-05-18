██████╗ ██╗   ██╗██╗     ███████╗███████╗██████╗ ██████╗
██╔══██╗██║   ██║██║     ██╔════╝██╔════╝██╔══██╗██╔══██╗
██████╔╝██║   ██║██║     ███████╗█████╗  ██║  ██║██████╔╝
██╔═══╝ ██║   ██║██║     ╚════██║██╔══╝  ██║  ██║██╔══██╗
██║     ╚██████╔╝███████╗███████║███████╗██████╔╝██████╔╝
╚═╝      ╚═════╝ ╚══════╝╚══════╝╚══════╝╚═════╝ ╚═════╝

A Redis-compatible server built from scratch in C99

````

$ cat problem.txt
Redis is one of the most widely used pieces of infrastructure in the world.
Most engineers use it daily without knowing how it actually works inside.

PulseDB is a ground-up reimplementation of the Redis server — the wire
protocol, the in-memory store, expiry, persistence, streams, transactions,
and geo — written in C99 with no dependencies, to understand the layer
that most engineers never touch.

$ cat stack.txt
{
  "language"    : "C99",
  "protocol"    : "RESP (Redis Serialisation Protocol)",
  "build"       : "GNU Make + GCC",
  "persistence" : "RDB file format — loaded on startup",
  "testing"     : "redis-cli — speaks standard RESP, drop-in compatible",
  "dependencies": "none"
}

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

$ cat architecture.txt
Client ──TCP──▶ RESP parser ──▶ Command dispatcher ──▶ In-memory store ──▶ Response

Every connection is read into a buffer, parsed as a RESP array,
dispatched to the matching handler, and responded to before the
connection closes. One thread. No event loop library. No magic.

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

$ git clone && make
git clone https://github.com/Lancelcode/PulseDB.git
cd PulseDB
make
./pulsedb
# pulsedb starting...
# pulsedb listening on port 6379

$ redis-cli ping
PONG

$ redis-cli set name pulsedb
OK

$ redis-cli get name
"pulsedb"

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

Built by Djiby Sow Rebollo — Edinburgh, Scotland
````
