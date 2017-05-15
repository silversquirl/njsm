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
    NJSMException(string msg) : message(msg) {}
    virtual const char *what() const throw()
    {
      return message.c_str();
    }

  protected:
    string message;
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

    void find_ports();
};

NJSMJackClient::NJSMJackClient()
{
  activated = false;
  client = jack_client_open("NJSM", JackNoStartServer, nullptr);

  if (client == nullptr)
    throw NJSMException("Creating JACK client failed");

  jack_set_client_registration_callback(client, client_reg_cb, this);

  find_ports();

  if (!jack_activate(client))
    activated = true;
  else
    throw NJSMException("Activating JACK client failed");
}

NJSMJackClient::~NJSMJackClient()
{
  if (activated)
    jack_deactivate(client);
  jack_client_close(client);
}

void NJSMJackClient::find_ports()
{
  const char **ports = jack_get_ports(client, nullptr, nullptr, 0);
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
  if (reg) {
    me->managed_clients.insert(name);
  } else {
    me->managed_clients.erase(name);
  }
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

  DEBUG("Creating OSC address");
  lo_address osc_addr = lo_address_new_from_url(nsm_url);
  if (osc_addr == nullptr) {
    cerr << "Error creating OSC address" << endl;
    return -1;
  }
  DEBUG("Address created. Port: " << lo_address_get_port(osc_addr));

  DEBUG("Creating OSC server");
  lo_server osc_server = lo_server_new_with_proto(
      nullptr, // Autodetect port
      lo_address_get_protocol(osc_addr), // Use the same protocol as NSM
      [](int num, const char *msg, const char *where){
        cerr << "Error creating OSC server: " << msg << endl;
      });
  if (osc_server == nullptr) {
    return -1;
  }

  bool ack = false;
  DEBUG("Registering /reply callback");
  lo_server_add_method(osc_server, "/reply", "ssss",
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
  lo_send(osc_addr, "/nsm/server/announce", "sssiii",
      "NJSM", "::", argv[0], 1, 2, getpid());

  while (!ack) lo_server_recv(osc_server);

  try {
    NJSMJackClient saver;
    DEBUG("Waiting");
    pause();
  } catch (exception &e) {
    cerr << e.what() << endl;
    return -1;
  }

  return 0;
}

