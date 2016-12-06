./waf --run proj2-part4-seq
./waf --run proj2-part1
tcpdump -nn -tt -r proj2-part1-0-0.pcap > rawdata
tcpdump -nn -tt -r proj2-part4-3-0.pcap > rawdata4
python get_seq_p1.py
python get_seq_p4.py
gnuplot compare_seq
