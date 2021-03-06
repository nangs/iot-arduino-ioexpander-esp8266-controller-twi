#include "WiFiUtil.h"
#include "EEUtil.h"
#include "SerialOS.h"

#include "image.h"
#include "TWIUtil.h"
#include "Config.h"

#include "arduinoNanoUtil.h"

WiFiServer server(WEB_PORT);

//
void reconnectWifi()
{
  if (server.status() != CLOSED)
    server.stop();

  String ssid = "";
  String pass = "";

  readWifiSSID(ssid);
  readWifiPWD(pass);

  Serial.printf("Trying connecting SSID:[%s]\n", ssid.c_str());

  WiFi.begin(ssid.c_str(), pass.c_str());

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println("WiFi shield not present");
    // don't continue:
    while (true)
      ;
  }

  Serial.println("Connecting WiFi ( press CTRL+C to stop )");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print('.');
    if (Serial.available() && Serial.read() == 3)
    {
      printSyntaxHelp();
      return;
    }
  }

  // you're connected now, so print out the data:
  Serial.println("You're connected to the network");
  printCurrentNet();
  printWifiData();

  Serial.println("Type ? for commands");
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, LOW);

  server.begin();
}

#define CCTYPE_HTML 0
#define CCTYPE_JSON 1
#define CCTYPE_TEXT 2
#define CCTYPE_JS 3
#define CCTYPE_PNG 4

void clientOk(WiFiClient &client, int type)
{
  client.println("HTTP/1.1 200 OK");
  switch (type)
  {
  case CCTYPE_HTML:
    Serial.println("[HTML]");
    client.println("Content-Type: text/html");
    break;

  case CCTYPE_JSON:
    Serial.println("[JSON]");
    client.println("Content-Type: application/json");
    break;

  case CCTYPE_TEXT:
    Serial.println("[TEXT]");
    client.println("Content-Type: text/plain");
    break;

  case CCTYPE_JS:
    Serial.println("[JS]");
    client.println("Content-Type: text/javascript");
    break;

  case CCTYPE_PNG:
    Serial.println("[PNG]");
    client.println("Content-Type: image/png");
    break;
  }

#if ENABLE_CORS == 1
  client.println("Access-Control-Allow-Origin: *");
#endif

  //client.println("Connection: close");
  client.println();
}

#define FSTRBUFSZ 80

// buffered writing of string
void clientWriteBigString(WiFiClient &client, const __FlashStringHelper *str)
{
  auto p = (const char PROGMEM *)str;
  char buf[FSTRBUFSZ + 1];

  auto l = 0;
  while (pgm_read_byte(p + (l++)))
    ;

  for (int i = 0; i < l; i += FSTRBUFSZ)
  {
    auto s = FSTRBUFSZ;
    if (i + FSTRBUFSZ > l)
      s = l - i;
    auto k = 0;
    while (s > 0)
    {
      buf[k++] = pgm_read_byte(p++);
      --s;
    }

    buf[k] = 0;
    client.print(buf);
  }
}

// buffered writing of binary
void clientWriteBinary(WiFiClient &client, unsigned char arr[], unsigned int l)
{
  auto j = 0;
  for (int i = 0; i < l; i += FSTRBUFSZ)
  {
    auto s = FSTRBUFSZ;
    if (i + FSTRBUFSZ > l)
      s = l - i;

    client.write((const uint8_t *)(arr + i), s);
  }
}

void clientWriteBinaryF(WiFiClient &client, const unsigned char arr[], unsigned int l)
{
  const unsigned char *p = arr;
  uint8_t buf[FSTRBUFSZ];

  for (int i = 0; i < l; i += FSTRBUFSZ)
  {
    auto s = FSTRBUFSZ;
    if (i + FSTRBUFSZ > l)
      s = l - i;
    auto k = 0;
    for (int j = 0; j < s; ++j)
    {
      buf[k++] = pgm_read_byte(p++);
    }

    client.write((const uint8_t *)buf, s);
  }
}

String header;

//
void manageWifi()
{
  WiFiClient client = server.available();

  if (client)
  {
    String currentLine = "";

    while (client.connected())
    {
      if (client.available())
      {
        char c = client.read();
        if (c != '\n')
          header += c;
        else
        {
          Serial.printf("header [%s]\n", header.c_str());
          while (client.available())
            client.read(); // discard rest of header

          if (header.indexOf("GET / ") >= 0 || header.indexOf("GET /index.htm") >= 0)
          {
            clientOk(client, CCTYPE_HTML);

            clientWriteBigString(client,
#include "index.htm.h"
            );
          }
          else if (header.indexOf("GET /app.js ") >= 0)
          {
            clientOk(client, CCTYPE_JS);

            clientWriteBigString(client,
#include "app.js.h"
            );
          }
          else if (header.indexOf("GET /image.png") >= 0)
          {
            clientOk(client, CCTYPE_PNG);

            //client.write((uint8_t *)image, image_len); // doesn't work data truncated at 2.9k

            //clientWriteBinary(client, image, image_len); // FLASH_VERSION=0 in gen-h

            clientWriteBinaryF(client, image, image_len); // FLASH_VERSION=1 in gen-h
          }
          //
          // /api/scan
          else if (header.indexOf("GET /api/scan ") >= 0)
          {
            Serial.println("/api/scan");
            clientOk(client, CCTYPE_JSON);

            client.print("[");
            auto lst = TWIScan();
            auto n = lst.GetNode(0);
            int i = 0;
            while (n)
            {
              if (i > 0)
                client.print(',');
              client.print(n->data);
              n = n->next;
              ++i;
            }
            client.println("]");
          }
          //
          // /api/getportmodes/<addr>
          else if (header.indexOf("GET /api/getportmodes/") >= 0)
          {
            clientOk(client, CCTYPE_JSON);

            int slaveaddr;
            // retrieve slave address from GET
            {
              String str;
              for (int i = 22; i < header.length(); ++i)
              {
                if (header.c_str()[i] == ' ')
                  break;
                str += header.c_str()[i];
              }
              slaveaddr = atoi(str.c_str());
            }
            Serial.printf("addr[%d]\n", slaveaddr);

            auto portModes = getPortModes(slaveaddr);
            auto n = portModes.GetNode(0);

            client.print("[");
            while (n)
            {
              //Serial.printf("{ \"port\": \"%s\", \"mode\": %d }", n->data.port.c_str(), n->data.mode);
              client.printf("{ \"port\": \"%s\", \"mode\": %d }", n->data.port.c_str(), n->data.mode);
              if (n->next != NULL)
              {
                client.print(",");
                //Serial.print(",");
              }
              n = n->next;
            }

            client.println("]");
          }
          //
          // /api/setportmode/<addr>/<portstr>/<mode>
          else if (header.indexOf("GET /api/setportmode/") >= 0)
          {
            clientOk(client, CCTYPE_JSON);

            int slaveaddr;
            String port;
            int mode;
            // retrieve slave address from GET
            {
              String str;
              int i = 21;
              for (str = ""; i < header.length(); ++i)
              {
                if (header.c_str()[i] == '/')
                {
                  ++i;
                  break;
                }
                str += header.c_str()[i];
              }
              slaveaddr = atoi(str.c_str());

              for (str = ""; i < header.length(); ++i)
              {
                if (header.c_str()[i] == '/')
                {
                  ++i;
                  break;
                }
                str += header.c_str()[i];
              }
              Serial.printf("portstr[%s]\n", str.c_str());
              port = str;

              for (str = ""; i < header.length(); ++i)
              {
                if (header.c_str()[i] == ' ')
                  break;
                str += header.c_str()[i];
              }
              mode = atoi(str.c_str());
            }
            Serial.printf("addr[%d] port[%s] mode[%d]\n", slaveaddr, port.c_str(), mode);

            setPortMode(slaveaddr, port.c_str(), mode);
          }
          //
          // /api/getportvalues/<addr>
          else if (header.indexOf("GET /api/getportvalues/") >= 0)
          {
            clientOk(client, CCTYPE_JSON);

            int slaveaddr;
            // retrieve slave address from GET
            {
              String str;
              for (int i = 23; i < header.length(); ++i)
              {
                if (header.c_str()[i] == ' ')
                  break;
                str += header.c_str()[i];
              }
              slaveaddr = atoi(str.c_str());
            }
            Serial.printf("addr[%d]\n", slaveaddr);

            auto portModes = getPortValues(slaveaddr);
            auto n = portModes.GetNode(0);

            client.print("[");
            while (n)
            {
              //Serial.printf("{ \"port\": \"%s\", \"value\": %d }", n->data.port.c_str(), n->data.value);
              client.printf("{ \"port\": \"%s\", \"value\": %d }", n->data.port.c_str(), n->data.value);
              if (n->next != NULL)
              {
                client.print(",");
                //Serial.print(",");
              }
              n = n->next;
            }

            client.println("]");
          }
          //
          // /api/setportvalue/<addr>/<portstr>/<value>
          else if (header.indexOf("GET /api/setportvalue/") >= 0)
          {
            clientOk(client, CCTYPE_JSON);

            int slaveaddr;
            String port;
            int value;
            // retrieve slave address from GET
            {
              String str;
              int i = 22;
              for (str = ""; i < header.length(); ++i)
              {
                if (header.c_str()[i] == '/')
                {
                  ++i;
                  break;
                }
                str += header.c_str()[i];
              }
              slaveaddr = atoi(str.c_str());

              for (str = ""; i < header.length(); ++i)
              {
                if (header.c_str()[i] == '/')
                {
                  ++i;
                  break;
                }
                str += header.c_str()[i];
              }
              port = str;

              for (str = ""; i < header.length(); ++i)
              {
                if (header.c_str()[i] == ' ')
                  break;
                str += header.c_str()[i];
              }
              value = atoi(str.c_str());
            }
            Serial.printf("addr[%d] port[%s] value[%d]\n", slaveaddr, port.c_str(), value);

            setPortValue(slaveaddr, port.c_str(), value);
          }

          header = "";
          break;
        }
      }
    }

    Serial.println("---> client stop");
    client.stop();
  }
}

//
void printWifiData()
{
  auto ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC address: ");
  for (int i = 0; i < 6; ++i)
  {
    Serial.print(mac[i], HEX);
    if (i != 5)
      Serial.print(":");
    else
      Serial.println();
  }
}

//
void printCurrentNet()
{
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());

  auto bssid = WiFi.BSSID();
  Serial.print("BSSID: ");
  for (int i = 0; i < 6; ++i)
  {
    Serial.print(bssid[i], HEX);
    if (i != 5)
      Serial.print(":");
    else
      Serial.println();
  }

  auto rssi = WiFi.RSSI();
  Serial.printf("signal strength (RSSI): %ld\n", rssi);
}