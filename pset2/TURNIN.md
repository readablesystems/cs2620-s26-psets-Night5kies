Problem set 2 turnin
====================

Describe your turnin: What features did you add to `netsim.hh`? How are your
broken `ctconsensus` versions broken? And which random seeds reliably
demonstrate the failures?


I had some setup issues since I was on windows, but eventually was able to work them out with the help of AI. 

In initially testing the ct stubborn.exe, I noticed that my fails were coming up with a window that says ctstubborn.cc was aborting and I couldn't get the seed it was failing on. Then I debugged and found that it had to be using the Release build.
cmake -B build
cmake --build build --config Release


First, I decided to add the jitter to netsim.hh. I decided to go with exponential delay because it didn't seem that difficult to add. I added exponential delay of 100ms and maintaining the base of 20ms. 
Tested the ctconsensus algorithm 10,000,000 times (which is how many times it took me to find a fail in ctstubborn) and it still passed.

I then implemented support for server failure. and tested it again with the ctconsensus algorithm and it passed.

Lastly, I added exponential computation delay of 100ms and this also passed with the ctconsensus test. I also tested ctconsensus with different numbers of servers (3,4,5,7, etc.) and it didn't break


Now, moving onto finding errors with consensus, I built the 3 suggested areas to modify. 
First was weakening the failure detector, I had it fall down to a 15 min wait with 10% probability but struggled to get a fail with higher numbers of servers as well.

Next, I made ctconsensus drop retransmission messages to all other servers besides Nancy, I also got a consensus error with 3 servers immediately on seed 1850235953 from not reaching consensus 15 min of virtual time.
Then I modified it to also send a retransimission message to Nancy and one other server in a ring style and it worked fine for 3 servers in 1000000 tries, but then it failed with 5 servers on seed 1049539823 from 15 minuites of virtual time without consensus.

For the skip state version, I had it skip the both leader and receiver color round update from prepare. I got a consensus error with 3 servers almost immediately on seed 484171403 where Nancy receives DECIDE(red) instead of Blue. 
Then I tried adding back the receiver color round update and still got an error with 3 servers on 426082298 for receiving a DECIDE(red) instead of blue.
Then I tried removing the receiver side color round update but adding back in the leader color round update and got the same error for 3 servers on seed 3357778248. 


After going through the other two changes to ctconsensus, I went back to weakening the failure detector and tried changing the delay to uniform instead of exponential delay anad used uniform(0,1000ms) and got a fail on 7 servers with seed 2053164472 from 15 min of virtual time without consensus. This still passed the original ctconsensus.

All of these tests were done with 1000000 runs.