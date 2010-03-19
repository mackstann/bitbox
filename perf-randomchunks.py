import sys, time, random
sys.path.append('gen-py')

from bitbox import Bitbox
from bitbox.constants import *

from thrift import Thrift
from thrift.transport import TSocket
from thrift.transport import TTransport
from thrift.protocol import TBinaryProtocol

transport = TSocket.TSocket('localhost', 9090)
transport = TTransport.TBufferedTransport(transport)
protocol = TBinaryProtocol.TBinaryProtocol(transport)

client = Bitbox.Client(protocol)

transport.open()

keys = set()
total_set = 0
iterations = 0

while True:
    index = random.randint(0, 29999)
    value_range_start = random.randint(0, 100000)

    client.set_bits('foo%d' % index, range(value_range_start, value_range_start+500))

    keys.add(index)
    total_set += 500
    iterations += 1

    print "have set %d bits on %d different keys in %d iterations" % (total_set, len(keys), iterations)
