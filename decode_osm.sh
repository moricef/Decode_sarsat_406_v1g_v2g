#! /bin/bash
# sox -t alsa default -t wav - lowpass 3000 highpass 400  2>/dev/null | ./dec406_V7 --osm
# sox  -t alsa hw:0,0  -t wav - lowpass 3000 highpass 400 2>/dev/null |./dec406_V7  --osm
sox  -d  -t wav - lowpass 3000 highpass 400 2>/dev/null |./dec406 --osm




