#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <SD.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
//#include <Base64.h>

#define REQ_BUF_SZ   50
#define PINS 8
#define TURN_ON LOW
#define TURN_OFF HIGH
#define REQ_TCP_SZ   8
#define LIMIT_VAL 800
#define RESET_EEPROM_TIMEOUT 5000 //время задержки кнопки, для сброса EEPROM, микросекунды (1с = 1000мкс)

byte defaultMac[]  = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte defaultIp[] = { 192, 168, 0, 123 };
byte defaultGateway[] = { 192, 168, 0, 1 };
byte defaultSubnet[] = { 255, 255, 255, 0 };

EthernetServer server(80);
EthernetServer serverTcp(10001);

char HTTP_req[REQ_BUF_SZ] = {0}; // buffered HTTP request stored as null terminated string
char reqTcp[REQ_TCP_SZ] = {0};   // buffered HTTP request stored as null terminated string
char req_index = 0;              // index into HTTP_req buffer

//char login[] = "admin";           // Логин по дефолту. В дальнейшем необходимо измененные данные авторизации в Web морде хранить в EPROM
//char password[] = "kpatechno";    // Пароль по умолчанию. -/-
String auth_hash = "Authorization: Basic YWRtaW46a3BhdGVjaG5v"; // зашифровано login: admin, password:kpatechno; 

byte initFlagExpected[] = {0xDE, 0xED};
struct Settings {
  byte initFlag[2];
  byte mac[6];
  byte ip[4];
  byte gateway[4];
  byte subnet[4];
};
Settings settings;

bool pins[PINS];
bool EnableWatchDog = true;
bool RebootFlag = false; // обозначает необходимость перезагруки устройства, для применения новых настроек (TCP/IP)

int i;
#define buttonResetEEP  15 // 14 = A0, 15 = A1
#define SetupResetPin  7  // номер проверочного пина для обязательного программного ресета при запуске устройства

String GetRebootFlagJSON()
{
      String s;
      if (RebootFlag)
      {
          s = "\"rf\":1,";
      }
      else
      {
        s = "\"rf\":0,";
      }
      return s;
}

boolean isFirstRun()
{
  return settings.initFlag[0] != initFlagExpected[0] ||
         settings.initFlag[1] != initFlagExpected[1];
}

void rebootDevice() 
{
  wdt_disable(); 
  wdt_enable(WDTO_15MS);
  while (1) {}
}

void(* resetFunc) (void) = 0; // Reset MC function


void initSettings()
{
  EEPROM.get(0, settings);
  
  if (isFirstRun())
  {
    settings.initFlag[0] = initFlagExpected[0];
    settings.initFlag[1] = initFlagExpected[1];
    for (i = 0; i < 6; ++i)
    {
      settings.mac[i] = defaultMac[i];
    }
    for (i = 0; i < 4; ++i)
    {
      settings.ip[i] = defaultIp[i];
      settings.gateway[i] = defaultGateway[i];
      settings.subnet[i] = defaultSubnet[i];
    }
    EEPROM.put(0, settings);
  }
  
}

void updateSettings()
{
  RebootFlag = true;
  EEPROM.put(0, settings);
}

void initSD()
{
  // initialize SD card
  if (!SD.begin(4)) {
    Serial.println("SD failed");
    return;         // init failed
  }
}

void initPins()
{
  for (i = 2; i <= PINS + 1; ++i)
  {
    if (i != 4)
      pinMode(i, OUTPUT);
  }
}

void SetupRestart()
{
    delay(1000); 
    pinMode(SetupResetPin, OUTPUT);
    //програмный ресет
    int val = digitalRead(SetupResetPin);
    if (val == TURN_ON)
    {
      digitalWrite(SetupResetPin, TURN_OFF);
      resetFunc();
    }
    else
    {
      digitalWrite(SetupResetPin, TURN_ON);
    } 

//    byte val; // полный ресет (с использованием вотчдога)
//    val = EEPROM.read(254);
//    if (val != 1)
//    {
//      Serial.println(1); // для отладки
//      EEPROM.write(254, 1);
//      digitalWrite(SetupResetPin, TURN_ON);
//      rebootDevice();
//    }
//    else
//    {
//      digitalWrite(SetupResetPin, TURN_ON);
//      Serial.println(2);// для отладки
//      EEPROM.write(254, 0);
//    }

}

void setup()
{
  Serial.begin(9600);
  // SetupRestart();
  
  initSettings();
  initSD();
  initPins();
  setPins();
  
  pins[0] = false;
  // Serial.println("Free RAM: ");
  // Serial.println(FreeRam());

  // Подготавливаем строку авторизации
  //auth_hash = auth_update();
  
  Ethernet.begin(settings.mac, settings.ip, settings.gateway, settings.subnet);
  //Ethernet.begin(defaultMac, settings.ip, settings.gateway, settings.subnet);
  serverTcp.begin();
  server.begin();
  
  if (EnableWatchDog)
  {
    wdt_enable(WDTO_8S); // start wath dog!!
  }
   
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
}

bool checkAuth(EthernetClient* client)
{
      bool StopThis = 1;
      char c;
      String readString;
      while (client->available())
      {
          c = client->read();
          readString += c;
          if (readString.lastIndexOf("Authorization: Basic ") > -1)
          {
            while (client->available())
            {
                c = client->read();
                readString += c;
                if(c == '\n')
                {
                  if (readString.lastIndexOf(auth_hash) > -1)
                  {
                      StopThis = 0;
                  }
                  break;
                }
            }
            break;
          }
          else if(c == '\n')
          {
            readString = "";
          }
          
      }
      readString = "";
      
      if (StopThis)
      {
        client->println(F("HTTP/1.0 401 Unauthorized"));
        client->println(F("WWW-Authenticate: Basic realm=\"Login, please\""));
        client->println(F(""));
        client->stop(); 
      }
      return StopThis;
}

void loop() {
  //  digitalWrite(3, HIGH);   // turn the LED on (HIGH is the voltage level)
  //  delay(1000);              // wait for a second
  //  digitalWrite(3, LOW);    // turn the LED off by making the voltage LOW
  //    delay(1000);              
//    if (Serial.available() != 0 ) // for test WatchDog
//    {
//      digitalWrite(2, ! digitalRead(2));
//      Serial.println(Serial.read());
//      while(1)
//      {
//        ;
//        }
//    }
//    else
//    {
//      wdt_reset();
//      }

  
    if (EnableWatchDog)
    {
      wdt_reset(); // reset wath dog timer!!
    }
    
  char rootFileName[10] = "index.htm";
  File webFile;
  char *filename;
  EthernetClient clientTcp = serverTcp.available();
  EthernetClient client = server.available();
  
  
  if (client) 
  {
    boolean currentLineIsBlank = true;
    req_index = 0;
    while (client.connected()) 
    {
      if (client.available()) 
      {
        char c = client.read(); // считываем по одному символу запрос пользователя
        
     // if ( c==0x0A || c==0x0D ) goto aa; // проверка на какие то левые символы
     // if ( c<0x20 || c>0x7E ) break;
     // aa:
     // if (c != '\n' && c != '\r' && c!= '?') 
     //   if (c2 != c || c2 != c3 || req_index == 0 ) 
     //if (c != -1) 
     if (c != '\n' && c != '\r' && c!= '?')
        {       // если еще не конец строки или конец запроса, то записываем символ в строку запроса
          HTTP_req[req_index] = c;          // save HTTP request character
          req_index++;
          continue;
        }
        // HTTP_req[req_index] = 0;
        filename = 0;        
        
        if (strstr(HTTP_req, "HEAD /") != 0)
        {
            client.println(F("HTTP/1.0 401 Unauthorized"));
            client.println(F("WWW-Authenticate: Basic realm=\"Login, please\""));
            client.println(F(""));
            client.stop();
            break; 
        }
        
          if ((c == '\n' && currentLineIsBlank) || (c == '?' && currentLineIsBlank)) //если запрос уже считан (последний символ равен концу строки), начинается обработка запроса
          {
            // open requested web page file
          
            if (strstr(HTTP_req, "GET / ")) 
            {
              filename = rootFileName;
            }

           if (strstr(HTTP_req, "GET /") != 0 && strstr(HTTP_req, "GET /SetPins") == 0 && strstr(HTTP_req, "GET /SetSettings") == 0 && strstr(HTTP_req, "GET /restart") == 0)
           {            

              
              if (checkAuth(&client))
              {
                break;
              }
              
              // this time no space after the /, so a sub-file
              if (!filename) filename = HTTP_req + 5; // look after the "GET /" (5 chars)
              // a little trick, look for the " HTTP/1.1" string and
              // turn the first character of the substring into a 0 to clear it out.
              (strstr(HTTP_req, " HTTP"))[0] = 0;
        
              // print the file we want
              if (strstr(filename, "jquery.js"))
              {
                webFile = SD.open("files/1.gz");
              }
              else
              {
                  webFile = SD.open(filename);
              }
              
              if (!webFile) 
              {
                
                client.println(F("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<h2>File Not Found!</h2>"));
                break;
              }

             client.println(F("HTTP/1.1 200 OK"));
             if (strstr(filename, ".htm") != 0)
                 client.println(F("Content-Type: text/html"));
             else if (strstr(filename, ".css") != 0)
                 client.println(F("Content-Type: text/css"));
             else if (strstr(filename, ".png") != 0)
                 client.println(F("Content-Type: image/png"));
             else if(strstr(filename, "jquery.js"))
             {
                  client.println(F("Content-Type: application/x-javascript\r\nContent-Encoding: gzip"));
             }
             else if (strstr(filename, ".js") != 0)
                 client.println(F("Content-Type: text/javascript"));
             //else if (strstr(filename, ".js") != 0)
             //    client.println(F("Content-Type: application/x-javascript"));
             else
                 client.println(F("Content-Type: text"));
            // for econom ozu disable file type if not use
            //else if (strstr(filename, ".jpg") != 0) 
            //   client.println("Content-Type: image/jpeg\r\n");
            // else if (strstr(filename, ".gif") != 0)
            //     client.println("Content-Type: image/gif");
            // else if (strstr(filename, ".3gp") != 0)
            //     client.println("Content-Type: video/mpeg");
            // else if (strstr(filename, ".pdf") != 0)
            //     client.println("Content-Type: application/pdf");
             // else if (strstr(filename, ".xml") != 0)
             // client.println("Content-Type: application/xml");
             
             client.println();
              
              
              byte cB[64];
              int cC=0;
                while (webFile.available())
                   {
                     cB[cC]=webFile.read();
                     cC++;
                   if(cC > 63)
                    {
                     client.write(cB,64);
                     cC=0;
                    }
                   }
                  if(cC > 0) client.write(cB,cC);
                   webFile.close();
                   delay(10);
                   client.stop();
                   break;
                
           }
           else if (strstr(HTTP_req, "POST /GetStatePins")) 
           {
            if (checkAuth(&client))
            {
              break;
            }
            client.println(F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nPragma: no-cache\r\n\r\n"));
            client.println();

             String strJson="";
             strJson += "{";
             strJson += GetRebootFlagJSON();
             strJson += "\"r\":[";
             //strJson = "{\"r\":[";
             for (i = 0; i < PINS; ++i)
             {
                if (pins[i])
                {
                  strJson += "1";
                }
                else
                {
                  strJson += "0";
                }
                if (i<(PINS-1))
                {
                  strJson += ",";
                }
                
              }
              strJson += "]}";
             //client.println("{\"r\":[1,1,1,0 ,1,1,1,1]}"); // for tests
             client.println(strJson);
             
            delay(10);
            client.stop();
            break;
          }
          else if (strstr(HTTP_req, "POST /GetStateTCP")) 
           {
            if (checkAuth(&client))
            {
              break;
            }
            client.println(F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nPragma: no-cache\r\n\r\n"));
            client.println();
             
             String strJson="";
             strJson += "{";
             strJson += GetRebootFlagJSON();
             strJson += "\"ip\":[";
             for (i = 0; i < 4; ++i)
             {
                strJson += settings.ip[i];
                
                if (i < 3)
                {
                  strJson += ",";
                }
              }
              
             strJson += "],\"sub\":[";

             for (i = 0; i < 4; ++i)
             {
                strJson += settings.subnet[i];
                
                if (i < 3)
                {
                  strJson += ",";
                }
              }
              
              strJson += "],\"gw\":[";

             for (i = 0; i < 4; ++i)
             {
                strJson += settings.gateway[i];
                
                if (i < 3)
                {
                  strJson += ",";
                }
             }

             strJson += "],\"mac\":[";

             for (i = 0; i < 6; ++i)
             {
              //  strJson += settings.mac[i];
              strJson += F("\"");
              if(settings.mac[i] < 15)
              {
                strJson += "0";
              }
              strJson += String(settings.mac[i], HEX);
              strJson += F("\"");
                if (i < 5)
                {
                  strJson += ",";
                }
             }
            strJson += "]}";
              
            // client.println("{\"ip\":[192,168,0,178],\"sub\":[255,255,255,0],\"gw\":[192,168,0,1]}"); // for tests
            // Serial.println(strJson);
            client.println(strJson);
            
            delay(10);
            client.stop();
            break;
          } 
          else if (strstr(HTTP_req, "GET /SetPins")) 
          {
            resetPins();
            while (client.available())
            {
              // read request body
              char c = client.read();
            
              if (c == 'r')
              {
                
                char relayNum = client.read();
                pins[relayNum - '0' - 2] = true;
               // Serial.print(relayNum);
              }
              if (c == '\n' && currentLineIsBlank)
              {
                break;
              }
            }
              
            if (checkAuth(&client))
            {
              break;
            }
            setPins();
            client.println(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"));
            client.println(F("<script>window.location.href='index.htm'</script>"));
            delay(10);
            break;
          }
          else if (strstr(HTTP_req, "GET /SetSettings")) 
          {
            int newIp[4];
            int newSubnet[4];
            int newGateway[4];
            //int newMac[6];
            int newMac[6];
            int paramCt = 0;
            bool vIp = true, vSub = true, vGw = true, vMac = true;
            while (client.available() && vIp && vSub && vGw && vMac)
            {
              // read request body
              // e.g. ip=192.168.0.9&sub=255.255.0.0&gw=192.168.0.1, mac = DE-AD-BE-EF-FE-ED
              char c = client.read();
              
              if (c == '=')
              {
                //e.g. 192.168.0.9
                int buf = 0;
                char buf_char[2];
                int placeCt = 0;
                //byte buf_mac = 0;
                int buf_mac_count = 0; 
                while (client.available())
                {
                  bool validSym = false;
                  char dig = client.read();
                  
                  if (isdigit(dig) && paramCt != 3)
                  {
                    if (!buf)
                    {
                      buf = dig - '0';
                    }
                    else
                    {
                      buf *= 10;
                      buf += dig - '0';
                    }
                    validSym = true;
                  }
                  else if (paramCt == 3 && normMacSym(dig)) // mac-адрес включает символы латиницы, учтем это
                  {
                     buf_char[buf_mac_count] = dig;
                     validSym = true;
                     buf_mac_count++;
                  }
                  
                  if (dig == '.' || dig == '-' || dig == '&' || !client.available() || dig == ' ')
                  {
                    switch (paramCt)
                    {
                      case 0:
                        newIp[placeCt] = buf;
                        break;
                      case 1:
                        newSubnet[placeCt] = buf;
                        break;
                      case 2:
                        newGateway[placeCt] = buf;
                        break;
                      case 3:
                        //char buf_char2[4];
                        //buf_char2[0] = String("0");
                        //buf_char2[2] = buf_char[0];
                        //buf_char2[3] = buf_char[1];
                        
                        unsigned long result = strtoul(buf_char, NULL, 16); 
                        newMac[placeCt] = result;
                        
                        if (buf_mac_count < 2)
                        {
                          vMac=false;
                        }
                        
                        buf_mac_count = 0;
                        break;
                    }
                    
                    placeCt++;
                    buf = 0;
                    
                    // if (dig == '\r' || dig == '&')
                    if (dig == ' ' || dig == '\r'|| dig == '&')
                    {
                      break;
                    }
                    validSym = true;
                  }
                  else if (paramCt != 3 && !validSym && dig != 's' && dig != 'u' && dig != 'b' && dig != 'g' && dig != 'w'&& dig != 'i' && dig != 'p'&& dig != 'm'&& dig != 'a' && dig != 'c' && dig != '=' && dig != '.'&& dig != '-' && dig != '&' )
                  {
                    switch (paramCt)
                    {
                      case 0:
                        vIp = false;
                        break;
                      case 1:
                        vSub = false;
                        break;
                  // case 3:
                  // vMac = false;
                  // break;
                    }
                  }
                  else if(paramCt == 3 && !validSym && dig != 'm' && dig != 'a' && dig != 'c')
                  {
                    vMac = false;
                    break;
                  }
                }

                if (placeCt != 4 && paramCt != 3)
                {
                  switch (paramCt)
                  {
                    case 0:
                      vIp = false;
                      break;
                    case 1:
                      vSub = false;
                      break;
                    case 2:
                      vGw = false;
                      break;
                    }
                }
                else if(placeCt != 6 && paramCt == 3)
                {
                  switch (paramCt)
                
                  {
                    case 3:
                      vMac = false;
                      break;
                    }
                }    
                paramCt++;
                
              }

              if (c == '\n' && currentLineIsBlank)
              {
                break;
              }
            }

            if (checkAuth(&client))
            {
              break;
            }
            
            if (vSub && vGw && vIp && vMac)
            {
              vSub = isValidSubnet(newSubnet);
              vGw  = isValidIp(newGateway);
              vIp  = isValidIp(newIp);
              vMac = isValidMac(newMac);
            }
            
            if (vSub && vGw && vIp && vMac)
            {
              settings.ip[0] = newIp[0];
              settings.ip[1] = newIp[1];
              settings.ip[2] = newIp[2];
              settings.ip[3] = newIp[3];
              settings.subnet[0]  = newSubnet[0];
              settings.subnet[1]  = newSubnet[1];
              settings.subnet[2]  = newSubnet[2];
              settings.subnet[3]  = newSubnet[3];
              settings.gateway[0] = newGateway[0];
              settings.gateway[1] = newGateway[1];
              settings.gateway[2] = newGateway[2];
              settings.gateway[3] = newGateway[3];
              settings.mac[0] = newMac[0];
              settings.mac[1] = newMac[1];
              settings.mac[2] = newMac[2];
              settings.mac[3] = newMac[3];
              settings.mac[4] = newMac[4];
              settings.mac[5] = newMac[5];

              updateSettings();
              client.println(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n"));
              // client.println("Location: /");
              // client.println(F("<script>alert('Configuration is OK. Page reboot.'); window.location.href='tcp.htm'</script>"));
              client.println(F("<script>window.location.href='tcp.htm'</script>"));
              delay(10);
              break;
            }
            else
            {
              client.println(F("HTTP/1.1 200 OK"));
              client.println(F("Content-Type: text/html"));
              client.println(F("Connnection: close"));
              client.println();
              client.print(F("<html><head><meta charset='utf-8'><title>Некорректный ввод</title></head><body>"));
              client.print(F("<h1>Некорректный ввод</h1><ul>"));
              if (!vIp)
              {
                client.print(F("<li>IP Адрес</li>"));
              }
              if (!vSub)
              {
                client.print(F("<li>Маска подсети</li>"));
              }
              if (!vGw)
              {
                client.print(F("<li>Шлюз</li>"));
              }
               if (!vMac)
              {
                client.print(F("<li>Mac-адрес</li>"));
              }
              client.print(F("<br/><a href='JavaScript:window.history.go(-1)'>Вернуться</a></ul></body></html>"));
              break;
            }

          }
          else if(strstr(HTTP_req, "GET /restart"))
          {
           if (checkAuth(&client))
            {
              break;
            }
            // rebootDevice(); //вызов
            client.println(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"));
            String newIp = F("<html><body><script>window.location.href='http://");
            for (i=0; i < 4; ++i)
            {
              newIp += settings.ip[i];
              if(i < 3)
              {
               newIp += ".";
              }
            }
            newIp += F("/index.htm'</script></body></html>");
            // Settings.ip[0]
            client.println(newIp);
            delay(10);
            client.stop();
            resetFunc(); 
            break;
          }
          else 
          {
            if (checkAuth(&client))
            {
              break;
            }
         
            client.println(F("HTTP/1.1 404 Not Found"));
            client.println();
            delay(10);
          }
         } // end if ((c == '\n' && currentLineIsBlank) || c == '?' && currentLineIsBlank)
           // every line of text received from the client ends with \r\n
        if (c == '\n') 
        {
          // last character on line of received text
          // starting new line with next character read
          currentLineIsBlank = true;
        }
        else if (c != '\r') 
        {
          // a text character was received from client
          currentLineIsBlank = false;
        }
      }// end if (client.available())
    } // end while (client.connected())

    delay(30);      // give the web browser time to receive the data
    client.stop();  // close the connection

  } // end if (client)
  
  // tcp server :10001
  if (clientTcp)
  {
    if (clientTcp.connected()) 
    {
      int tcpIndex = 0;
      while (clientTcp.available()) 
      {
        char c = clientTcp.read();
        if (tcpIndex < (REQ_TCP_SZ - 1)) 
        {
          reqTcp[tcpIndex] = c;
          tcpIndex++;
        }
        //Serial.print(c);
      }
      //(r1_on)
      //(r1_off)
      //(p1)
     
     
  if ((reqTcp[1] == 'r' || reqTcp[1] == 'R') && (reqTcp[5] == 'n' || reqTcp[5] == 'N'))
  {
    byte num = reqTcp[2] - '0';
      switch (num)
        {
            case 1:
              pins[num] = true;
                digitalWrite(num + 1, TURN_ON);
              break;
            case 2:
              pins[num] = true;
                digitalWrite(num + 1, TURN_ON);
              break;
            case 3:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
              break;
            case 4:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
              break;
               case 5:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
              break;
            case 6:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
              break;
              case 7:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
              break;
            default:
              Serial.println("failed");

        }
  }
  if (reqTcp[1] == 'p' || reqTcp[1] == 'P') 
  {
    byte num = reqTcp[2] - '0';
      switch (num)
        {
            case 1:
              pins[num] = true;
                digitalWrite(num + 1, TURN_ON);
                delay (500);
                pins[num] = false;
                digitalWrite(num + 1, TURN_OFF);
              break;
            case 2:
              pins[num] = true;
                digitalWrite(num + 1, TURN_ON);
                delay (500);
                pins[num] = false;
                digitalWrite(num + 1, TURN_OFF);
              break;
            case 3:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
                delay (500);
                pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
            case 4:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
                delay (500);
                pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
               case 5:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
                delay (500);
                pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
            case 6:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
                delay (500);
                pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
              case 7:
              pins[num + 1] = true;
                digitalWrite(num + 2, TURN_ON);
                delay (500);
                pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
            default:
              Serial.println("failed");

        }
  }
  else if ((reqTcp[1] == 'r' || reqTcp[1] == 'R') && (reqTcp[5] == 'f' || reqTcp[5] == 'F'))
  {
    byte num = reqTcp[2] - '0';
      switch (num)
        {
            case 1:
              pins[num] = false;
                digitalWrite(num + 1, TURN_OFF);
              break;
            case 2:
              pins[num] = false;
                digitalWrite(num + 1, TURN_OFF);
              break;
            case 3:
              pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
            case 4:
              pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
               case 5:
              pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
            case 6:
              pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
              case 7:
              pins[num + 1] = false;
                digitalWrite(num + 2, TURN_OFF);
              break;
            default:
              Serial.println("failed");

        }
     }
      StrClear(reqTcp, REQ_TCP_SZ);
    }
  }

    
    boolean isButtonDown = digitalRead(buttonResetEEP);
    // Serial.println(isButtonDown);
    if (isButtonDown) 
    {
      for (i=0; i < RESET_EEPROM_TIMEOUT; i=i+10)
      {
          //Serial.println(i);
          delay(10); // waiting for long up
          isButtonDown = digitalRead(buttonResetEEP);
          if (!isButtonDown)
          {
            break;
          }
      }
      
      if (isButtonDown)
      {  // если она всё ещё нажата
         // reset && reboot
         //Serial.println('t');
        for (int i = 0 ; i < EEPROM.length() ; i++)
        {
          EEPROM.write(i, 0);
        }
        resetFunc(); 
      }
    }
  

  if (analogRead(A0) > LIMIT_VAL && !pins[0])
  {
        pins[2] = true;
        digitalWrite(1, TURN_ON);
        delay(500);
        digitalWrite(1, TURN_OFF);
       pins[i] = false;
  }
}

// sets every element of str to 0 (clears array)
void StrClear(char *str, char length)
{
  for (int i = 0; i < length; i++) {
    str[i] = 0;
  }
}


void resetPins()
{
  for (i = 0; i < PINS; ++i)
  {
    pins[i] = false;
  }
}

void setPins()
{
  for (i = 0; i < PINS; ++i)
  {
    if (i != 2)
    {
      if (pins[i])
      {
        pins[i] = true;
        digitalWrite(i + 2, TURN_ON);
       // delay(500);
       // digitalWrite(i + 2, TURN_OFF);
       // pins[i] = false;
      }
      else
      {
        digitalWrite(i + 2, TURN_OFF);
        pins[i] = false;
      }
    }
  }
}

bool isValidSubnet(int subnet[4])
{
  byte copy[4];
  copy[0] = subnet[0];
  copy[1] = subnet[1];
  copy[2] = subnet[2];
  copy[3] = subnet[3];
  bool zeros = true;
  for (i = 3; i >= 0; --i)
  {
    if (!copy[i] && !zeros)
      return false;
    while (copy[i])
    {
      if (copy[i] & 1)
      {
        if (zeros)
        {
          zeros = false;
        }
      }
      else
      {
        if (!zeros)
          return false;
      }
      copy[i] >>= 1; 
    }
  }
  return isValidIp(subnet) && !zeros;
}

bool isValidIp(int ip[4])
{
  return ip[0] >= 0 && ip[0] <= 255 && ip[1] >= 0 && ip[1] <= 255 && ip[2] >= 0 && ip[2] <= 255 && ip[3] >= 0 && ip[3] <= 255;
}

bool isValidPin(byte pin)
{
  return pin != 0 && pin != 1 && pin != 4 && pin != 10 && pin != 11 && pin != 12 && pin != 13;
}

bool isValidMac(int mac[6])
{
  return mac[0] >= 0 && mac[0] <= 255 && mac[1] >= 0 && mac[1] <= 255 && mac[2] >= 0 && mac[2] <= 255 && mac[3] >= 0 && mac[3] <= 255&& mac[4] >= 0 && mac[4] <= 255&& mac[5] >= 0 && mac[5] <= 255;
}

bool inArray(char mac_sym[], char dig)
{
  //int sizeArr = sizeof(mac_sym);
  int sizeArr = 22;
  int i = 0;
    for (i=0; i<sizeArr; ++i)
    {
//      Serial.println(dig);
//      Serial.println(mac_sym[i]);
      if (dig == mac_sym[i])
      {
        return true;
      }
    }
    return false;
}

bool normMacSym(char dig)
{
    char mac_sym[] = {'a','b','c','d','e','f','A','B','C','D','E','F','0','1','2','3','4','5','6','7','8','9'};
   // char mac_sym = "abcdef";
    if (inArray(mac_sym, dig) != 0)
    {
      return true;
    }
    return false;
}

//String auth_update(char login[], char password[]) 
//{
//  // Строка для будущего хэша
//  String hash  = strcat(strcat(login, ":"),password);
//  int buf_size = hash.length()+1;
//  char buf_char[buf_size];
//  // Переводим строку в char для дальнейшей обработки base64
//  hash.toCharArray(buf_char, buf_size);
//  char encoded[base64_enc_len(buf_size-1)];
//  // Получаем хэш для авторизации
//  base64_encode(encoded, buf_char, buf_size-1);
//  // Формируем полную строку поиска "Authorization: Basic <BASE64-HASH>" чтобы не могли подсунуть хэш в GET или POST
//  Serial.println(encoded);
  //return "Authorization: Basic " + String(encoded);
  //return "Authorization: Basic YWRtaW46a3BhdGVjaG5v";
//}
