import os
import re

def main():
	# stream = os.popen("tcpdump -nn -tt -r tcp-large-transfer-0-0.pcap >> rawdata")
	fin1 = open("rawdata0")
	fin2 = open("rawdata1")
	fout_re = open("retransmission.data", "w")
	fout_no = open("normal.data", "w")
	lines1 = fin1.readlines()
	lines2 = fin2.readlines()
	pattern = re.compile(r'\d+.+seq\s\d+')
	time = re.compile(r'\d+\.\d+')
	seq = re.compile(r'\d+')
	max_seq = 0
	# genereate retransmission.data and normal.data
	# plot "normal.cwnd" using 1:2 title 'normal' with linespoints, "retransmission.cwnd" using 1:2 title 'retransmission' 
	for line in lines1:
		res = pattern.findall(line)
		if len(res) == 1:
			new_seq = int(seq.findall(res[0])[-1])
			time_val = float(time.findall(res[0])[0])
			if new_seq < max_seq:
				# write into retransmission
				fout_re.write(str(time_val) + " " +str(new_seq) + "\n")
			else:
				# write into normal
				fout_no.write(str(time_val) + " " +str(new_seq) + "\n")
			max_seq = max(new_seq, max_seq)
	
	for line in lines2:
		res = pattern.findall(line)
		if len(res) == 1:
			new_seq = int(seq.findall(res[0])[-1])
			time_val = float(time.findall(res[0])[0])
			if new_seq < max_seq:
				# write into retransmission
				fout_re.write(str(time_val) + " " +str(new_seq) + "\n")
			else:
				# write into normal
				fout_no.write(str(time_val) + " " +str(new_seq) + "\n")
			max_seq = max(new_seq, max_seq)

if __name__ == "__main__":
    main()