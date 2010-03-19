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

for i in range(30000000):
    if i % 10000 == 0:
        print i
    client.set_bit('foo%d' % i, 0)