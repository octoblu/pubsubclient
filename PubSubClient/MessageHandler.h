#ifndef _MESSAGE_HANDLER_H
#define _MESSAGE_HANDLER_H

class MessageHandler
{
public:
  virtual void onMessage(char* topic, byte* payload, unsigned int length);
};

#endif _MESSAGE_HANDLER_H
