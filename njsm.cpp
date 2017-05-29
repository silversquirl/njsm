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

class NJSMExitException : public NJSMException {
  public:
    NJSMExitException() : NJSMException("Exitting...") {}
};

class NJSMSaveException : public NJSMException {
  public:
    NJSMSaveException() : NJSMException("An error occurred while saving the JACK session") {}
};

class NJSMJackClient {
  public:
    NJSMJackClient() : client(nullptr), activated(false), save_dir(nullptr) {}
    ~NJSMJackClient();

    void activate(const char *client_name);
    void setSaveDir(char *path);
    void save();

  protected:
    jack_client_t *client;
    bool activated;
    char *save_dir;
};

void NJSMJackClient::activate(const char *client_name)
{
  if (activated) return;

  DEBUG("Initializing JACK connection");

  DEBUG("Creating JACK client");
  client = jack_client_open(client_name, JackNoStartServer, nullptr);

  if (client == nullptr)
    throw NJSMException("Creating JACK client failed");

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

void NJSMJackClient::setSaveDir(char *path)
{
  DEBUG("Setting JACK session save directory");
  save_dir = path;
}

void NJSMJackClient::save()
{
  DEBUG("Saving JACK session");

  if (save_dir == nullptr)
    throw NJSMException("The save directory must be set before saving");

  jack_session_command_t *ret = jack_session_notify(client,
      nullptr, JackSessionSave, save_dir);

  for (jack_session_command_t *cmd = ret; cmd->uuid != nullptr; cmd++) {
    if (cmd->flags != 0) throw NJSMSaveException();
  }

  DEBUG("JACK session saved");
}

class NJSMNonClient {
  public:
    NJSMNonClient(char *nsm_url);
    ~NJSMNonClient();

    void mainLoop();

  protected:
    lo_address addr;
    lo_server server;

    NJSMJackClient jack;

    void initCallbacks();
    void announce();
    void success(const char *path);

    static int openCallback(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *data);
    static int saveCallback(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *data);
};

NJSMNonClient::NJSMNonClient(char *nsm_url)
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

  initCallbacks();
  announce();
}

NJSMNonClient::~NJSMNonClient()
{
  DEBUG("Cleaning up Non client");
  lo_server_free(server);
  lo_address_free(addr);
  DEBUG("Non cleanup complete");
}

void NJSMNonClient::mainLoop()
{
  while (1) {
    lo_server_recv(server);
  }
}

void NJSMNonClient::initCallbacks()
{
  DEBUG("Adding OSC callbacks");

  lo_server_add_method(server, "/nsm/client/open", "sss", openCallback, this);
  lo_server_add_method(server, "/nsm/client/save", "", saveCallback, this);
}

void NJSMNonClient::announce()
{
  DEBUG("Announcing presence to NSM");

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

  DEBUG("Sending /nsm/server/announce");
  lo_send_from(addr, server, LO_TT_IMMEDIATE,
      "/nsm/server/announce", "sssiii",
      "NJSM", "::", "njsm", 1, 2, getpid());

  while (!ack) lo_server_recv(server);

  lo_server_del_method(server, "/reply", "ssss");
}

void NJSMNonClient::success(const char *path)
{
  lo_send_from(addr, server, LO_TT_IMMEDIATE, "/reply", "ss", path, "");
}

int NJSMNonClient::openCallback(const char *path, const char *types,
    lo_arg **argv, int argc, lo_message msg, void *data)
{
  DEBUG("Opening session on NSM's request");
  NJSMNonClient *me = reinterpret_cast<NJSMNonClient *>(data);
  me->jack.activate(&argv[2]->s);
  me->jack.setSaveDir(&argv[0]->s);
  // me->jack.open();
  me->success(path);
  return 0;
}

int NJSMNonClient::saveCallback(const char *path, const char *types,
    lo_arg **argv, int argc, lo_message msg, void *data)
{
  DEBUG("Saving session on NSM's request");
  NJSMNonClient *me = reinterpret_cast<NJSMNonClient *>(data);
  me->jack.save();
  me->success(path);
  return 0;
}

int main(int argc, char *argv[])
{
  signal(SIGINT, [](int){ throw NJSMExitException(); });
  signal(SIGTERM, [](int){ throw NJSMExitException(); });

  char *nsm_url = getenv("NSM_URL");
  if (nsm_url == nullptr) {
    cerr << "NSM_URL is undefined" << endl;
    return -1;
  } else {
    DEBUG("NSM_URL=" << nsm_url);
  }

  try {
    NJSMNonClient non(nsm_url);
    DEBUG("Running main event loop");
    non.mainLoop();
  } catch (NJSMExitException &e) {
    cerr << e.what() << endl;
  } catch (exception &e) {
    cerr << e.what() << endl;
    return -1;
  }

  return 0;
}

