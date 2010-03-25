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

key = 'persistence'
assert client.get_bit(key, 0) == 1
assert client.get_bit(key, 1) == 1
assert client.get_bit(key, 2) == 1
assert client.get_bit(key, 3) == 1
assert client.get_bit(key, 4) == 1
assert client.get_bit(key, 5) == 1
assert client.get_bit(key, 6) == 1
assert client.get_bit(key, 7) == 1
assert client.get_bit(key, 8) == 1
assert client.get_bit(key, 9) == 1
assert client.get_bit(key, 10) == 1
assert client.get_bit(key, 11) == 1
assert client.get_bit(key, 12) == 1
assert client.get_bit(key, 13) == 1
assert client.get_bit(key, 14) == 1
assert client.get_bit(key, 15) == 1
assert client.get_bit(key, 16) == 1
assert client.get_bit(key, 17) == 1
assert client.get_bit(key, 18) == 1
assert client.get_bit(key, 19) == 1
assert client.get_bit(key, 20) == 0
assert client.get_bit(key, 200) == 0
assert client.get_bit(key, 2000) == 0
