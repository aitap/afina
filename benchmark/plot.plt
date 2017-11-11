#!/usr/bin/gnuplot
if (!exists("filename")) exit error "please run with -e 'filename=...'"

set term pngcairo size 1000,1000 font ",10"
set out sprintf("%s.png", filename)
set ytics nomirror
set y2tics
set xlabel "Set operation probability, %"
set ylabel "Latency, µs"
set y2label "Throughput, s^{-1}"
set key top center horizontal

set multiplot layout 3,1

color(str) = str eq "map_global" ? 1 : str eq "map_rwlock" ? 2 : str eq "map_striped" ? 3 : 5

do for [net in "blocking epoll uv"] {
	set title net
	plot \
		for [storage in "map_global map_rwlock map_striped"] \
			filename using "probability":(column(sprintf("%s_%s_Get", net, storage))):(color(storage)) \
			w lp lc variable pt 1 axes x1y1 not, \
		for [storage in "map_global map_rwlock map_striped"] \
			filename using "probability":(column(sprintf("%s_%s_Set", net, storage))):(color(storage)) \
			w lp lc variable pt 2 axes x1y1 not, \
		for [storage in "map_global map_rwlock map_striped"] \
			filename using "probability":(column(sprintf("%s_%s_Throughput", net, storage))):(color(storage)) \
			w lp lc variable pt 3 dt 2 not axes x1y2, \
	0/0 w lp pt 1 lc 4 t "Set", \
	0/0 w lp pt 2 lc 4 t "Get", \
	0/0 w lp pt 3 dt 2 lc 4 t "Throughput", \
	0/0 w lp lc 1 pt 4 t "GlobalLock", \
	0/0 w lp lc 2 pt 4 t "RWLock", \
	0/0 w lp lc 3 pt 4 t "Striped"
}


unset multiplot
