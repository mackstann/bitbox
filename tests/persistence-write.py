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

client.set_bits('persistence', range(20))
