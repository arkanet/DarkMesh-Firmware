#pragma once
#include "Observer.h"
#include "SinglePortModule.h"

/**
 * Console message handling
 */
class ConsoleModule : public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{
public:
  /** Constructor
   * name is for debugging output
   */
  ConsoleModule() : SinglePortModule("console", meshtastic_PortNum_TEXT_MESSAGE_APP) {
    LOG_INFO("console module");
  }

  static void sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies, bool compressed = false);
  bool command_state=false;

protected:
  /** Called to handle a particular incoming message

  @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
  it
  */
  virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
  virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};

extern ConsoleModule *consoleModule;