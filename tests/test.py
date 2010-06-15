import sys, time, random
sys.path.append('gen-py')

from bitbox import Bitbox
from bitbox.constants import *

from thrift import Thrift
from thrift.transport import TSocket
from thrift.transport import TTransport
from thrift.protocol import TBinaryProtocol

transport = TSocket.TSocket('localhost', 9090)
transport = TTransport.TFramedTransport(transport)
protocol = TBinaryProtocol.TBinaryProtocol(transport)

client = Bitbox.Client(protocol)

transport.open()

# set_bits

key = str("%0.12f" % time.time())
client.set_bits(key, range(20))
assert client.get_bit(key, 0) == 1
assert client.get_bit(key, 10) == 1
assert client.get_bit(key, 19) == 1
assert client.get_bit(key, 20) == 0
assert client.get_bit(key, 200) == 0
assert client.get_bit(key, 20000) == 0

key = str("%0.12f" % time.time())
client.set_bits(key, [1, 2, 8])
assert client.get_bit(key, 0) == 0
assert client.get_bit(key, 1) == 1
assert client.get_bit(key, 2) == 1
assert client.get_bit(key, 3) == 0
assert client.get_bit(key, 7) == 0
assert client.get_bit(key, 8) == 1
assert client.get_bit(key, 9) == 0

key = str("%0.12f" % time.time())
client.set_bits(key, [1000000])
assert client.get_bit(key, 999999) == 0
assert client.get_bit(key, 1000000) == 1
assert client.get_bit(key, 1000001) == 0

# set_bit

key = str("%0.12f" % time.time())
client.set_bit(key, 0)
assert client.get_bit(key, 0) == 1
assert client.get_bit(key, 1) == 0

key = str("%0.12f" % time.time())
client.set_bit(key, 1000000)
assert client.get_bit(key, 999999) == 0
assert client.get_bit(key, 1000000) == 1
assert client.get_bit(key, 1000001) == 0

# growing downwards

key = str("%0.12f" % time.time())
client.set_bit(key, 40)
assert client.get_bit(key, 30) == 0
assert client.get_bit(key, 40) == 1
assert client.get_bit(key, 50) == 0

client.set_bit(key, 30)
assert client.get_bit(key, 20) == 0
assert client.get_bit(key, 30) == 1
assert client.get_bit(key, 40) == 1
assert client.get_bit(key, 60) == 0

client.set_bit(key, 20)
assert client.get_bit(key, 10) == 0
assert client.get_bit(key, 20) == 1
assert client.get_bit(key, 30) == 1
assert client.get_bit(key, 40) == 1
assert client.get_bit(key, 60) == 0

client.set_bit(key, 10)
assert client.get_bit(key, 00) == 0
assert client.get_bit(key, 10) == 1
assert client.get_bit(key, 20) == 1
assert client.get_bit(key, 30) == 1
assert client.get_bit(key, 40) == 1
assert client.get_bit(key, 60) == 0

client.set_bit(key, 0)
assert client.get_bit(key, 00) == 1
assert client.get_bit(key, 10) == 1
assert client.get_bit(key, 20) == 1
assert client.get_bit(key, 30) == 1
assert client.get_bit(key, 40) == 1
assert client.get_bit(key, 60) == 0
