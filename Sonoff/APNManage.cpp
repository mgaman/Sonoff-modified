#include "Arduino.h"
#include "APNManage.h"
#include "FS.h"
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

extern Adafruit_MQTT_Publish apnreplies;

bool APNAppend(char ssid[], char pwd[])
{
  bool rc;
  File f = SPIFFS.open(APN_FILENAME, "a+");
  rc = f != 0;
  if (rc)
  {
    f.print(ssid);
    f.write((byte)0);
    f.print(pwd);
    f.write((byte)0);
    f.close();
  }
  return rc;
}
bool APNInit()
{
  return true;
}
bool APNInit(char ssid[],char pwd[])
{
  SPIFFS.remove(APN_FILENAME);
  return APNAppend(ssid,pwd);
}

#define DUMPSIZE 500
char dumpbuff[DUMPSIZE];
bool APNDump()
{
  bool rc;
  char *pBuff;
  File f = SPIFFS.open(APN_FILENAME, "r");
  rc = f != 0;
  if (rc)
  {
    int size = f.size();
    int offset = 0;
    bool copyssid = true;
    int dumplength = 0;
    strcpy(dumpbuff,"Recorded SSIDs: ");
    pBuff = &dumpbuff[strlen(dumpbuff)];
    bool firstchar = true;
    bool ignoressid = false;
    while (offset < size)
    {
      byte b = f.read();
      if (b == 0)  // string end
      {
        if (copyssid && !ignoressid)
          *pBuff++ = ',';  // add comma delimited
        copyssid = !copyssid;
        if (copyssid)
        {
          firstchar = true;
        }
      }
      else
      {
        if (copyssid)
        {
          if (firstchar)
          {
            firstchar = false;
            ignoressid = (b & 0x80) != 0;  // ignore ssid if high bit set
          }
          if (!ignoressid)
          {
            *pBuff++ = b;  // copy ssid only, dont publish passwords
            dumplength++;
            if (dumplength > DUMPSIZE - 20) // bail out if too cdlose to the end
              break;
          }
        }
      }
      offset++;
    }
    *pBuff = 0;
    apnreplies.publish(dumpbuff);
    Serial.println(dumpbuff);
    f.close();
  }
  return rc;
}

bool APNFind(const char target[],char ssid[],char pwd[])
{
  bool found = false;
  char buff[50], *pBuff;
  File f = SPIFFS.open(APN_FILENAME, "r");
  found = f != 0;
  if (found)
  {
    int size = f.size();
    int offset = 0;
    found = false;
    pBuff = buff;
    bool even = true;  // even ssid, odd pwd
    while (offset < size)
    {
      byte b = f.read();
      *pBuff++ = b;
      offset++;
      if (b == 0)
      {
        if (even)
        {
          if (!found)
            found = stricmp(buff,target) == 0;
          if (found)
            strcpy(ssid,buff);
        }
        else 
        {
          if (found)
          {
            strcpy(pwd,buff);
            break;
          }
        }
        even = !even; 
        pBuff = buff;
      }
    }
    f.close();
  }
  return found;
}

/*
 *  Delete does not actually delete a record, it just sets hight bit 
 *  on 1st character of ssid
 */
bool APNDelete(char ssid[])
{
  bool rc = false;
  char buff[50], *pBuff;
  File f = SPIFFS.open(APN_FILENAME, "r+");
  rc = f != 0;
  int ssidstart;   // byte position in file to be marked
  if (rc)
  {
    int size = f.size();
    int offset = 0;
    bool even = true;
    pBuff = buff;
    buff[0] = 0;
    ssidstart = 0;
    rc = false;
    while (offset < size)
    {
      byte b = f.read();
      *pBuff++ = b;
      //    Serial.print(b,HEX);
      if (b == 0)
      {
        if (even)
        {
          if (stricmp(buff,ssid) == 0)
          {
            f.seek(ssidstart,SeekSet);
            byte bb = f.read();
     //       Serial.print("Char ");Serial.print(bb,HEX);Serial.print(" at ");Serial.println(ssidstart);
            bb |= 0x80;
            f.seek(ssidstart,SeekSet);
            f.write(bb);
            f.seek(0,SeekEnd);
            rc = true;
            break;
          }
        }
        even = !even;
        if (even)
          ssidstart = offset+1;
        pBuff = buff;
      }
      offset++;
    }
    f.close();
  }
  return rc;  
}

