SDLscale fix is a small library for allowing easy ports from the GCW0, Dingux and similar to the Arcade Mini.
It is based on sdlfix, which was a similar library for the RS-97, with a slightly different purpose.
It intercepts the video calls and upscales them to 480x272, which is the native resolution of the Arcade Mini and PAP K3 Plus.
A version for the PAP K3S is also available : this device uses a much higher resolution of 800x480.

Resolutions with a width of 256 and 320 have a fast path available, with a much faster scaling. (other than IPU, which is not available yet)
Other resolutions are also supported but those will be slower.

Usage
======

Use it with LD_PRELOAD and load your app with it.

#!/bin/sh
LD_PRELOAD=./sdlfix.so ./DinguxCommander.dge

Make sure said app is dynamic linked to libSDL.so.
