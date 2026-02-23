export LD_LIBRARY_PATH=$HOME/.local/lib
export PATH=$PATH:~/.local/bin
cmake -B build
(cd build; cmake --build .)

(killall rpcg-server; build/rpcg-server& sleep 0.5; build/rpcg-client)
First, I tried setting it up myself on Windows with the help of ChatGPT, however, I was tinkering with it for a while. I then thought I had it working since I could run it, however, after trying to make a bunch of changes to it, it wasn’t rebuilding with the updated code changes that I was making. It took me a while to realize this because the performance kept varying when I ran it multiple times. I then added another line to the Done print statements that printed the maximum in_flights. When this didn’t show, I realized it wasn’t actually using updated code. Then I switched to codespaces from the instructions on the ed, and now it seems to work.

Base Code Performance:
sent 1995 RPCs per sec

Modified Client to be Async and use windowing with Completion Queue:
sent 2827 RPCs per sec

It seems that adding the Async with windowing improved the RPCs/sec quite a bit. However, the performance varies. I got worse ones around 2300 as well, but also some close to 3000. 

Copy Avoidance:
sent 2308 RPCs per sec 

Similarish performance to async with windowing. I think the difference is due to the variation on a run to run basis

Turned off GZIP Compression
sent 2215 RPCs per sec

Didn’t make much of a difference, slightly worse if anything

2 Completion Queue threads, more copy avoidance, notify_one() instead of notify_all() 
sent 2123 RPCs per sec

Not much of a difference

Used rpclib instead of gRPC:
sent 31282 RPCs per sec

SIGNFICANT improvement, much more than the toher things I tried.

