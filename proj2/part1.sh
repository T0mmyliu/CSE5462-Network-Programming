./waf --run "proj2-part1"
tcpdump -nn -tt -r proj2-part1-0-0.pcap > rawdata
python get_seq_p1.py
gnuplot proj2-part1-script
