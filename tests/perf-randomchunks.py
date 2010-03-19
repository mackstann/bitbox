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

range_size = 700
num_keys = 30000
array_size = 1000000

while True:
    index = random.randint(0, num_keys - 1)
    value_range_start = random.randint(0, array_size - range_size - 1)

    client.set_bits('foo%d' % index, range(value_range_start, value_range_start+range_size))

    keys.add(index)
    total_set += range_size
    iterations += 1

    print "have set %d bits on %d different keys in %d iterations" % (total_set, len(keys), iterations)
    if total_set >= 1000*1000*1000:
        raise SystemExit
