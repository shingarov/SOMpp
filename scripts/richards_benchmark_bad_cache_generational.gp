set terminal postscript eps enhanced solid color
set output "richards_benchmark_bad_cache_generational.eps"
set title "Richards Benchmark"
set datafile separator "," #csv is comma separated
set yrange [0:]      #plot starting from 0
set ylabel "Average execution time (ms)"
set style data histograms  #plot histogram style
set style fill solid 1.00 border 0 #fill bars
set style histogram errorbars gap 2 lw 1
set key left
plot "benchmark_results/generational_nocache_noTagging_richards.csv" using 2:3:xtic(1) ti "SOM++ generational (no cache)", \
     "benchmark_results/generational_badcache_noTagging_richards.csv" using 2:3 ti "SOM++ generational (bad cache)", \
     "benchmark_results/generational_cache_noTagging_richards.csv" using 2:3 ti "SOM++ generational (with cache)"