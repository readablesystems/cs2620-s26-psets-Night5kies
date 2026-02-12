export LD_LIBRARY_PATH=$HOME/.local/lib
export PATH=$PATH:~/.local/bin
cmake -B build
(cd build; cmake --build .)

(killall rpcg-server; build/rpcg-server& sleep 0.5; build/rpcg-client)

First, I tried setting it up myself on Windows with the help of ChatGPT, however, I was tinkering with it for a while. I then thought I had it working since I could run it, however, after trying to make a bunch of changes to it, it wasn’t rebuilding with the updated code changes that I was making. It took me a while to realize this because the performance kept varying when I ran it multiple times. I then added another line to the Done print statements that printed the maximum in_flights. When this didn’t show, I realized it wasn’t actually using updated code. Then I switched to codespaces from the instructions on the ed, and now it seems to work.

Base Code Performance:
sent 1995 RPCs per sec

Modified Client to be Async and use windowing with Completion Queue:
Window size 50 - sent 3891 RPCs per sec 
Window size 32 - sent 4043 RPCs per sec
Window size 64 - sent 4211 RPCs per sec
Window size 128 - sent 3490 RPCs per sec

It seems that adding the Async with windowing improved the RPCs/sec quite a bit. However, the performance varies. I got worse ones around 3500 as well, but also some around 4200. Out of the different window sizes, it seems like a window size of 64 did best.

Copy Avoidance:
sent 4138 RPCs per sec

Similarish performance to async with windowing. I think the difference is due to the variation on a run to run basis

Turned off GZIP Compression
sent 6348 RPCs per sec

Made a pretty signifigant improvement


Used rpclib instead of gRPC:
sent 30390 RPCs per sec

SIGNFICANT improvement, much more than the toher things I tried.

Implemented Batching:
sent 142557 RPCs per sec

Again, significant improvement Went with a batch size of 128, others had similar performance and was hard to tell what was best since they were all very similar because of the varaince