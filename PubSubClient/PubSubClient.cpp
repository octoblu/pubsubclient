/*
 PubSubClient.cpp - A simple client for MQTT.
  Nicholas O'Leary
  http://knolleary.net
*/

#include "PubSubClient.h"
#include <string.h>

PubSubClient::PubSubClient() {
   this->_client = NULL;
   this->stream = NULL;
}

PubSubClient::PubSubClient(uint8_t *ip, uint16_t port, void (*callback)(char*,uint8_t*,unsigned int), Client& client) {
   this->_client = &client;
   this->callback = callback;
   this->ip = ip;
   this->port = port;
   this->domain = NULL;
   this->stream = NULL;
}

PubSubClient::PubSubClient(char *domain, uint16_t port, MessageHandler *messageHandler, Client& client) {
   this->_client = &client;
   this->messageHandler = messageHandler;
   this->ip = NULL;
   this->port = port;
   this->domain = domain;
   this->stream = NULL;
}

PubSubClient::PubSubClient(char* domain, uint16_t port, void (*callback)(char*,uint8_t*,unsigned int), Client& client) {
   this->_client = &client;
   this->callback = callback;
   this->domain = domain;
   this->port = port;
   this->stream = NULL;
}

PubSubClient::PubSubClient(uint8_t *ip, uint16_t port, void (*callback)(char*,uint8_t*,unsigned int), Client& client, Stream& stream) {
   this->_client = &client;
   this->callback = callback;
   this->ip = ip;
   this->port = port;
   this->domain = NULL;
   this->stream = &stream;
}

PubSubClient::PubSubClient(char* domain, uint16_t port, void (*callback)(char*,uint8_t*,unsigned int), Client& client, Stream& stream) {
   this->_client = &client;
   this->callback = callback;
   this->domain = domain;
   this->port = port;
   this->stream = &stream;
}

boolean PubSubClient::connect(char *id) {
   return connect(id,NULL,NULL,0,0,0,0);
}

boolean PubSubClient::connect(char *id, char *user, char *pass) {
   return connect(id,user,pass,0,0,0,0);
}

boolean PubSubClient::connect(char *id, char* willTopic, uint8_t willQos, uint8_t willRetain, char* willMessage)
{
   return connect(id,NULL,NULL,willTopic,willQos,willRetain,willMessage);
}

boolean PubSubClient::connect(char *id, char *user, char *pass, char* willTopic, uint8_t willQos, uint8_t willRetain, char* willMessage) {
   if (!connected()) {
      int result = 0;

      // PRINTLNF("trying TCP");

     Serial.println("Trying TCP");
     Serial.println(this->domain);
     Serial.println(this->port);

      if (domain != NULL) {
        result = _client->connect(this->domain, this->port);
      } else {
        result = _client->connect(this->ip, this->port);
      }

      Serial.println(_client->available());

      if (result) {
         nextMsgId = 1;
         uint8_t d[9] = {0x00,0x06,'M','Q','I','s','d','p',MQTTPROTOCOLVERSION};
         // Leave room in the buffer for header and variable length field
         uint16_t length = 5;
         unsigned int j;
         for (j = 0;j<9;j++) {
            buffer[length++] = d[j];
         }

         uint8_t v;
         if (willTopic) {
            v = 0x06|(willQos<<3)|(willRetain<<5);
         } else {
            v = 0x02;
         }

         if(user != NULL) {
            v = v|0x80;

            if(pass != NULL) {
               v = v|(0x80>>1);
            }
         }

         buffer[length++] = v;

         buffer[length++] = ((MQTT_KEEPALIVE) >> 8);
         buffer[length++] = ((MQTT_KEEPALIVE) & 0xFF);
         length = writeString(id,buffer,length);
         if (willTopic) {
            length = writeString(willTopic,buffer,length);
            length = writeString(willMessage,buffer,length);
         }

         if(user != NULL) {
            length = writeString(user,buffer,length);
            if(pass != NULL) {
               length = writeString(pass,buffer,length);
            }
         }

         // PRINTLNF("sending MQTTCONNECT");

         write(MQTTCONNECT,buffer,length-5);

         lastInActivity = lastOutActivity = millis();

         while (!_client->available()) {
            unsigned long t = millis();
            if (t-lastInActivity > MQTT_KEEPALIVE*1000UL) {
               PRINTLNF("Err: MQTT_KEEPALIVE timeout");
               _client->stop();
               return false;
            }
         }
         uint8_t llen;
         uint16_t len = readPacket(&llen);

         if (len == 4 && buffer[3] == 0) {
            lastInActivity = millis();
            pingOutstanding = false;
            return true;
         }

         PRINTF("Err: MQTT: ");
         PRINTLN(buffer[3]);
      }
      else{
         PRINTLNF("Err: TCP");
      }
      _client->stop();
   }
   return false;
}

uint8_t PubSubClient::readByte() {
   while(!_client->available()) {}
   char c = _client->read();
   PRINTCH(c, HEX);
   PRINTCH(',');
   return c;
}

uint16_t PubSubClient::readPacket(uint8_t* lengthLength) {
   // PRINTLNF("Received");

   uint16_t len = 0;
   buffer[len++] = readByte();
   bool isPublish = (buffer[0]&0xF0) == MQTTPUBLISH;
   uint32_t multiplier = 1;
   uint16_t length = 0;
   uint8_t digit = 0;
   uint16_t skip = 0;
   uint8_t start = 0;

   do {
      digit = readByte();
      buffer[len++] = digit;
      length += (digit & 127) * multiplier;
      multiplier *= 128;
   } while ((digit & 128) != 0);
   *lengthLength = len-1;

   if (isPublish) {
      // Read in topic length to calculate bytes to skip over for Stream writing
      buffer[len++] = readByte();
      buffer[len++] = readByte();
      skip = (buffer[*lengthLength+1]<<8)+buffer[*lengthLength+2];
      start = 2;
      if (buffer[0]&MQTTQOS1) {
         // skip message id
         skip += 2;
      }
   }

   for (uint16_t i = start;i<length;i++) {
      digit = readByte();
      if (this->stream) {
         if (isPublish && len-*lengthLength-2>skip) {
             this->stream->write(digit);
         }
      }
      if (len < MQTT_MAX_PACKET_SIZE) {
         buffer[len] = digit;
      }
      len++;
   }

   PRINTLN();

   if (!this->stream && len > MQTT_MAX_PACKET_SIZE) {
       len = 0; // This will cause the packet to be ignored.
       PRINTLNF("Err: MQTT_MAX_PACKET_SIZE");
   }



   return len;
}

boolean PubSubClient::loop() {

   if (connected()) {
      unsigned long t = millis();
      if ((t - lastInActivity > MQTT_KEEPALIVE*1000UL) || (t - lastOutActivity > MQTT_KEEPALIVE*1000UL)) {
         if (pingOutstanding) {
            _client->stop();
            PRINTLNF("Err: Ping timed out");
            return false;
         } else {
            buffer[0] = MQTTPINGREQ;
            buffer[1] = 0;
            // PRINTLNF("sending MQTTPINGREQ");
            _client->write(buffer,2);
            lastOutActivity = t;
            lastInActivity = t;
            pingOutstanding = true;
         }
      }
      if (_client->available()) {
         uint8_t llen;
         uint16_t len = readPacket(&llen);
         uint16_t msgId = 0;
         uint8_t *payload;
         if (len > 0) {
            lastInActivity = t;
            uint8_t type = buffer[0]&0xF0;
            if (type == MQTTPUBLISH) {
               // PRINTLNF("MQTTPUBLISH");
               if (messageHandler) {
                  uint16_t tl = (buffer[llen+1]<<8)+buffer[llen+2];
                  char topic[tl+1];
                  for (uint16_t i=0;i<tl;i++) {
                     topic[i] = buffer[llen+3+i];
                  }
                  topic[tl] = 0;
                  // msgId only present for QOS>0
                  if ((buffer[0]&0x06) == MQTTQOS1) {
                    msgId = (buffer[llen+3+tl]<<8)+buffer[llen+3+tl+1];
                    payload = buffer+llen+3+tl+2;
                    messageHandler->onMessage(topic,payload,len-llen-3-tl-2);

                    buffer[0] = MQTTPUBACK;
                    buffer[1] = 2;
                    buffer[2] = (msgId >> 8);
                    buffer[3] = (msgId & 0xFF);
                    // PRINTLNF("sending MQTTPUBACK");
                    _client->write(buffer,4);
                    lastOutActivity = t;

                  } else {
                    payload = buffer+llen+3+tl;
                    messageHandler->onMessage(topic,payload,len-llen-3-tl);
                  }
               }
            } else if (type == MQTTPINGREQ) {
               // PRINTLNF("MQTTPINGREQ");
               buffer[0] = MQTTPINGRESP;
               buffer[1] = 0;
               // PRINTLNF("sending MQTTPINGRESP");
               _client->write(buffer,2);
            } else if (type == MQTTPINGRESP) {
               // PRINTLNF("MQTTPINGRESP");
               pingOutstanding = false;
            }
         }
      }
      return true;
   }
   return false;
}

boolean PubSubClient::publish(char* topic, char* payload) {
   return publish(topic,(uint8_t*)payload,strlen(payload),false);
}

boolean PubSubClient::publish(char* topic, uint8_t* payload, unsigned int plength) {
   return publish(topic, payload, plength, false);
}

boolean PubSubClient::publish(char* topic, uint8_t* payload, unsigned int plength, boolean retained) {
   if (connected()) {

      // PRINTLNF("Publishing");

      // Leave room in the buffer for header and variable length field
      uint16_t length = 5;
      length = writeString(topic,buffer,length);
      uint16_t i;
      for (i=0;i<plength;i++) {
         buffer[length++] = payload[i];
      }
      uint8_t header = MQTTPUBLISH;
      if (retained) {
         header |= 1;
      }
      return write(header,buffer,length-5);
   }
   return false;
}

//Streaming send - send the mqtt header with length, but user is responsable for writing actual buffer
boolean PubSubClient::publishHeader(char* topic, unsigned int plength, boolean retained) {
   if (connected()) {

      // PRINTLNF("Publishing");

      // Leave room in the buffer for header and variable length field
      uint16_t length = 5;
      length = writeString(topic,buffer,length);
      uint16_t i;
      for (i=0;i<plength;i++) {
         length++;
      }
      uint8_t header = MQTTPUBLISH;
      if (retained) {
         header |= 1;
      }
      return write(header,buffer,length-5, false);
   }
   return false;
}

boolean PubSubClient::publish_P(char* topic, uint8_t* PROGMEM payload, unsigned int plength, boolean retained) {
   uint8_t llen = 0;
   uint8_t digit;
   unsigned int rc = 0;
   uint16_t tlen;
   unsigned int pos = 0;
   unsigned int i;
   uint8_t header;
   unsigned int len;

   if (!connected()) {
      return false;
   }

   tlen = strlen(topic);

   header = MQTTPUBLISH;
   if (retained) {
      header |= 1;
   }
   buffer[pos++] = header;
   len = plength + 2 + tlen;
   do {
      digit = len % 128;
      len = len / 128;
      if (len > 0) {
         digit |= 0x80;
      }
      buffer[pos++] = digit;
      llen++;
   } while(len>0);

   pos = writeString(topic,buffer,pos);

   rc += _client->write(buffer,pos);

   for (i=0;i<plength;i++) {
      char c = (char)pgm_read_byte_near(payload + i);
      rc += _client->write(c);
   }

   PRINTLN();

   lastOutActivity = millis();

   return rc == tlen + 4 + plength;
}

boolean PubSubClient::write(uint8_t header, uint8_t* buf, uint16_t length) {
   return write(header, buf, length, true);
}

boolean PubSubClient::write(uint8_t header, uint8_t* buf, uint16_t length, bool sendData) {
   uint8_t lenBuf[4];
   uint8_t llen = 0;
   uint8_t digit;
   uint8_t pos = 0;
   uint8_t rc;
   uint8_t len = length;
   do {
      digit = len % 128;
      len = len / 128;
      if (len > 0) {
         digit |= 0x80;
      }
      lenBuf[pos++] = digit;
      llen++;
   } while(len>0);

   buf[4-llen] = header;
   for (int i=0;i<llen;i++) {
      buf[5-llen+i] = lenBuf[i];
   }

   if(sendData){

      WRITE(buf+(4-llen),length+1+llen);
      rc = _client->write(buf+(4-llen),length+1+llen);
      PRINTLN();

      lastOutActivity = millis();
      return (rc == 1+llen+length);

   }else{

      WRITE(buf+(4-llen),5+llen);
      rc = _client->write(buf+(4-llen),5+llen);
      PRINTLN();

      lastOutActivity = millis();
      return (rc == 5+llen);
   }
}

boolean PubSubClient::subscribe(char* topic) {
  return subscribe(topic, 0);
}

boolean PubSubClient::subscribe(char* topic, uint8_t qos) {
   if (qos < 0 || qos > 1)
     return false;

   if (connected()) {

      // PRINTLNF("sending MQTTSUBSCRIBE|MQTTQOS1");

      // Leave room in the buffer for header and variable length field
      uint16_t length = 5;
      nextMsgId++;
      if (nextMsgId == 0) {
         nextMsgId = 1;
      }
      buffer[length++] = (nextMsgId >> 8);
      buffer[length++] = (nextMsgId & 0xFF);
      length = writeString(topic, buffer,length);
      buffer[length++] = qos;
      int toReturn = write(MQTTSUBSCRIBE|MQTTQOS1,buffer,length-5);
      return toReturn;
   }
   return false;
}

boolean PubSubClient::unsubscribe(char* topic) {
   if (connected()) {

      // PRINTLNF("sending MQTTUNSUBSCRIBE|MQTTQOS1");

      uint16_t length = 5;
      nextMsgId++;
      if (nextMsgId == 0) {
         nextMsgId = 1;
      }
      buffer[length++] = (nextMsgId >> 8);
      buffer[length++] = (nextMsgId & 0xFF);
      length = writeString(topic, buffer,length);
      int toReturn = write(MQTTUNSUBSCRIBE|MQTTQOS1,buffer,length-5);
      PRINTLN();
      return toReturn;
   }
   return false;
}

void PubSubClient::disconnect() {
   // PRINTLNF("sending MQTTDISCONNECT");

   buffer[0] = MQTTDISCONNECT;
   buffer[1] = 0;
   _client->write(buffer,2);
   _client->stop();
   lastInActivity = lastOutActivity = millis();

   PRINTLN();
}

uint16_t PubSubClient::writeString(char* string, uint8_t* buf, uint16_t pos) {
   char* idp = string;
   uint16_t i = 0;
   pos += 2;
   while (*idp) {
      buf[pos++] = *idp++;
      i++;
   }
   buf[pos-i-2] = (i >> 8);
   buf[pos-i-1] = (i & 0xFF);
   return pos;
}


boolean PubSubClient::connected() {
   boolean rc;
   if (_client == NULL ) {
      rc = false;
   } else {
      rc = (int)_client->connected();
      if (!rc) _client->stop();
   }
   return rc;
}

