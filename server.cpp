// The skeleton of this was generated by Thrift.

#include "Bitbox.h"
#include <protocol/TBinaryProtocol.h>
#include <server/TSimpleServer.h>
#include <transport/TServerSocket.h>
#include <transport/TBufferTransports.h>

#include <vector>
#include <iterator>
#include <algorithm>
#include <sys/poll.h>
#include <stdio.h>
#include <inttypes.h>
#include <signal.h>

#include "bitbox.h"
#include "sigh.h"

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using boost::shared_ptr;

static int cleanup_running = 0;

gboolean idle_cleanup(gpointer data)
{
    fprintf(stderr, "IN THE CALLBACK\n");
    bitbox_t * box = (bitbox_t *)data;
    bitbox_cleanup_single_step(box, BITBOX_ITEM_LIMIT);
    cleanup_running = bitbox_cleanup_needed(box);
    if(cleanup_running)
        fprintf(stderr, "continuing the cleanup\n");
    else
        fprintf(stderr, "no more cleanup needed\n");
    return cleanup_running ? TRUE : FALSE;
}

class BitboxHandler : virtual public BitboxIf {
    private:
        void schedule_cleanup()
        {
            if(!cleanup_running)
            {
                fprintf(stderr, "ADD THE CALLBACK\n");
                g_idle_add(idle_cleanup, this->box);
                cleanup_running = 1;
            }
        }
    public:
        bitbox_t * box;

        BitboxHandler() {
            this->box = bitbox_new();
        }

        bool get_bit(const std::string& key, const int64_t bit)
        {
            return bitbox_get_bit(this->box, key.c_str(), bit);
        }

        void set_bit(const std::string& key, const int64_t bit)
        {
            this->schedule_cleanup();
            bitbox_set_bit(this->box, key.c_str(), bit);
        }

        void set_bits(const std::string& key, const std::set<int64_t> & bits)
        {
            this->schedule_cleanup();
            // convert to a plain c array
            std::vector<int64_t> vbits;
            int64_t * abits = (int64_t *)malloc(bits.size() * sizeof(int64_t));
            std::copy(bits.begin(), bits.end(), abits);

            bitbox_set_bits(this->box, key.c_str(), abits, bits.size());

            free(abits);
        }

        void shutdown()
        {
            bitbox_shutdown(this->box);
            bitbox_free(this->box);
        }
};

gboolean server_prepare_callback(GSource * source, gint * timeout_) {
    *timeout_ = -1;
    return FALSE;
}

typedef struct {
    GSource source;
    GPollFD poll_fd;
} ServerSource;

gboolean server_check_callback(GSource * source)
{
    ServerSource * ssource = (ServerSource *)source;
    //fprintf(stderr, "IO IN? %d\n", ssource->poll_fd.revents & G_IO_IN ? 1 : 0);
    return ssource->poll_fd.revents & G_IO_IN ? TRUE : FALSE;
}

TSimpleServer * global_server = NULL;
gboolean server_dispatch_callback(GSource * source, GSourceFunc callback, gpointer user_data)
{
    // XXX use g_source_set_callback() to do the callback properly without a global.
    fprintf(stderr, "SERVE\n");
    global_server->serve();
    return TRUE;
}

static GMainLoop * loop;

gboolean signal_check(gpointer data)
{
    if(sigh_poll((sigset_t *)data))
        g_main_loop_quit(loop);
    return TRUE;
}

int main(int argc, char **argv) {
  int port = 9090;

  sigset_t sigs = sigh_make_sigset(SIGINT, SIGTERM, 0);
  assert(sigh_watch(&sigs));

  shared_ptr<BitboxHandler> handler(new BitboxHandler());
  shared_ptr<TProcessor> processor(new BitboxProcessor(handler));
  shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
  shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

  TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory, true);
  global_server = &server;
  serverTransport->listen(); // XXX should catch TTransportException
  std::vector<struct pollfd> fds = serverTransport->getFDs();

  // add the server polling source to the main loop

  loop = g_main_loop_new(NULL, FALSE);

  GSourceFuncs sourcefuncs;

  sourcefuncs.prepare = server_prepare_callback;
  sourcefuncs.check = server_check_callback;
  sourcefuncs.dispatch = server_dispatch_callback;
  sourcefuncs.finalize = NULL;
  sourcefuncs.closure_callback = NULL;
  sourcefuncs.closure_marshal = NULL;

  for(std::vector<struct pollfd>::iterator it = fds.begin(); it != fds.end(); ++it)
  {
      ServerSource * source = (ServerSource *)g_source_new(&sourcefuncs, sizeof(ServerSource));
      source->poll_fd.fd = it->fd;
      source->poll_fd.events = G_IO_IN;
      g_source_add_poll((GSource *)source, &source->poll_fd);
      g_source_attach((GSource *)source, NULL);
  }

  g_idle_add(idle_cleanup, handler->box);
  g_timeout_add_seconds(1, signal_check, &sigs);

  // start the main loop

  g_main_loop_run(loop);

  // shutdown

  handler->shutdown();
  fprintf(stderr, "shutdown cleanly.\n");

  return 0;
}

