f = open('data-gdpr', 'w')

#f.write("insert into default.testdata values\n")

sep = ''
for id in xrange(0, 100000):
	for d in xrange(10,20):
		datum = '2017-12-%d' if id > 700000 else '2017-10-%d' if id > 500000 else '2017-11-%d'		
		f.write("%s('%s', %d, '%d@gmail.com')\n" % (sep, datum % d, id, id))
		sep = ','

f.close()


