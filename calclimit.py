from itertools import islice
import statistics

fh = open('result.txt')

while True:
    param = fh.readline()
    if not param:
        break
    next5 = [float(x.split()[1]) for x in list(islice(fh, 5))]
    print(int(statistics.median(next5) * 10) * 1000)

