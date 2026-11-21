#!/bin/bash
sleep 4
[ -z "$DISPLAY" ] && export DISPLAY=:0
export GDK_BACKEND=wayland,x11
cd /home/puregene/puregene/stage3/PureGene || exit 1
exec ./puregene_app > /home/puregene/puregene/stage3/PureGene/autostart.log 2>&1
