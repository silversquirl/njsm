#include <iostream>
#include <unordered_set>
#include <string>
using namespace std;

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <jack/jack.h>
#include <jack/session.h>

#include <lo/lo.h>

#define DEBUG(...) cerr << __VA_ARGS__ << endl;

class NJSMException : public exception {
  public:
    NJSMException(string msg) : message(msg.c_str()) {}
    NJSMException(const char *msg) : message(msg) {}
    virtual const char *what() const throw()
    {
      return message;
    }

  protected:
    const char *message;
};

class NJSMJackClient {
  public:
    NJSMJackClient();
    ~NJSMJackClient();

  private:
    static void client_reg_cb(const char *name, int reg, void *sess_saver);

  protected:
    jack_client_t *client;
    unordered_set<string> managed_clients;
    bool activated;

    void findPorts();
};

NJSMJackClient::NJSMJackClient()
{
  DEBUG("Initializing JACK connection");

  activated = false;
  DEBUG("Creating JACK client");
  client = jack_client_open("NJSM", JackNoStartServer, nullptr);

  if (client == nullptr)
    throw NJSMException("Creating JACK client failed");

  DEBUG("Adding client registration callback");
  jack_set_client_registration_callback(client, client_reg_cb, this);

  findPorts();

  DEBUG("Activating JACK client");
  if (!jack_activate(client))
    activated = true;
  else
    throw NJSMException("Activating JACK client failed");
}

NJSMJackClient::~NJSMJackClient()
{
  DEBUG("Cleaning up JACK client");
  if (activated)
    jack_deactivate(client);
  jack_client_close(client);
  DEBUG("JACK cleanup complete");
}

void NJSMJackClient::findPorts()
{
  DEBUG("Detecting JACK ports");
  const char **ports = jack_get_ports(client, nullptr, nullptr, 0);
  DEBUG("Adding clients to managed client set");
  string client_name;
  for (int i = 0; ports[i]; i++) {
    client_name = ports[i];
    client_name = client_name.substr(0, client_name.find(":"));
    managed_clients.insert(client_name);
  }
}

void NJSMJackClient::client_reg_cb(const char *name, int reg, void *saver)
{
  NJSMJackClient *me = reinterpret_cast<NJSMJackClient *>(saver);
  DEBUG("New client detected");
  if (reg) {
    me->managed_clients.insert(name);
  } else {
    me->managed_clients.erase(name);
  }
}

class NJSMNonClient {
  public:
    NJSMNonClient(char *nsm_url);
    ~NJSMNonClient();

  protected:
    lo_address addr;
    lo_server server;

    NJSMJackClient jack;
};

NJSMNonClient::NJSMNonClient(char *nsm_url) : jack()
{
  DEBUG("Initializing Non connection");

  DEBUG("Creating OSC address");
  addr = lo_address_new_from_url(nsm_url);
  if (addr == nullptr) 
    throw NJSMException("Error creating OSC address");

  DEBUG("Address created. Port: " << lo_address_get_port(addr));

  DEBUG("Creating OSC server");
  server = lo_server_new_with_proto(
      nullptr, // Autodetect port
      lo_address_get_protocol(addr), // Use the same protocol as NSM
      [](int num, const char *msg, const char *where){
        throw NJSMException("Error creating OSC Server");
      });
  if (server == nullptr)
    throw NJSMException("Error creating OSC Server");

  bool ack = false;
  DEBUG("Registering /reply callback");
  lo_server_add_method(server, "/reply", "ssss",
    [](
      const char *path, const char *types,
      lo_arg **argv, int argc,
      lo_message msg, void *data) -> int
    {
      bool *ack = reinterpret_cast<bool *>(data);
      if (argc < 4) return 1;

      string pth = &argv[0]->s;
      if (pth == "/nsm/server/announce") {
        DEBUG("Connected to " << &argv[2]->s <<
            " with message '" << &argv[1]->s);
        *ack = true;
        return 0;
      }

      return 1;
    },
    &ack);

  DEBUG("Sending announce");
  lo_send(addr, "/nsm/server/announce", "sssiii",
      "NJSM", "::", "njsm", 1, 2, getpid());

  while (!ack) lo_server_recv(server);

  lo_server_del_method(server, "/reply", "ssss");
}

NJSMNonClient::~NJSMNonClient()
{
  DEBUG("Cleaning up Non client");
  lo_server_free(server);
  lo_address_free(addr);
  DEBUG("Non cleanup complete");
}

int main(int argc, char *argv[])
{
  signal(SIGINT, [](int){ cout << endl << "Exiting..." << endl; });
  signal(SIGTERM, [](int){ cout << endl << "Terminating..." << endl; });

  char *nsm_url = getenv("NSM_URL");
  if (nsm_url == nullptr) {
    cerr << "NSM_URL is undefined" << endl;
    return -1;
  } else {
    DEBUG("NSM_URL=" << nsm_url);
  }

  try {
    NJSMNonClient non(nsm_url);
    DEBUG("Waiting");
    pause();
  } catch (exception &e) {
    cerr << e.what() << endl;
    return -1;
  }

  return 0;
}

