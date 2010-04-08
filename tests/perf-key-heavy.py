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

prefix = str('%0.12f' % random.random())

for i in xrange(7000):
    key = prefix + str(i)
    client.set_bit(key, 0)
    if i % 100 == 0:
        assert client.get_bit(key, 0) == 1
        assert client.get_bit(key, 1) == 0
