#!/bin/bash
# Quick dev loop: build firmware, start emulator, open browser
set -e

echo "🔨 Building firmware..."
sh build.sh > /tmp/fm1_build.log 2>&1 || { echo "Build failed:"; tail -20 /tmp/fm1_build.log; exit 1; }

echo "✅ Build complete. Starting emulator on port 8642..."
# Start emulator in background; dev_emulator.sh blocks, so put it in bg
tools/dev_emulator.sh 8642 > /tmp/fm1_emu.log 2>&1 &
EMU_PID=$!

# Wait for the emulator to be ready (check if port is listening)
echo "⏳ Waiting for emulator to be ready..."
for i in {1..30}; do
  if nc -z localhost 8642 2>/dev/null; then
    echo "✅ Emulator ready on http://localhost:8642"
    break
  fi
  sleep 1
done

# Try to open the browser
if command -v open > /dev/null 2>&1; then
  open "http://localhost:8642"
elif command -v xdg-open > /dev/null 2>&1; then
  xdg-open "http://localhost:8642"
elif command -v start > /dev/null 2>&1; then
  start "http://localhost:8642"
fi

echo ""
echo "🎮 Emulator running. Edit files and re-run this script to rebuild and reload."
echo "   Logs: build=$tmp/fm1_build.log, emulator=/tmp/fm1_emu.log"
echo "   Press Ctrl+C to stop the emulator."
echo ""

# Keep the script running so the emulator stays alive
wait $EMU_PID 2>/dev/null || true
