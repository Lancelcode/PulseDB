#!/bin/sh
# A work queue: producers RPUSH jobs, a worker waits for them with BLPOP,
# and a transaction updates two counters atomically.
# Start a server first (./pulsedb), then run: examples/job_queue.sh
set -e
CLI="${PULSEDB_CLI:-./pulsedb-cli} -p ${PULSEDB_PORT:-6379}"

$CLI del example:jobs example:done example:failed > /dev/null

echo "== Worker starts and blocks waiting for a job (5 s timeout)"
$CLI blpop example:jobs 5 &
WORKER=$!
sleep 0.5

echo "== Producer enqueues three jobs; the waiting worker receives the first"
$CLI rpush example:jobs resize-image send-email rebuild-index
wait $WORKER

echo "== Remaining queue"
$CLI lrange example:jobs 0 -1

echo "== Record the outcome atomically with MULTI/EXEC"
printf 'multi\nincr example:done\nincrby example:failed 0\nexec\n' | $CLI

echo "== An empty queue times out with (nil)"
$CLI del example:jobs > /dev/null
$CLI blpop example:jobs 0.2
