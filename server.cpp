// The skeleton of this was generated by Thrift.

#include "Bitbox.h"
#include <protocol/TBinaryProtocol.h>
#include <server/TNonblockingServer.h>
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

static bool maintenance_running = false;
gboolean idle_maintenance(gpointer data)
{
    Bitbox * box = static_cast<Bitbox *>(data);
    bool more_maintenance_needed = maintenance_running = box->run_maintenance_step();
    if(!more_maintenance_needed)
        fprintf(stderr, "done with maintenance for now.\n");
    return more_maintenance_needed ? TRUE : FALSE;
}

class BitboxHandler : virtual public BitboxIf {
    private:
        void schedule_maintenance()
        {
            //if(!maintenance_running)
            //{
                //fprintf(stderr, "adding the maintenance callback.\n");
                //g_idle_add(idle_maintenance, &this->box);
                idle_maintenance(&this->box);
                //maintenance_running = true;
            //}
        }
    public:
        Bitbox box;

        bool get_bit(const std::string& key, const int64_t bit)
        {
            return this->box.get_bit(key.c_str(), bit);
        }

        void set_bit(const std::string& key, const int64_t bit)
        {
            this->schedule_maintenance();
            this->box.set_bit(key.c_str(), bit);
        }

        void set_bits(const std::string& key, const std::set<int64_t> & bits)
        {
            this->schedule_maintenance();
            this->box.set_bits(key.c_str(), bits.begin(), bits.end());
        }

        void shutdown()
        {
            this->box.shutdown();
        }
};

//gboolean server_prepare_callback(GSource * source, gint * timeout_) {
//    *timeout_ = -1;
//    return FALSE;
//}
//
//typedef struct {
//    GSource source;
//    GPollFD poll_fd;
//} ServerSource;
//
//gboolean server_check_callback(GSource * source)
//{
//    ServerSource * ssource = (ServerSource *)source;
//    //fprintf(stderr, "IO IN? %d\n", ssource->poll_fd.revents & G_IO_IN ? 1 : 0);
//    return ssource->poll_fd.revents & G_IO_IN ? TRUE : FALSE;
//}
//
//TSimpleServer * global_server = NULL;
//gboolean server_dispatch_callback(GSource * source, GSourceFunc callback, gpointer user_data)
//{
//    // XXX use g_source_set_callback() to do the callback properly without a global.
//    fprintf(stderr, "SERVE\n");
//    global_server->serve();
//    return TRUE;
//}
//
//static GMainLoop * loop;
//
//gboolean signal_check(gpointer data)
//{
//    if(sigh_poll((sigset_t *)data))
//        g_main_loop_quit(loop);
//    return TRUE;
//}

int main(int argc, char **argv) {
  int port = 9090;

  sigset_t sigs = sigh_make_sigset(SIGINT, SIGTERM, 0);
  assert(sigh_watch(&sigs));

  shared_ptr<BitboxHandler> handler(new BitboxHandler());
  shared_ptr<TProcessor> processor(new BitboxProcessor(handler));

  TNonblockingServer server(processor, port);
  //global_server = &server;

  //// add the server polling source to the main loop

  //loop = g_main_loop_new(NULL, FALSE);

  //GSourceFuncs sourcefuncs;

  //sourcefuncs.prepare = server_prepare_callback;
  //sourcefuncs.check = server_check_callback;
  //sourcefuncs.dispatch = server_dispatch_callback;
  //sourcefuncs.finalize = NULL;
  //sourcefuncs.closure_callback = NULL;
  //sourcefuncs.closure_marshal = NULL;

  //for(std::vector<struct pollfd>::iterator it = fds.begin(); it != fds.end(); ++it)
  //{
  //    ServerSource * source = (ServerSource *)g_source_new(&sourcefuncs, sizeof(ServerSource));
  //    source->poll_fd.fd = it->fd;
  //    source->poll_fd.events = G_IO_IN;
  //    g_source_add_poll((GSource *)source, &source->poll_fd);
  //    g_source_attach((GSource *)source, NULL);
  //}

  //g_timeout_add_seconds(1, signal_check, &sigs);

  //// start the main loop

  //g_main_loop_run(loop);

  // shutdown

  fprintf(stderr, "shutting down.\n");
  server.serve();

  event_base_loopbreak(server.getEventBase());
  //server.thread()->join();
  fprintf(stderr, "shutdown cleanly.\n");

  return 0;
}

