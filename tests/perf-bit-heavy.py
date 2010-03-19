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

key = str('%0.12f' % random.random())

for i in range(30000):
    if i % 100 == 0:
        print i

    if i % 100000 == 0:
        assert client.get_bit(key, i*1000) == 0
        assert client.get_bit(key, (i+1)*1000-1) == 0

    client.set_bits(key, range(i*1000, (i+1)*1000))

    if i % 100000 == 0:
        assert client.get_bit(key, i*1000) == 1
        assert client.get_bit(key, (i+1)*1000-1) == 1
        assert client.get_bit(key, (i+1)*1000) == 0
