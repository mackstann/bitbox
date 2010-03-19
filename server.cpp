// The skeleton of this was generated by Thrift.

#include "Bitbox.h"
#include <protocol/TBinaryProtocol.h>
#include <server/TSimpleServer.h>
#include <transport/TServerSocket.h>
#include <transport/TBufferTransports.h>

#include <vector>
#include <sys/poll.h>
#include <stdio.h>

extern "C" {
#include "bitbox.h"
}

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using boost::shared_ptr;

class BitboxHandler : virtual public BitboxIf {
    private:
        bitbox_t * box;

    public:
        BitboxHandler() {
            this->box = bitbox_new();
        }

        bool get_bit(const std::string& key, const int32_t bit) {
            return bitbox_get_bit(this->box, key.c_str(), bit);
        }

        void set_bit(const std::string& key, const int32_t bit) {
            bitbox_set_bit(this->box, key.c_str(), bit);
        }

        void set_bits(const std::string& key, const std::set<int32_t> & bits) {
            bitarray_t * b = bitbox_find_array(this->box, key.c_str());
            for(std::set<int32_t>::const_iterator it = bits.begin(); it != bits.end(); ++it)
                bitarray_set_bit(b, *it);
        }
};

gboolean server_prepare_callback(GSource * source, gint * timeout_)
{
    *timeout_ = -1;
    return FALSE;
}

gboolean server_check_callback(GSource * source)
{
    return TRUE;
}

TSimpleServer * global_server = NULL;
gboolean server_dispatch_callback(GSource * source, GSourceFunc callback, gpointer user_data)
{
    global_server->serve();
    return TRUE;
}


int main(int argc, char **argv) {
  int port = 9090;
  shared_ptr<BitboxHandler> handler(new BitboxHandler());
  shared_ptr<TProcessor> processor(new BitboxProcessor(handler));
  shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
  shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

  TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory, true);
  global_server = &server;
  serverTransport->listen();
  std::vector<struct pollfd> fds = serverTransport->getFDs();

  // add the server polling source to the main loop

  GSourceFuncs sourcefuncs;

  sourcefuncs.prepare = server_prepare_callback;
  sourcefuncs.check = server_check_callback;
  sourcefuncs.dispatch = server_dispatch_callback;
  sourcefuncs.finalize = NULL;
  sourcefuncs.closure_callback = NULL;
  sourcefuncs.closure_marshal = NULL;

  GSource * source = g_source_new(&sourcefuncs, sizeof(GSource));

  for(std::vector<struct pollfd>::iterator it = fds.begin(); it != fds.end(); ++it)
  {
      GPollFD * gpfd = g_new(GPollFD, 1);
      gpfd->fd = it->fd;
      gpfd->events = G_IO_IN;
      g_source_add_poll(source, gpfd);
  }

  g_source_attach(source, NULL);

  // add the idle function to the main loop



  // start the main loop

  fprintf(stderr, "%d fds\n", fds.size());
  GMainLoop * loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);
  return 0;
}

