#include <iostream>
#include <unordered_set>
#include <string>
using namespace std;

#include <unistd.h>
#include <signal.h>

#include <jack/jack.h>
#include <jack/session.h>

class NonJackException : public exception {
  public:
    NonJackException(string msg) : message(msg) {}
    virtual const char *what() const throw()
    {
      return message.c_str();
    }

  protected:
    string message;
};

class NonJack {
  public:
    NonJack();
    ~NonJack();

  private:
    static void client_reg_cb(const char *name, int reg, void *sess_saver);

  protected:
    jack_client_t *client;
    unordered_set<string> managed_clients;
    bool activated;

    void find_ports();
};

NonJack::NonJack()
{
  activated = false;
  client = jack_client_open("Session Saver", JackNoStartServer, NULL);

  if (client == NULL)
    throw NonJackException("Creating JACK client failed");

  jack_set_client_registration_callback(client, client_reg_cb, this);

  find_ports();

  if (!jack_activate(client))
    activated = true;
  else
    throw NonJackException("Activating JACK client failed");
}

NonJack::~NonJack()
{
  if (activated)
    jack_deactivate(client);
  jack_client_close(client);
}

void NonJack::find_ports()
{
  const char **ports = jack_get_ports(client, NULL, NULL, 0);
  string client_name;
  for (int i = 0; ports[i]; i++) {
    client_name = ports[i];
    client_name = client_name.substr(0, client_name.find(":"));
    managed_clients.insert(client_name);
  }
}

void NonJack::client_reg_cb(const char *name, int reg, void *saver)
{
  NonJack *me = reinterpret_cast<NonJack *>(saver);
  if (reg) {
    me->managed_clients.insert(name);
  } else {
    me->managed_clients.erase(name);
  }
}

int main()
{
  sigset_t sigint_set;
  sigemptyset(&sigint_set);
  sigaddset(&sigint_set, SIGINT);

  signal(SIGINT, [](int){
      cout << endl << "Exiting..." << endl;
      });

  try {
    NonJack saver;
    pause();
  } catch (exception &e) {
    cerr << e.what() << endl;
    return -1;
  }

  return 0;
}

