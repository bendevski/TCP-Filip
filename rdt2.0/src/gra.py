#!/usr/bin/env python3
# -*- coding: utf-8 -*-


import matplotlib.pyplot as plt


fp = open("CWND.csv")
cong = []
time = []


for n in fp:
    n = n.strip().split(',')
    time.append(int(n[0]))
    cong.append(int(n[1]))


fig = plt.figure(figsize=(30, 8))
ax = fig.add_subplot(111)
ax.plot(time, cong)

plt.xlabel('Time', fontsize=20)
plt.ylabel('Congestion', fontsize=20)

plt.savefig("Congestion Window Graph with time.pdf", dpi=350)

plt.show()
fp.close()
