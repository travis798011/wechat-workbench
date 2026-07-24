#!/bin/bash
export PATH="/Users/travis/.local/bin:$PATH"
DIR="/Users/travis/projects/debtor_ticket/weixin-workbench"

kill $(lsof -ti:3028) 2>/dev/null
kill $(lsof -ti:5173) 2>/dev/null
sleep 1

cd $DIR/packages/server
nohup npx tsx src/index.ts > /tmp/server.log 2>&1 &
echo "Backend PID $!"

cd $DIR/packages/web
nohup npx vite --port 5173 --host > /tmp/vite.log 2>&1 &
echo "Frontend PID $!"

sleep 3
echo "Done. http://localhost:5173"
