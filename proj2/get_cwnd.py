import os
import re

def main():
	fin_cwnd = open("proj2-part2.cwnd")
	fout_sl_cwnd = open("slowstart.cwnd", "w")
	fout_ca_cwnd = open("normal.cwnd", "w") #congection avoid
	lines_cwnd = fin_cwnd.readlines()
	old_cwnd = 0
	for line_cwnd in lines_cwnd:
		nodes = line_cwnd.split(" ")
		time_cwnd = nodes[0]
		new_cwnd = nodes[1]
		slowstart = int(nodes[2])
		if slowstart==0:
			fout_sl_cwnd.write(time_cwnd + " " + str(new_cwnd) + "\n")
		else:
			fout_ca_cwnd.write(time_cwnd + " " + str(new_cwnd) + "\n")
		old_cwnd = new_cwnd

if __name__ == "__main__":
    main()
