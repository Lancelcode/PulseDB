#!/bin/sh
# A game leaderboard on a sorted set.
# Start a server first (./pulsedb), then run: examples/leaderboard.sh
set -e
CLI="${PULSEDB_CLI:-./pulsedb-cli} -p ${PULSEDB_PORT:-6379}"

$CLI del example:leaderboard > /dev/null

echo "== Recording scores"
$CLI zadd example:leaderboard 1200 alice 950 bob 1430 carol 1100 dave

echo "== Bob has a good round (scores are replaced, not added)"
$CLI zadd example:leaderboard 1500 bob

echo "== Full table, lowest first"
$CLI zrange example:leaderboard 0 -1 withscores

echo "== Players between 1000 and 1450 points"
$CLI zrangebyscore example:leaderboard 1000 1450

echo "== Alice's rank (0 = lowest score) and score"
$CLI zrank example:leaderboard alice
$CLI zscore example:leaderboard alice
