#!/usr/bin/gnuplot
if (!exists("filename")) exit error "please run with -e 'filename=...'"

set term pngcairo size 1000,1000 font ",10"
set out sprintf("%s.png", filename)
set xlabel "Set operation probability, %"
set key top center horizontal outside

set multiplot layout 3,2 rowsfirst

color(str) = str eq "map_global" ? 1 : str eq "map_rwlock" ? 2 : str eq "map_striped" ? 3 : 5

do for [net in "blocking nonblocking uv"] {
	set title net
	set ylabel "Latency, µs"
	plot \
		for [storage in "map_global map_rwlock map_striped"] \
			filename using "probability":(column(sprintf("%s_%s_Get_Latency", net, storage))):(color(storage)) \
			w lp lw 1.5 ps 1.5 lc variable pt 1 dt 1 not, \
		for [storage in "map_global map_rwlock map_striped"] \
			filename using "probability":(column(sprintf("%s_%s_Set_Latency", net, storage))):(color(storage)) \
			w lp lw 1.5 ps 1.5 lc variable pt 2 dt 4 not, \
		0/0 w lp lw 1.5 ps 1.5 pt 1 dt 1 lc 4 t "Set", \
		0/0 w lp lw 1.5 ps 1.5 pt 2 dt 4 lc 4 t "Get", \
		0/0 w l lw 1.5 lc 1 t "GlobalLock", \
		0/0 w l lw 1.5 lc 2 t "RWLock", \
		0/0 w l lw 1.5 lc 3 t "Striped"

	set ylabel "Throughput, s^{-1}"
	plot \
		for [storage in "map_global map_rwlock map_striped"] \
			filename using "probability":(column(sprintf("%s_%s_Get_Throughput", net, storage))):(color(storage)) \
			w lp lw 1.5 ps 1.5 lc variable pt 1 dt 1 not, \
		for [storage in "map_global map_rwlock map_striped"] \
			filename using "probability":(column(sprintf("%s_%s_Set_Throughput", net, storage))):(color(storage)) \
			w lp lw 1.5 ps 1.5 lc variable pt 2 dt 4 not, \
		0/0 w lp lw 1.5 ps 1.5 pt 1 dt 1 lc 4 t "Set", \
		0/0 w lp lw 1.5 ps 1.5 pt 2 dt 4 lc 4 t "Get", \
		0/0 w l lw 1.5 lc 1 t "GlobalLock", \
		0/0 w l lw 1.5 lc 2 t "RWLock", \
		0/0 w l lw 1.5 lc 3 t "Striped"
}

unset multiplot
