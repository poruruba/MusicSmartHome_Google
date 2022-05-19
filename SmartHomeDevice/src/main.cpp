#include <Arduino.h>
#include <M5Core2.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <driver/i2s.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <AudioFileSourceID3.h>
#include <AudioFileSourceSD.h>

#define DEFAULT_AUDIO_GAIN  40.0
#define MUTE_AUDIO_GAIN     2.0

#define LISTEN_PORT 3311
#define REPORT_HOST "192.168.1.16"
#define REPORT_PORT 3314

#define AUDIO_BASE_FOLDER  "/Music"

#define DISCOVERY_PACKET  "HelloLocalHomeSDK"
#define DEFAULT_DEVICE_ID "soundbar"
#define LOCAL_DEVICE_ID   "deviceid123"

#define WIFI_SSID NULL // WiFiアクセスポイントのSSID
#define WIFI_PASSWORD NULL // WiFIアクセスポイントのパスワード
#define WIFI_TIMEOUT  10000
#define SERIAL_TIMEOUT1  10000
#define SERIAL_TIMEOUT2  20000

#define JSONDOC_BUFFER_SIZE 2048
StaticJsonDocument<JSONDOC_BUFFER_SIZE> jsonDoc;

static WiFiUDP udp;

static AudioOutputI2S *out = NULL;
static AudioGeneratorMP3 *mp3 = NULL;
static AudioFileSourceSD *file_sd = NULL;
static AudioFileSourceID3 *id3 = NULL;
static float g_audio_gain = DEFAULT_AUDIO_GAIN;
static bool g_audio_muted = false;
static int g_audio_index = -1;

typedef enum{
  STANDBY,
  STOPPED,
  PLAYING,
  PAUSED
} AUDIO_STATE;
static AUDIO_STATE g_audio_state = STANDBY;

static long m5_initialize(void);
long udpSend(IPAddress host, uint16_t port);
long udpOnOffReport(bool onoff, const char *p_device_id);
long processUdpPacket(const char *p_message);

long audio_initialize(void)
{
  out = new AudioOutputI2S(I2S_NUM_0, AudioOutputI2S::EXTERNAL_I2S);
  out->SetOutputModeMono(true);
  out->SetPinout(12, 0, 2);
  out->SetGain(g_audio_gain / 100.0);

  return 0;
}

long audio_stopMp3(void)
{
  if( mp3 != NULL )
    mp3->stop();
  if( id3 != NULL ){
    id3->RegisterMetadataCB(NULL, NULL);
    id3->close();
  }
  if( file_sd != NULL )
    file_sd->close();

  if( mp3 != NULL ){
    delete mp3;
    mp3 = NULL;
  }
  if( id3 != NULL ){
    delete id3;
    id3 = NULL;
  }
  if( file_sd != NULL ){
    delete file_sd;
    file_sd = NULL;
  }

  g_audio_state = STOPPED;

  return 0;
}

long audio_onoff(bool onoff)
{
  if( !onoff ){
    if( g_audio_state != STANDBY ){
      audio_stopMp3();
      g_audio_state = STANDBY;
      g_audio_muted = false;
    }
  }else{
    if( g_audio_state == STANDBY )
      g_audio_state = STOPPED;
  }

  return 0;
}

long audio_updateGain(void){
  if( g_audio_gain < 0 ) g_audio_gain = 0;
  if( g_audio_gain > 100 ) g_audio_gain = 100;

  if( g_audio_state == PAUSED ){
    out->SetGain(0 / 100.0);
    out->flush();
  }else if( g_audio_muted ){
    out->SetGain(MUTE_AUDIO_GAIN / 100.0);
  }else{
    out->SetGain(g_audio_gain / 100.0);
  }

  return 0;
}

void audio_MDCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
  (void)cbData;
  if (string[0] == '\0') { return; }
  if (strcmp(type, "eof") == 0){
    Serial.println("ID3: eof");
    return;
  }
  Serial.printf("ID3: %s: %s\n", type, string);
}

long audio_playMp3(const char *p_fname, bool reset_gain = false)
{
  audio_stopMp3();

  file_sd = new AudioFileSourceSD(p_fname);
  if( !file_sd->isOpen() ){
    delete file_sd;
    file_sd = NULL;
    return -1;
  }

  audio_onoff(true);
  id3 = new AudioFileSourceID3(file_sd);
  id3->RegisterMetadataCB(audio_MDCallback, (void*)"ID3TAG");
  mp3 = new AudioGeneratorMP3();
  g_audio_state = PLAYING;
  mp3->begin(id3, out);

  return 0;
}

long audio_playNextMp3(bool next){
  File base = SD.open(AUDIO_BASE_FOLDER);
  if( !base )
    return -1;

  long ret;
  File file;
  int num_of_sound = 0;
  for( int i = 0 ; file = base.openNextFile(); i++ ){
    num_of_sound++;
  }

  if( !next ){
    g_audio_index--;
    if( g_audio_index < 0 ) g_audio_index = num_of_sound -1;
  }else{
    g_audio_index++;
    if( g_audio_index >= num_of_sound ) g_audio_index = 0;
  }

  base.rewindDirectory();
  for( int i = 0 ; file = base.openNextFile(); i++ ){
    if( g_audio_index == i ){
      ret = audio_playMp3(file.name());
      file.close();
      base.close();
      return ret;
    }
    file.close();
  }

  return -1;
}

void audio_loop(void)
{
  if( mp3 != NULL ){
    if (mp3->isRunning()) {
      if( g_audio_state == PLAYING ){
        if (!mp3->loop()){
          audio_playNextMp3(true);
//          mp3->stop();
//          g_audio_state = STOPPED;
        }
      }
    }
  }
}

void setup() {
  // put your setup code here, to run once:

  long ret = m5_initialize();
  if( ret != 0 )
    Serial.println("m5_initialize error");

  udp.begin(LISTEN_PORT);

  ret = audio_initialize();
  if( ret != 0 )
    Serial.println("audio_initialize error");

  Serial.println("setup finished");
}

void loop() {
  // put your main code here, to run repeatedly:
  audio_loop();
  M5.update();

  while(true){
    int packetSize = udp.parsePacket();
    if( packetSize > 0 ){
      char *p_buffer = (char*)malloc(packetSize + 1);
      if( p_buffer == NULL )
        break;
      
      int len = udp.read(p_buffer, packetSize);
      if( len <= 0 ){
        free(p_buffer);
        break;
      }
      p_buffer[len] = '\0';

      Serial.println(p_buffer);
      long ret = processUdpPacket(p_buffer);
      free(p_buffer);
      if( ret != 0 )
        Serial.println("processUdpPacket error"); 
    }
    break;
  }

  if( M5.BtnA.wasPressed() ){
    if( g_audio_state == PAUSED ){
      g_audio_state = PLAYING;
      audio_updateGain();
    }else{
      if( g_audio_state == STANDBY )
        udpOnOffReport(true, DEFAULT_DEVICE_ID);
      audio_playNextMp3(false);
    }
  }else if( M5.BtnB.wasPressed() ){
    if( g_audio_state == PAUSED ){
      g_audio_state = PLAYING;
      audio_updateGain();
    }else if( g_audio_state == PLAYING ){
      g_audio_state = PAUSED;
      audio_updateGain();
    }else{
      if( g_audio_state == STANDBY )
        udpOnOffReport(true, DEFAULT_DEVICE_ID);
      audio_playNextMp3(true);
    }
  }else if( M5.BtnC.wasPressed() ){
    if( g_audio_state == PAUSED ){
      g_audio_state = PLAYING;
      audio_updateGain();
    }else{
      if( g_audio_state == STANDBY )
        udpOnOffReport(true, DEFAULT_DEVICE_ID);
      audio_playNextMp3(true);
    }
  }

  delay(1);
}

static long wifi_connect(const char *ssid, const char *password, unsigned long timeout)
{
  Serial.println("");
  Serial.print("WiFi Connenting");

  if( ssid == NULL && password == NULL )
    WiFi.begin();
  else
    WiFi.begin(ssid, password);
  unsigned long past = 0;
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(500);
    past += 500;
    if( past > timeout ){
      Serial.println("\nCan't Connect");
      return -1;
    }
  }
  Serial.print("\nConnected : IP=");
  Serial.print(WiFi.localIP());
  Serial.print(" Mac=");
  Serial.println(WiFi.macAddress());

  return 0;
}

static long m5_initialize(void)
{
  M5.begin(true, true, true, true);
  M5.Axp.SetSpkEnable(true);

//  Serial.begin(115200);
  Serial.println("[initializing]");

  long ret = wifi_connect(WIFI_SSID, WIFI_PASSWORD, WIFI_TIMEOUT);
  if( ret != 0 ){
    char ssid[32 + 1] = {'\0'};
    Serial.print("\ninput SSID:");
    Serial.setTimeout(SERIAL_TIMEOUT1);
    ret = Serial.readBytesUntil('\r', ssid, sizeof(ssid) - 1);
    if( ret <= 0 )
      return -1;

    delay(10);
    Serial.read();
    char password[32 + 1] = {'\0'};
    Serial.print("\ninput PASSWORD:");
    Serial.setTimeout(SERIAL_TIMEOUT2);
    ret = Serial.readBytesUntil('\r', password, sizeof(password) - 1);
    if( ret <= 0 )
      return -1;

    delay(10);
    Serial.read();
    Serial.printf("\nSSID=%s PASSWORD=", ssid);
    for( int i = 0 ; i < strlen(password); i++ )
      Serial.print("*");
    Serial.println("");
    ret = wifi_connect(ssid, password, WIFI_TIMEOUT);
    if( ret != 0 )
      return ret;
  }

  return 0;
}

long processUdpPacket(const char *p_message)
{
  if( strcmp(p_message, DISCOVERY_PACKET ) == 0 ){
      jsonDoc.clear();
      jsonDoc["device_id"] = DEFAULT_DEVICE_ID;
      jsonDoc["local_device_id"] = LOCAL_DEVICE_ID;

      long ret = udpSend(udp.remoteIP(), udp.remotePort());
      if( ret != 0 )
        return ret;
  }else if( strcmp(p_message, "CloseCompanion" ) == 0 ){
    // do nothing
  }else{
    DeserializationError err = deserializeJson(jsonDoc, p_message);
    if (err) {
      Serial.print(F("deserializeJson() failed with code "));
      Serial.println(err.f_str());
      return -1;
    }

    const char *intent = jsonDoc["payload"]["intent"];
    Serial.println(intent);
    const uint32_t msgId = jsonDoc["msgId"];
    if( strcmp( intent, "action.devices.QUERY") == 0 ){
      const char *p_device_id = jsonDoc["payload"]["device_id"];
      Serial.println(p_device_id);
      String device_id(p_device_id);

      if( strcmp(p_device_id, DEFAULT_DEVICE_ID) == 0 ){
        jsonDoc.clear();
        jsonDoc["msgId"] = msgId;
        jsonDoc["payload"]["device_id"] = device_id.c_str();
        jsonDoc["payload"]["states"]["on"] = (g_audio_state != STANDBY);
        jsonDoc["payload"]["states"]["activityState"] = (g_audio_state == STANDBY) ? "STANDBY" : "ACTIVE";
        jsonDoc["payload"]["states"]["playbackState"] = (g_audio_state == PAUSED) ? "PAUSED" : ((g_audio_state == PLAYING) ? "PLAYING" : "STOPPED");
        jsonDoc["payload"]["states"]["isMuted"] = g_audio_muted;
        jsonDoc["payload"]["states"]["currentVolume"] = (int)g_audio_gain;
        jsonDoc["payload"]["states"]["online"] = true;

        long ret = udpSend(udp.remoteIP(), udp.remotePort());
        if( ret != 0 )
          return ret;
      }else{
        Serial.printf("Unknown device_id: %s\n", p_device_id);
      }
    }else if( strcmp( intent, "action.devices.EXECUTE") == 0 ){
      const char *p_device_id = jsonDoc["payload"]["device_id"];
      Serial.println(p_device_id);
      String device_id(p_device_id);

      const char *p_command = jsonDoc["payload"]["command"];
      Serial.println(p_command);

      if( strcmp(p_device_id, DEFAULT_DEVICE_ID) == 0 ){
        if( strcmp(p_command, "action.devices.commands.OnOff") == 0 ){
          bool onoff = jsonDoc["payload"]["params"]["on"];
          audio_onoff(onoff);
          if( onoff )
            audio_playNextMp3(true);

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["on"] = onoff;
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
          ret = udpOnOffReport(onoff, device_id.c_str());
          if( ret != 0 )
            return ret;
        }else
        if( strcmp(p_command, "action.devices.commands.volumeRelative") == 0 ){
          int relativeSteps = jsonDoc["payload"]["params"]["relativeSteps"];
          g_audio_gain += relativeSteps;
          g_audio_muted = false;
          audio_updateGain();

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["currentVolume"] = (int)g_audio_gain;
          jsonDoc["payload"]["states"]["isMuted"] = g_audio_muted;
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
        }else
        if( strcmp(p_command, "action.devices.commands.setVolume") == 0 ){
          int volumeLevel = jsonDoc["payload"]["params"]["volumeLevel"];
          g_audio_gain = volumeLevel;
          g_audio_muted = false;
          audio_updateGain();

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["currentVolume"] = (int)g_audio_gain;
          jsonDoc["payload"]["states"]["isMuted"] = g_audio_muted;
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
        }else
        if( strcmp(p_command, "action.devices.commands.mute") == 0 ){
          bool mute = jsonDoc["payload"]["params"]["mute"];
          g_audio_muted = mute;
          audio_updateGain();

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["currentVolume"] = (int)g_audio_gain;
          jsonDoc["payload"]["states"]["isMuted"] = g_audio_muted;
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
        }else
        if( strcmp(p_command, "action.devices.commands.mediaResume") == 0 ){
          if( g_audio_state == PAUSED ){
            g_audio_state = PLAYING;
            audio_updateGain();
          }else{
            audio_playNextMp3(true);
          }

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
        }else
        if( strcmp(p_command, "action.devices.commands.mediaNext") == 0 ){
          if( g_audio_state == PAUSED ){
            g_audio_state = PLAYING;
            audio_updateGain();
          }
          audio_playNextMp3(true);

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
        }else
        if( strcmp(p_command, "action.devices.commands.mediaPrevious") == 0 ){
          if( g_audio_state == PAUSED ){
            g_audio_state = PLAYING;
            audio_updateGain();
          }
          audio_playNextMp3(false);

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
        }else
        if( strcmp(p_command, "action.devices.commands.mediaPause") == 0 ){
          if( g_audio_state == PLAYING ){
            g_audio_state = PAUSED;
            audio_updateGain();
          }

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
        }else
        if( strcmp(p_command, "action.devices.commands.mediaStop") == 0 ){
          audio_stopMp3();

          jsonDoc.clear();
          jsonDoc["msgId"] = msgId;
          jsonDoc["payload"]["device_id"] = device_id.c_str();
          jsonDoc["payload"]["states"]["online"] = true;
          long ret = udpSend(udp.remoteIP(), udp.remotePort());
          if( ret != 0 )
            return ret;
        }
      }else{
        Serial.printf("Unknown device_id: %s\n", p_device_id);
      }
    }else{
      Serial.printf("Unknown Intent: %s\n", intent);
    }
  }

  return 0;
}

long udpSend(IPAddress ipaddress, uint16_t port)
{
  int len = measureJson(jsonDoc);
  char *p_buffer = (char*)malloc(len + 1);
  if( p_buffer == NULL )
    return -1;
  int wroteLen = serializeJson(jsonDoc, p_buffer, len);
  p_buffer[wroteLen] = '\0';
  Serial.printf("response: %s\n\n", p_buffer);

  udp.beginPacket(ipaddress, port);
  udp.write((const uint8_t*)p_buffer, wroteLen);
  udp.endPacket();

  free(p_buffer);

  return 0;
}

long udpOnOffReport(bool onoff, const char *p_device_id)
{
  jsonDoc.clear();
  jsonDoc["msgId"] = 0;
  jsonDoc["payload"]["device_id"] = p_device_id;
  jsonDoc["payload"]["states"]["on"] = onoff;

  int len = measureJson(jsonDoc);
  char *p_buffer = (char*)malloc(len + 1);
  if( p_buffer == NULL )
    return -1;
  int wroteLen = serializeJson(jsonDoc, p_buffer, len);
  p_buffer[wroteLen] = '\0';
  Serial.printf("report: %s\n\n", p_buffer);

  udp.beginPacket(REPORT_HOST, REPORT_PORT);
  udp.write((const uint8_t*)p_buffer, wroteLen);
  udp.endPacket();

  free(p_buffer);

  return 0;
}

