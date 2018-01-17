import pyclickhouse as p

conn = p.Connection('127.0.0.1', 8123)
cur = conn.cursor()

cur.select("""
select distinct emailneu 
from Aktionen_t_rmt
where length(emailneu) > 0
""")

mails = cur.fetchall()

for m in mails:
	cur.insert("replace all '%s' with '' in Aktionen_t_rmt at emailneu" % m['emailneu'])
	print '[%s]' % (m['emailneu'], )

