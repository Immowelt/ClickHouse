f = open('data-gdpr', 'w')

#f.write("insert into default.testdata values\n")

sep = ''
for id in xrange(1,100000000):
	f.write("%s('%s', %d, '%d@gmail.com')\n" % (sep, '2017-12-01' if id > 700000 else '2017-10-01' if id > 500000 else '2017-11-01', id, id))
	sep = ','

f.close()


